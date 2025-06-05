#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "jpegarchive.h"
#include "src/util.h"
#include "src/edit.h"
#include "src/iqa/include/iqa.h"

// strdup implementation for systems that don't have it
#ifndef strdup
static char *lib_strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *result = malloc(len);
    if (result) {
        memcpy(result, s, len);
    }
    return result;
}
#define strdup lib_strdup
#endif

// Global variables for library state
static char error_buffer[1024];

void jpeg_recompress_free_result(jpeg_recompress_result *result) {
    if (result && result->error) {
        free(result->error);
        result->error = NULL;
    }
}

void jpeg_recompress(const char *input_path, const char *output_path, int quality_preset, jpeg_recompress_result *result) {
    // Initialize result
    result->exit_code = 0;
    result->quality = 0;
    result->ssim = 0.0;
    result->error = NULL;

    // Clear error buffer
    error_buffer[0] = '\0';
    
    // Initialize progname for error handling
    extern const char *progname;
    progname = "jpeg-recompress-lib";
    
    // Variables for metadata
    unsigned char *metaBuf = NULL;
    unsigned int metaSize = 0;

    // Validate quality preset
    if (quality_preset < JPEG_RECOMPRESS_QUALITY_LOW || quality_preset > JPEG_RECOMPRESS_QUALITY_VERYHIGH) {
        result->exit_code = 1;
        result->error = strdup("Invalid quality preset");
        return;
    }

    // Set default parameters
    int attempts = 6;
    float target = 0;
    int jpegMin = 40;
    int jpegMax = 95;
    int strip = 0;
    int accurate = 0;
    int subsample = SUBSAMPLE_DEFAULT;
    int copyFiles = 1;

    // Set target based on quality preset for SSIM method
    switch (quality_preset) {
        case JPEG_RECOMPRESS_QUALITY_LOW:
            target = 0.999;
            break;
        case JPEG_RECOMPRESS_QUALITY_MEDIUM:
            target = 0.9999;
            break;
        case JPEG_RECOMPRESS_QUALITY_HIGH:
            target = 0.99995;
            break;
        case JPEG_RECOMPRESS_QUALITY_VERYHIGH:
            target = 0.99999;
            break;
    }

    // Read input file
    unsigned char *buf = NULL;
    long bufSize = readFile((char *)input_path, (void **) &buf);
    if (bufSize <= 0) {
        result->exit_code = 1;
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Could not read input file: %s (size: %ld)", input_path, bufSize);
        result->error = strdup(error_msg);
        return;
    }

    // Detect input file type
    enum filetype inputFiletype = detectFiletypeFromBuffer(buf, bufSize);
    if (inputFiletype == FILETYPE_UNKNOWN) {
        // Force JPEG type if detection failed but size is reasonable
        if (bufSize > 100 && buf[0] == 0xFF && buf[1] == 0xD8) {
            inputFiletype = FILETYPE_JPEG;
        }
    }
    
    // Decode original image
    unsigned char *original = NULL;
    int width, height;
    long originalSize = decodeFileFromBuffer(buf, bufSize, &original, inputFiletype, &width, &height, JCS_RGB);
    if (!originalSize) {
        free(buf);
        result->exit_code = 1;
        char error_msg[512];
        if (bufSize >= 2) {
            snprintf(error_msg, sizeof(error_msg), "Invalid input file (type: %d, bufSize: %ld, magic: %02X%02X)", 
                    inputFiletype, bufSize, buf[0], buf[1]);
        } else {
            snprintf(error_msg, sizeof(error_msg), "Invalid input file (type: %d, bufSize: %ld)", 
                    inputFiletype, bufSize);
        }
        result->error = strdup(error_msg);
        return;
    }

    // Convert to grayscale for comparison
    unsigned char *originalGray = NULL;
    long originalGraySize = grayscale(original, &originalGray, width, height);
    if (!originalGraySize) {
        free(buf);
        free(original);
        result->exit_code = 1;
        result->error = strdup("Failed to convert to grayscale");
        return;
    }

    // Check for existing jpeg-recompress comment and read metadata
    if (inputFiletype == FILETYPE_JPEG) {
        // First try to get metadata with comment check
        if (getMetadata(buf, bufSize, &metaBuf, &metaSize, "Compressed by jpeg-recompress")) {
            // File already processed
            if (copyFiles) {
                // Just copy the file
                FILE *out = fopen(output_path, "wb");
                if (!out) {
                    free(buf);
                    free(original);
                    free(originalGray);
                    result->exit_code = 1;
                    result->error = strdup("Could not open output file");
                    return;
                }
                fwrite(buf, bufSize, 1, out);
                fclose(out);
                
                free(buf);
                free(original);
                free(originalGray);
                result->exit_code = 0;
                return;
            } else {
                free(buf);
                free(original);
                free(originalGray);
                result->exit_code = 2;
                result->error = strdup("File already processed by jpeg-recompress");
                return;
            }
        } else {
            // File not already processed, get all metadata
            getMetadata(buf, bufSize, &metaBuf, &metaSize, NULL);
        }
    }

    // Binary search for optimal quality
    int min = jpegMin, max = jpegMax;
    unsigned char *compressed = NULL;
    unsigned long compressedSize = 0;
    float finalMetric = 0;
    int finalQuality = 0;

    for (int attempt = attempts - 1; attempt >= 0; --attempt) {
        int quality = min + (max - min) / 2;
        
        if (min == max)
            attempt = 0;

        int progressive = attempt ? 0 : 1;  // Progressive on final attempt
        int optimize = accurate ? 1 : (attempt ? 0 : 1);

        // Free previous compressed data if any
        if (compressed && attempt) {
            free(compressed);
            compressed = NULL;
        }

        // Compress at current quality
        compressedSize = encodeJpeg(&compressed, original, width, height, JCS_RGB, quality, progressive, optimize, subsample);
        if (!compressedSize) {
            free(buf);
            free(original);
            free(originalGray);
            result->exit_code = 1;
            result->error = strdup("Failed to encode JPEG");
            return;
        }

        // Decode compressed image for comparison
        unsigned char *compressedGray = NULL;
        int cWidth, cHeight;
        long compressedGraySize = decodeJpeg(compressed, compressedSize, &compressedGray, &cWidth, &cHeight, JCS_GRAYSCALE);
        if (!compressedGraySize) {
            free(buf);
            free(original);
            free(originalGray);
            free(compressed);
            result->exit_code = 1;
            result->error = strdup("Failed to decode compressed image");
            return;
        }

        // Calculate SSIM
        float metric = iqa_ssim(originalGray, compressedGray, width, height, width, 0, 0);
        
        if (!attempt) {
            finalMetric = metric;
            finalQuality = quality;
        }

        // Adjust search range based on result
        if (metric < target) {
            // Too distorted, increase quality
            min = (quality + 1 < max) ? quality + 1 : max;
        } else {
            // Higher than required, decrease quality
            max = (quality - 1 > min) ? quality - 1 : min;
        }

        free(compressedGray);
    }

    // Check if output would be larger than input
    if (compressedSize >= bufSize) {
        if (copyFiles) {
            // Just copy the original
            FILE *out = fopen(output_path, "wb");
            if (!out) {
                free(buf);
                free(original);
                free(originalGray);
                free(compressed);
                result->exit_code = 1;
                result->error = strdup("Could not open output file");
                return;
            }
            fwrite(buf, bufSize, 1, out);
            fclose(out);
            
            free(buf);
            free(original);
            free(originalGray);
            free(compressed);
            result->exit_code = 0;
            result->quality = finalQuality;
            result->ssim = finalMetric;
            return;
        } else {
            free(buf);
            free(original);
            free(originalGray);
            free(compressed);
            result->exit_code = 1;
            result->error = strdup("Output file would be larger than input");
            return;
        }
    }

    // Write output file
    FILE *out = fopen(output_path, "wb");
    if (!out) {
        free(buf);
        free(original);
        free(originalGray);
        free(compressed);
        result->exit_code = 1;
        result->error = strdup("Could not open output file");
        return;
    }

    // Write SOI marker and APP0
    if (!checkJpegMagic(compressed, compressedSize) || 
        compressed[2] != 0xff || compressed[3] != 0xe0) {
        fclose(out);
        free(buf);
        free(original);
        free(originalGray);
        free(compressed);
        result->exit_code = 1;
        result->error = strdup("Invalid JPEG structure");
        return;
    }

    int app0_len = (compressed[4] << 8) + compressed[5];
    fwrite(compressed, 4 + app0_len, 1, out);

    // Write comment marker
    const char *comment = "Compressed by jpeg-recompress";
    fputc(0xff, out);
    fputc(0xfe, out);
    fputc(0x00, out);
    fputc(strlen(comment) + 2, out);
    fwrite(comment, strlen(comment), 1, out);

    // Write metadata if preserving and input was JPEG
    if (!strip && inputFiletype == FILETYPE_JPEG && metaSize > 0) {
        fwrite(metaBuf, metaSize, 1, out);
    }

    // Write remaining image data
    fwrite(compressed + 4 + app0_len, compressedSize - 4 - app0_len, 1, out);
    fclose(out);

    // Clean up
    free(buf);
    free(original);
    free(originalGray);
    free(compressed);
    if (metaBuf) {
        free(metaBuf);
    }

    // Set success result
    result->exit_code = 0;
    result->quality = finalQuality;
    result->ssim = finalMetric;
    
    if (error_buffer[0] != '\0') {
        result->error = strdup(error_buffer);
    }
}