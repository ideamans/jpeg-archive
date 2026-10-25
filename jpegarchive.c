#include "jpegarchive.h"
#include "src/util.h"
#include "src/edit.h"
#include "src/smallfry.h"
#include "src/iqa/include/iqa.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <math.h>
#include <jpeglib.h>

// Custom error handler for libjpeg to prevent process termination
struct jpegarchive_error_mgr {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};

typedef struct jpegarchive_error_mgr * jpegarchive_error_ptr;

static void jpegarchive_error_exit(j_common_ptr cinfo) {
    jpegarchive_error_ptr myerr = (jpegarchive_error_ptr) cinfo->err;
    longjmp(myerr->setjmp_buffer, 1);
}

// Safe version of decodeJpeg that doesn't exit on errors
static unsigned long safeDecodeJpeg(unsigned char *buf, unsigned long bufSize, unsigned char **image, int *width, int *height, int pixelFormat, jpegarchive_error_code_t *error) {
    struct jpeg_decompress_struct cinfo;
    struct jpegarchive_error_mgr jerr;
    int row_stride;
    JSAMPARRAY buffer;
    
    *error = JPEGARCHIVE_OK;
    
    // Set up error handling
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpegarchive_error_exit;
    
    // Establish the setjmp return context
    if (setjmp(jerr.setjmp_buffer)) {
        // If we get here, libjpeg encountered an error
        jpeg_destroy_decompress(&cinfo);
        *error = JPEGARCHIVE_UNSUPPORTED;
        return 0;
    }
    
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, buf, bufSize);
    
    // Read header
    jpeg_read_header(&cinfo, TRUE);
    
    // Check for unsupported color spaces
    if (cinfo.jpeg_color_space == JCS_CMYK || cinfo.jpeg_color_space == JCS_YCCK) {
        jpeg_destroy_decompress(&cinfo);
        *error = JPEGARCHIVE_UNSUPPORTED;
        return 0;
    }

    // Check if conversion is possible
    if (pixelFormat == JCS_RGB && cinfo.jpeg_color_space != JCS_RGB &&
        cinfo.jpeg_color_space != JCS_YCbCr && cinfo.jpeg_color_space != JCS_GRAYSCALE) {
        jpeg_destroy_decompress(&cinfo);
        *error = JPEGARCHIVE_UNSUPPORTED;
        return 0;
    }

    // Check if grayscale conversion is possible
    if (pixelFormat == JCS_GRAYSCALE && cinfo.jpeg_color_space != JCS_RGB &&
        cinfo.jpeg_color_space != JCS_YCbCr && cinfo.jpeg_color_space != JCS_GRAYSCALE) {
        jpeg_destroy_decompress(&cinfo);
        *error = JPEGARCHIVE_UNSUPPORTED;
        return 0;
    }

    cinfo.out_color_space = pixelFormat;
    
    // Start decompression
    jpeg_start_decompress(&cinfo);
    
    *width = cinfo.output_width;
    *height = cinfo.output_height;
    
    // Allocate temporary row
    row_stride = (*width) * cinfo.output_components;
    buffer = (*cinfo.mem->alloc_sarray)((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);
    
    // Allocate image pixel buffer
    *image = malloc(row_stride * (*height));
    if (!*image) {
        jpeg_destroy_decompress(&cinfo);
        *error = JPEGARCHIVE_MEMORY_ERROR;
        return 0;
    }
    
    // Read image row by row
    int row = 0;
    while (cinfo.output_scanline < cinfo.output_height) {
        (void) jpeg_read_scanlines(&cinfo, buffer, 1);
        memcpy((void *)((*image) + row_stride * row), buffer[0], row_stride);
        row++;
    }
    
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    
    return row_stride * (*height);
}

