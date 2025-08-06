#include "jpegarchive.h"
#include "src/util.h"
#include "src/edit.h"
#include "src/smallfry.h"
#include "src/iqa/include/iqa.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Helper function to convert quality preset to target value
static float get_target_from_preset(JpegArchiveQuality preset, JpegArchiveMethod method) {
    if (method == JPEGARCHIVE_METHOD_SSIM) {
        switch (preset) {
            case JPEGARCHIVE_QUALITY_LOW:
                return 0.999;
            case JPEGARCHIVE_QUALITY_MEDIUM:
                return 0.9999;
            case JPEGARCHIVE_QUALITY_HIGH:
                return 0.99995;
            case JPEGARCHIVE_QUALITY_VERYHIGH:
                return 0.99999;
            default:
                return 0.9999;
        }
    }
    return 0.9999;
}

JpegRecompressOutput jpeg_recompress(JpegRecompressInput input) {
    JpegRecompressOutput output;
    memset(&output, 0, sizeof(output));
    
    // Validate input
    if (!input.jpeg || input.length <= 0) {
        output.error_code = JPEGARCHIVE_INVALID_INPUT;
        return output;
    }
    
    // Check if input is JPEG
    if (!checkJpegMagic(input.jpeg, input.length)) {
        output.error_code = JPEGARCHIVE_NOT_JPEG;
        return output;
    }
    
    // Set default values if not provided
    int min = (input.min > 0) ? input.min : 40;
    int max = (input.max > 0) ? input.max : 95;
    int loops = (input.loops > 0) ? input.loops : 6;
    
    if (min > max) {
        output.error_code = JPEGARCHIVE_INVALID_INPUT;
        return output;
    }
    
    float target = get_target_from_preset(input.quality, input.method);
    
    // Decode original image
    unsigned char *original = NULL;
    unsigned char *originalGray = NULL;
    int width, height;
    
    long originalSize = decodeJpeg((unsigned char *)input.jpeg, input.length, &original, &width, &height, JCS_RGB);
    if (!originalSize) {
        output.error_code = JPEGARCHIVE_NOT_JPEG;
        return output;
    }
    
    // Convert to grayscale for comparison
    long originalGraySize = grayscale(original, &originalGray, width, height);
    if (!originalGraySize) {
        free(original);
        output.error_code = JPEGARCHIVE_MEMORY_ERROR;
        return output;
    }
    
    // Check if already processed and get metadata
    unsigned char *metaBuf = NULL;
    unsigned int metaSize = 0;
    
    // First check if already processed
    unsigned char *tempBuf = NULL;
    unsigned int tempSize = 0;
    if (getMetadata(input.jpeg, input.length, &tempBuf, &tempSize, "Compressed by jpeg-recompress")) {
        // Comment found - file already processed
        free(tempBuf);
        free(original);
        free(originalGray);
        output.error_code = JPEGARCHIVE_NOT_SUITABLE;
        return output;
    }
    // Free tempBuf if it was allocated (when comment not found)
    if (tempBuf) {
        free(tempBuf);
        tempBuf = NULL;
    }
    
    // Get metadata for preservation (without comment check)
    getMetadata(input.jpeg, input.length, &metaBuf, &metaSize, NULL);
    
    // Binary search for optimal quality
    unsigned char *compressed = NULL;
    unsigned long compressedSize = 0;
    int finalQuality = min;
    float finalMetric = 0;
    
    for (int attempt = loops - 1; attempt >= 0; --attempt) {
        int quality = min + (max - min) / 2;
        
        if (min == max) {
            attempt = 0;
        }
        
        // Free previous compression if exists
        if (compressed && attempt > 0) {
            free(compressed);
            compressed = NULL;
        }
        
        // Compress with current quality
        int progressive = (attempt == 0) ? 1 : 0;
        int optimize = (attempt == 0) ? 1 : 0;
        compressedSize = encodeJpeg(&compressed, original, width, height, JCS_RGB, quality, progressive, optimize, SUBSAMPLE_DEFAULT);
        
        if (!compressedSize) {
            free(original);
            free(originalGray);
            if (compressed) free(compressed);
            if (metaBuf) free(metaBuf);
            output.error_code = JPEGARCHIVE_MEMORY_ERROR;
            return output;
        }
        
        // Decode compressed for comparison
        unsigned char *compressedGray = NULL;
        long compressedGraySize = decodeJpeg(compressed, compressedSize, &compressedGray, &width, &height, JCS_GRAYSCALE);
        
        if (!compressedGraySize) {
            free(original);
            free(originalGray);
            free(compressed);
            if (metaBuf) free(metaBuf);
            output.error_code = JPEGARCHIVE_UNKNOWN_ERROR;
            return output;
        }
        
        // Calculate metric
        float metric = 0;
        if (input.method == JPEGARCHIVE_METHOD_SSIM) {
            metric = iqa_ssim(originalGray, compressedGray, width, height, width, 0, 0);
        }
        
        finalQuality = quality;
        finalMetric = metric;
        
        // Adjust quality based on metric
        if (metric < target) {
            min = (quality + 1 < max) ? quality + 1 : max;
        } else {
            max = (quality - 1 > min) ? quality - 1 : min;
        }
        
        free(compressedGray);
        
        // Keep compressed data on last iteration
        if (attempt > 0) {
            free(compressed);
            compressed = NULL;
        }
    }
    
    // Check if output is larger than input
    if (compressedSize >= (unsigned long)input.length) {
        free(original);
        free(originalGray);
        free(compressed);
        if (metaBuf) free(metaBuf);
        output.error_code = JPEGARCHIVE_NOT_SUITABLE;
        return output;
    }
    
    // Build complete JPEG with metadata and comment
    const char *COMMENT = "Compressed by jpeg-recompress";
    
    // Check APP0 marker
    if (compressed[2] != 0xff || compressed[3] != 0xe0) {
        free(original);
        free(originalGray);
        free(compressed);
        if (metaBuf) free(metaBuf);
        output.error_code = JPEGARCHIVE_UNKNOWN_ERROR;
        return output;
    }
    
    int app0_len = (compressed[4] << 8) + compressed[5];
    
    // Calculate total size: SOI+APP0 + COM + metadata + image data
    unsigned long totalSize = 4 + app0_len + 4 + strlen(COMMENT) + metaSize + (compressedSize - 4 - app0_len);
    
    unsigned char *finalJpeg = malloc(totalSize);
    if (!finalJpeg) {
        free(original);
        free(originalGray);
        free(compressed);
        if (metaBuf) free(metaBuf);
        output.error_code = JPEGARCHIVE_MEMORY_ERROR;
        return output;
    }
    
    unsigned char *ptr = finalJpeg;
    
    // Copy SOI and APP0
    memcpy(ptr, compressed, 4 + app0_len);
    ptr += 4 + app0_len;
    
    // Add COM marker
    *ptr++ = 0xff;
    *ptr++ = 0xfe;
    *ptr++ = 0x00;
    *ptr++ = strlen(COMMENT) + 2;
    memcpy(ptr, COMMENT, strlen(COMMENT));
    ptr += strlen(COMMENT);
    
    // Add original metadata
    if (metaSize > 0) {
        memcpy(ptr, metaBuf, metaSize);
        ptr += metaSize;
    }
    
    // Add remaining image data
    memcpy(ptr, compressed + 4 + app0_len, compressedSize - 4 - app0_len);
    
    // Prepare output
    output.error_code = JPEGARCHIVE_OK;
    output.jpeg = finalJpeg;
    output.length = totalSize;
    output.quality = finalQuality;
    output.metric = finalMetric;
    
    free(compressed);
    free(original);
    free(originalGray);
    if (metaBuf) free(metaBuf);
    
    return output;
}

