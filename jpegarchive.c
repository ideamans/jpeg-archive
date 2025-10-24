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
            free(original);
            free(originalGray);
            if (compressed) free(compressed);
            if (metaBuf) free(metaBuf);
            output.error_code = encode_error;
            return output;
        }
        
        // Decode compressed for comparison
        unsigned char *compressedGray = NULL;
        jpegarchive_error_code_t decode_error2;
        long compressedGraySize = safeDecodeJpeg(compressed, compressedSize, &compressedGray, &width, &height, JCS_GRAYSCALE, &decode_error2);
        
        if (!compressedGraySize) {
            free(original);
            free(originalGray);
            free(compressed);
            if (metaBuf) free(metaBuf);
            output.error_code = decode_error2;
            return output;
        }
        
        // Calculate metric
        float metric = 0;
        if (input.method == JPEGARCHIVE_METHOD_SSIM) {
            metric = iqa_ssim(originalGray, compressedGray, width, height, width, 0, 0);
            // Check for SSIM calculation failure (returns INFINITY on error)
            if (metric == INFINITY || metric != metric) {  // NaN check
                free(compressed);
                free(compressedGray);
                free(original);
                free(originalGray);
                if (metaBuf) free(metaBuf);
                output.error_code = JPEGARCHIVE_MEMORY_ERROR;
                return output;
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