// Safe version of encodeJpeg that doesn't exit on errors
static unsigned long safeEncodeJpeg(unsigned char **jpeg, unsigned char *buf, int width, int height, int pixelFormat, int quality, int progressive, int optimize, int subsample, jpegarchive_error_code_t *error) {
    long unsigned int jpegSize = 0;
    struct jpeg_compress_struct cinfo;
    struct jpegarchive_error_mgr jerr;
    JSAMPROW row_pointer[1];
    int row_stride = width * (pixelFormat == JCS_RGB ? 3 : 1);

    *error = JPEGARCHIVE_OK;

    // Set up error handling
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpegarchive_error_exit;

    // Establish the setjmp return context
    if (setjmp(jerr.setjmp_buffer)) {
        // If we get here, libjpeg encountered an error
        jpeg_destroy_compress(&cinfo);
        *error = JPEGARCHIVE_UNKNOWN_ERROR;
        return 0;
    }

    jpeg_create_compress(&cinfo);

    if (!optimize) {
        if (jpeg_c_int_param_supported(&cinfo, JINT_COMPRESS_PROFILE)) {
            jpeg_c_set_int_param(&cinfo, JINT_COMPRESS_PROFILE, JCP_FASTEST);
        }
    }

    jpeg_mem_dest(&cinfo, jpeg, &jpegSize);

    // Set options
    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = pixelFormat == JCS_RGB ? 3 : 1;
    cinfo.in_color_space = pixelFormat;

    jpeg_set_defaults(&cinfo);

    if (!optimize) {
        if (jpeg_c_bool_param_supported(&cinfo, JBOOLEAN_TRELLIS_QUANT)) {
            jpeg_c_set_bool_param(&cinfo, JBOOLEAN_TRELLIS_QUANT, FALSE);
        }
        if (jpeg_c_bool_param_supported(&cinfo, JBOOLEAN_TRELLIS_QUANT_DC)) {
            jpeg_c_set_bool_param(&cinfo, JBOOLEAN_TRELLIS_QUANT_DC, FALSE);
        }
    }

    if (optimize && !progressive) {
        cinfo.scan_info = NULL;
        cinfo.num_scans = 0;
        if (jpeg_c_bool_param_supported(&cinfo, JBOOLEAN_OPTIMIZE_SCANS)) {
            jpeg_c_set_bool_param(&cinfo, JBOOLEAN_OPTIMIZE_SCANS, FALSE);
        }
    }

    if (!optimize && progressive) {
        jpeg_simple_progression(&cinfo);
    }

    // Handle subsampling
    if (cinfo.input_components == 3 && cinfo.in_color_space == JCS_RGB) {
        // Set default sampling factors based on libjpeg's defaults
        jpeg_set_colorspace(&cinfo, JCS_YCbCr);

        if (subsample == SUBSAMPLE_444) {
            // 4:4:4 - no subsampling
            cinfo.comp_info[0].h_samp_factor = 1;
            cinfo.comp_info[0].v_samp_factor = 1;
            cinfo.comp_info[1].h_samp_factor = 1;
            cinfo.comp_info[1].v_samp_factor = 1;
            cinfo.comp_info[2].h_samp_factor = 1;
            cinfo.comp_info[2].v_samp_factor = 1;
        } else if (subsample == SUBSAMPLE_422) {
            // 4:2:2 - horizontal subsampling
            cinfo.comp_info[0].h_samp_factor = 2;
            cinfo.comp_info[0].v_samp_factor = 1;
            cinfo.comp_info[1].h_samp_factor = 1;
            cinfo.comp_info[1].v_samp_factor = 1;
            cinfo.comp_info[2].h_samp_factor = 1;
            cinfo.comp_info[2].v_samp_factor = 1;
        }
        // else SUBSAMPLE_DEFAULT (4:2:0) - use libjpeg's defaults
    }

    jpeg_set_quality(&cinfo, quality, TRUE);

    jpeg_start_compress(&cinfo, TRUE);

    // Write image
    while (cinfo.next_scanline < cinfo.image_height) {
        row_pointer[0] = &buf[cinfo.next_scanline * row_stride];
        (void) jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);

    return jpegSize;
}