void free_jpeg_recompress_output(JpegRecompressOutput *output) {
    if (output && output->jpeg) {
        free(output->jpeg);
        output->jpeg = NULL;
        output->length = 0;
    }
}

JpegCompareOutput jpeg_compare(JpegCompareInput input) {
    JpegCompareOutput output;
    memset(&output, 0, sizeof(output));
    
    // Validate input
    if (!input.jpeg1 || !input.jpeg2 || input.length1 <= 0 || input.length2 <= 0) {
        output.error_code = JPEGARCHIVE_INVALID_INPUT;
        return output;
    }
    
    // Check if inputs are JPEG
    if (!checkJpegMagic(input.jpeg1, input.length1) || !checkJpegMagic(input.jpeg2, input.length2)) {
        output.error_code = JPEGARCHIVE_NOT_JPEG;
        return output;
    }
    
    // Decode first image
    unsigned char *image1 = NULL;
    int width1, height1;
    long size1 = decodeJpeg((unsigned char *)input.jpeg1, input.length1, &image1, &width1, &height1, JCS_GRAYSCALE);
    
    if (!size1) {
        output.error_code = JPEGARCHIVE_NOT_JPEG;
        return output;
    }
    
    // Decode second image
    unsigned char *image2 = NULL;
    int width2, height2;
    long size2 = decodeJpeg((unsigned char *)input.jpeg2, input.length2, &image2, &width2, &height2, JCS_GRAYSCALE);
    
    if (!size2) {
        free(image1);
        output.error_code = JPEGARCHIVE_NOT_JPEG;
        return output;
    }
    
    // Check dimensions match
    if (width1 != width2 || height1 != height2) {
        free(image1);
        free(image2);
        output.error_code = JPEGARCHIVE_UNSUPPORTED;
        return output;
    }
    
    // Calculate metric
    double metric = 0;
    if (input.method == JPEGARCHIVE_METHOD_SSIM) {
        metric = iqa_ssim(image1, image2, width1, height1, width1, 0, 0);
    }
    
    free(image1);
    free(image2);
    
    output.error_code = JPEGARCHIVE_OK;
    output.metric = metric;
    
    return output;
}

void free_jpeg_compare_output(JpegCompareOutput *output) {
    // Nothing to free for compare output
    (void)output;
}