// Helper function to detect original subsampling from JPEG buffer
static int detect_original_subsampling(const unsigned char *buf, unsigned long bufSize) {
    struct jpeg_decompress_struct cinfo;
    struct jpegarchive_error_mgr jerr;

    // Set up error handling
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpegarchive_error_exit;

    // Establish the setjmp return context
    if (setjmp(jerr.setjmp_buffer)) {
        // If we get here, libjpeg encountered an error
        jpeg_destroy_decompress(&cinfo);
        return SUBSAMPLE_DEFAULT;  // Default on error
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, (unsigned char *)buf, bufSize);

    // Read header to get component info
    jpeg_read_header(&cinfo, TRUE);

    // Detect subsampling from component info
    int subsample = SUBSAMPLE_DEFAULT;

    if (cinfo.num_components == 3 && cinfo.jpeg_color_space == JCS_YCbCr) {
        int h0 = cinfo.comp_info[0].h_samp_factor;
        int v0 = cinfo.comp_info[0].v_samp_factor;
        int h1 = cinfo.comp_info[1].h_samp_factor;
        int v1 = cinfo.comp_info[1].v_samp_factor;
        int h2 = cinfo.comp_info[2].h_samp_factor;
        int v2 = cinfo.comp_info[2].v_samp_factor;

        // Check for common subsampling patterns
        if (h0 == 1 && v0 == 1 && h1 == 1 && v1 == 1 && h2 == 1 && v2 == 1) {
            // 4:4:4 - no subsampling
            subsample = SUBSAMPLE_444;
        } else if (h0 == 2 && v0 == 1 && h1 == 1 && v1 == 1 && h2 == 1 && v2 == 1) {
            // 4:2:2 - horizontal subsampling
            subsample = SUBSAMPLE_422;
        } else if (h0 == 2 && v0 == 2 && h1 == 1 && v1 == 1 && h2 == 1 && v2 == 1) {
            // 4:2:0 - both horizontal and vertical subsampling
            subsample = SUBSAMPLE_DEFAULT;  // This is the default
        } else if (h0 == 4 && v0 == 1 && h1 == 1 && v1 == 1 && h2 == 1 && v2 == 1) {
            // 4:1:1 - convert to 4:2:0 for better compatibility
            subsample = SUBSAMPLE_DEFAULT;  // Use 4:2:0 instead of 4:1:1
        } else {
            // Unknown or uncommon pattern, use default
            subsample = SUBSAMPLE_DEFAULT;
        }
    }

    jpeg_destroy_decompress(&cinfo);
    return subsample;
}

// Helper function to convert quality preset to target value
static float get_target_from_preset(jpegarchive_quality_t preset, jpegarchive_method_t method) {
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

// Binary search for the lowest quality that meets the target metric.
// `original` is RGB and `originalGray` its luma. On success the caller owns *compressedOut;
// on error nothing is left allocated here (the caller still owns original/originalGray).
static jpegarchive_error_code_t search_quality(unsigned char *original, unsigned char *originalGray, int width, int height,
                                               int min, int max, int loops, float target, jpegarchive_method_t method,
                                               int subsample_method, unsigned char **compressedOut,
                                               unsigned long *compressedSizeOut, int *qualityOut, float *metricOut) {
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
        jpegarchive_error_code_t encode_error;
        compressedSize = safeEncodeJpeg(&compressed, original, width, height, JCS_RGB, quality, progressive, optimize, subsample_method, &encode_error);

        if (!compressedSize) {
            if (compressed) free(compressed);
            return encode_error;
        }

        // Decode compressed for comparison
        unsigned char *compressedGray = NULL;
        int decodedWidth, decodedHeight;
        jpegarchive_error_code_t decode_error;
        long compressedGraySize = safeDecodeJpeg(compressed, compressedSize, &compressedGray, &decodedWidth, &decodedHeight, JCS_GRAYSCALE, &decode_error);

        if (!compressedGraySize) {
            free(compressed);
            return decode_error;
        }

        // Calculate metric
        float metric = 0;
        if (method == JPEGARCHIVE_METHOD_SSIM) {
            metric = iqa_ssim(originalGray, compressedGray, width, height, width, 0, 0);
            // Check for SSIM calculation failure (returns INFINITY on error)
            if (metric == INFINITY || metric != metric) {  // NaN check
                free(compressed);
                free(compressedGray);
                return JPEGARCHIVE_MEMORY_ERROR;
            }
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

    *compressedOut = compressed;
    *compressedSizeOut = compressedSize;
    *qualityOut = finalQuality;
    *metricOut = finalMetric;
    return JPEGARCHIVE_OK;
}

// Insert the "already processed" COM marker and the original metadata right after SOI + APP0
static jpegarchive_error_code_t assemble_jpeg(const unsigned char *compressed, unsigned long compressedSize,
                                              const unsigned char *metaBuf, unsigned int metaSize,
                                              unsigned char **out, int64_t *outLength) {
    const char *COMMENT = "Compressed by jpeg-recompress";

    // Check APP0 marker
    if (compressedSize < 6 || compressed[2] != 0xff || compressed[3] != 0xe0) {
        return JPEGARCHIVE_UNKNOWN_ERROR;
    }

    int app0_len = (compressed[4] << 8) + compressed[5];

    // Calculate total size: SOI+APP0 + COM + metadata + image data
    unsigned long totalSize = 4 + app0_len + 4 + strlen(COMMENT) + metaSize + (compressedSize - 4 - app0_len);

    unsigned char *finalJpeg = malloc(totalSize);
    if (!finalJpeg) {
        return JPEGARCHIVE_MEMORY_ERROR;
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

    *out = finalJpeg;
    *outLength = totalSize;
    return JPEGARCHIVE_OK;
}

jpegarchive_recompress_output_t jpegarchive_recompress(jpegarchive_recompress_input_t input) {
    jpegarchive_recompress_output_t output;
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
    
    // Use provided target value if non-zero, otherwise use preset
    float target = (input.target > 0) ? input.target : get_target_from_preset(input.quality, input.method);
    
    // Decode original image
    unsigned char *original = NULL;
    unsigned char *originalGray = NULL;
    int width, height;
    jpegarchive_error_code_t decode_error;
    
    long originalSize = safeDecodeJpeg((unsigned char *)input.jpeg, input.length, &original, &width, &height, JCS_RGB, &decode_error);
    if (!originalSize) {
        output.error_code = decode_error;
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
    int metaResult = getMetadata(input.jpeg, input.length, &metaBuf, &metaSize, NULL);
    if (metaResult < 0) {
        // Metadata allocation failed
        free(original);
        free(originalGray);
        output.error_code = JPEGARCHIVE_MEMORY_ERROR;
        return output;
    }

    // Determine subsampling method to use
    int subsample_method = SUBSAMPLE_DEFAULT;  // Default to 4:2:0

    // Validate input.subsample value and use default if invalid
    if (input.subsample == JPEGARCHIVE_SUBSAMPLE_420) {
        subsample_method = SUBSAMPLE_DEFAULT;  // Force 4:2:0
    } else if (input.subsample == JPEGARCHIVE_SUBSAMPLE_KEEP) {
        // Keep original subsampling
        subsample_method = detect_original_subsampling(input.jpeg, input.length);
    } else if (input.subsample == JPEGARCHIVE_SUBSAMPLE_444) {
        subsample_method = SUBSAMPLE_444;  // Force 4:4:4
    } else {
        // Invalid value, use default
        subsample_method = SUBSAMPLE_DEFAULT;
    }

    // Binary search for optimal quality
    unsigned char *compressed = NULL;
    unsigned long compressedSize = 0;
    int finalQuality = min;
    float finalMetric = 0;
    jpegarchive_error_code_t search_error = search_quality(original, originalGray, width, height, min, max, loops, target, input.method, subsample_method, &compressed, &compressedSize, &finalQuality, &finalMetric);
    free(original);
    free(originalGray);
    if (search_error != JPEGARCHIVE_OK) {
        if (metaBuf) free(metaBuf);
        output.error_code = search_error;
        return output;
    }

    // Check if output is larger than input
    if (compressedSize >= (unsigned long)input.length) {
        free(compressed);
        if (metaBuf) free(metaBuf);
        output.error_code = JPEGARCHIVE_NOT_SUITABLE;
        return output;
    }

    // Build complete JPEG with metadata and comment
    output.error_code = assemble_jpeg(compressed, compressedSize, metaBuf, metaSize, &output.jpeg, &output.length);
    free(compressed);
    if (metaBuf) free(metaBuf);
    if (output.error_code != JPEGARCHIVE_OK) {
        return output;
    }
    output.quality = finalQuality;
    output.metric = finalMetric;

    return output;
}

jpegarchive_recompress_output_t jpegarchive_recompress_rgb(jpegarchive_recompress_rgb_input_t input) {
    jpegarchive_recompress_output_t output;
    memset(&output, 0, sizeof(output));

    // Validate input
    if (!input.rgb || input.width <= 0 || input.height <= 0) {
        output.error_code = JPEGARCHIVE_INVALID_INPUT;
        return output;
    }

    int min = (input.min > 0) ? input.min : 40;
    int max = (input.max > 0) ? input.max : 95;
    int loops = (input.loops > 0) ? input.loops : 6;
    if (min > max) {
        output.error_code = JPEGARCHIVE_INVALID_INPUT;
        return output;
    }

    float target = (input.target > 0) ? input.target : get_target_from_preset(input.quality, input.method);

    int subsample_method = input.subsample;
    if (subsample_method != SUBSAMPLE_444 && subsample_method != SUBSAMPLE_422) {
        subsample_method = SUBSAMPLE_DEFAULT;
    }

    // The pixels are the reference: there is no previous JPEG generation to compare against,
    // and no "already processed" mark or input size to check
    unsigned char *originalGray = NULL;
    long originalGraySize = grayscale((unsigned char *)input.rgb, &originalGray, input.width, input.height);
    if (!originalGraySize) {
        output.error_code = JPEGARCHIVE_MEMORY_ERROR;
        return output;
    }

    unsigned char *compressed = NULL;
    unsigned long compressedSize = 0;
    int finalQuality = min;
    float finalMetric = 0;
    jpegarchive_error_code_t search_error = search_quality((unsigned char *)input.rgb, originalGray, input.width, input.height, min, max, loops, target, input.method, subsample_method, &compressed, &compressedSize, &finalQuality, &finalMetric);
    free(originalGray);
    if (search_error != JPEGARCHIVE_OK) {
        output.error_code = search_error;
        return output;
    }

    output.error_code = assemble_jpeg(compressed, compressedSize, NULL, 0, &output.jpeg, &output.length);
    free(compressed);
    if (output.error_code != JPEGARCHIVE_OK) {
        return output;
    }
    output.quality = finalQuality;
    output.metric = finalMetric;

    return output;
}

void jpegarchive_free_recompress_output(jpegarchive_recompress_output_t *output) {
    if (output && output->jpeg) {
        free(output->jpeg);
        output->jpeg = NULL;
        output->length = 0;
    }
}

jpegarchive_compare_output_t jpegarchive_compare(jpegarchive_compare_input_t input) {
    jpegarchive_compare_output_t output;
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
    jpegarchive_error_code_t decode_error1;
    long size1 = safeDecodeJpeg((unsigned char *)input.jpeg1, input.length1, &image1, &width1, &height1, JCS_GRAYSCALE, &decode_error1);
    
    if (!size1) {
        output.error_code = decode_error1;
        return output;
    }
    
    // Decode second image
    unsigned char *image2 = NULL;
    int width2, height2;
    jpegarchive_error_code_t decode_error2;
    long size2 = safeDecodeJpeg((unsigned char *)input.jpeg2, input.length2, &image2, &width2, &height2, JCS_GRAYSCALE, &decode_error2);
    
    if (!size2) {
        free(image1);
        output.error_code = decode_error2;
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
        // Check for SSIM calculation failure (returns INFINITY on error)
        if (metric == INFINITY || metric != metric) {  // NaN check
            free(image1);
            free(image2);
            output.error_code = JPEGARCHIVE_MEMORY_ERROR;
            return output;
        }
    }

    free(image1);
    free(image2);

    output.error_code = JPEGARCHIVE_OK;
    output.metric = metric;
    
    return output;
}

void jpegarchive_free_compare_output(jpegarchive_compare_output_t *output) {
    // Nothing to free for compare output
    (void)output;
}

jpegarchive_encode_rgb_output_t jpegarchive_encode_rgb(jpegarchive_encode_rgb_input_t input) {
    jpegarchive_encode_rgb_output_t output;
    memset(&output, 0, sizeof(output));

    if (!input.rgb || input.width <= 0 || input.height <= 0) {
        output.error_code = JPEGARCHIVE_INVALID_INPUT;
        return output;
    }

    int quality = (input.quality > 0 && input.quality <= 100) ? input.quality : 95;
    int subsample_method = input.subsample;
    int progressive = input.progressive;

    unsigned char *jpeg = NULL;
    jpegarchive_error_code_t encode_error;
    unsigned long jpegSize = safeEncodeJpeg(
        &jpeg,
        (unsigned char *)input.rgb,
        input.width,
        input.height,
        JCS_RGB,
        quality,
        progressive,
        1,  // optimize
        subsample_method,
        &encode_error
    );

    if (!jpegSize || !jpeg) {
        output.error_code = encode_error ? encode_error : JPEGARCHIVE_UNKNOWN_ERROR;
        return output;
    }

    output.error_code = JPEGARCHIVE_OK;
    output.jpeg = jpeg;
    output.length = (int64_t)jpegSize;
    return output;
}

void jpegarchive_free_encode_rgb_output(jpegarchive_encode_rgb_output_t *output) {
    if (output && output->jpeg) {
        free(output->jpeg);
        output->jpeg = NULL;
        output->length = 0;
    }
}

int jpegarchive_detect_subsampling(const unsigned char *jpeg, int64_t length) {
    if (!jpeg || length <= 0) {
        return SUBSAMPLE_DEFAULT;
    }
    return detect_original_subsampling(jpeg, (unsigned long)length);
}

// Standard JPEG luminance quantization table (quality 50)
static const unsigned int std_luminance_quant_tbl[DCTSIZE2] = {
    16,  11,  10,  16,  24,  40,  51,  61,
    12,  12,  14,  19,  26,  58,  60,  55,
    14,  13,  16,  24,  40,  57,  69,  56,
    14,  17,  22,  29,  51,  87,  80,  62,
    18,  22,  37,  56,  68, 109, 103,  77,
    24,  35,  55,  64,  81, 104, 113,  92,
    49,  64,  78,  87, 103, 121, 120, 101,
    72,  92,  95,  98, 112, 100, 103,  99
};

// Standard JPEG chrominance quantization table (quality 50)
static const unsigned int std_chrominance_quant_tbl[DCTSIZE2] = {
    17,  18,  24,  47,  99,  99,  99,  99,
    18,  21,  26,  66,  99,  99,  99,  99,
    24,  26,  56,  99,  99,  99,  99,  99,
    47,  66,  99,  99,  99,  99,  99,  99,
    99,  99,  99,  99,  99,  99,  99,  99,
    99,  99,  99,  99,  99,  99,  99,  99,
    99,  99,  99,  99,  99,  99,  99,  99,
    99,  99,  99,  99,  99,  99,  99,  99
};

int jpegarchive_detect_quality(const unsigned char *jpeg, int64_t length) {
    if (!jpeg || length <= 0) {
        return 95;
    }

    struct jpeg_decompress_struct cinfo;
    struct jpegarchive_error_mgr jerr;

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpegarchive_error_exit;

    if (setjmp(jerr.setjmp_buffer)) {
        jpeg_destroy_decompress(&cinfo);
        return 95;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, (unsigned char *)jpeg, (unsigned long)length);
    jpeg_read_header(&cinfo, TRUE);

    // Get luminance quantization table
    JQUANT_TBL *qtbl = cinfo.quant_tbl_ptrs[0];
    if (!qtbl) {
        jpeg_destroy_decompress(&cinfo);
        return 95;
    }

    // Estimate quality by computing average scaling factor relative to standard table
    // IJG formula: scale = actual * 100 / standard
    //   quality >= 50: scale = 200 - 2*quality  => quality = (200 - scale) / 2
    //   quality <  50: scale = 5000 / quality    => quality = 5000 / scale
    double scale_sum = 0;
    int count = 0;
    for (int i = 0; i < DCTSIZE2; i++) {
        if (std_luminance_quant_tbl[i] > 0) {
            scale_sum += (double)qtbl->quantval[i] * 100.0 / (double)std_luminance_quant_tbl[i];
            count++;
        }
    }

    jpeg_destroy_decompress(&cinfo);

    if (count == 0) return 95;

    double avg_scale = scale_sum / count;
    int quality;
    if (avg_scale <= 100.0) {
        quality = (int)((200.0 - avg_scale) / 2.0 + 0.5);
    } else {
        quality = (int)(5000.0 / avg_scale + 0.5);
    }

    if (quality < 1) quality = 1;
    if (quality > 100) quality = 100;
    return quality;
}
