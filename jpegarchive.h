#ifndef JPEG_ARCHIVE_H
#define JPEG_ARCHIVE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Error codes
typedef enum {
    JPEGARCHIVE_OK = 0,
    JPEGARCHIVE_INVALID_INPUT,
    JPEGARCHIVE_NOT_JPEG,
    JPEGARCHIVE_UNSUPPORTED,
    JPEGARCHIVE_NOT_SUITABLE,
    JPEGARCHIVE_MEMORY_ERROR,
    JPEGARCHIVE_UNKNOWN_ERROR
} jpegarchive_error_code_t;

// Method enum
typedef enum {
    JPEGARCHIVE_METHOD_SSIM = 0
} jpegarchive_method_t;

// Quality preset
typedef enum {
    JPEGARCHIVE_QUALITY_LOW = 0,
    JPEGARCHIVE_QUALITY_MEDIUM,
    JPEGARCHIVE_QUALITY_HIGH,
    JPEGARCHIVE_QUALITY_VERYHIGH
} jpegarchive_quality_t;

// Subsample method
typedef enum {
    JPEGARCHIVE_SUBSAMPLE_420 = 0,  // Force 4:2:0 subsampling
    JPEGARCHIVE_SUBSAMPLE_KEEP = 1, // Keep original image's subsampling
    JPEGARCHIVE_SUBSAMPLE_444 = 2   // Force 4:4:4 (no subsampling)
} jpegarchive_subsample_t;

// Input structure for jpegarchive_recompress
typedef struct {
    const unsigned char *jpeg;
    int64_t length;
    int min;
    int max;
    int loops;
    jpegarchive_quality_t quality;
    jpegarchive_method_t method;
    float target;  // Target metric value (0 = use quality preset)
    jpegarchive_subsample_t subsample;  // Subsampling method
} jpegarchive_recompress_input_t;

// Output structure for jpegarchive_recompress
typedef struct {
    jpegarchive_error_code_t error_code;
    unsigned char *jpeg;
    int64_t length;
    int quality;
    double metric;
} jpegarchive_recompress_output_t;

// Input structure for jpegarchive_compare
typedef struct {
    const unsigned char *jpeg1;
    const unsigned char *jpeg2;
    int64_t length1;
    int64_t length2;
    jpegarchive_method_t method;
} jpegarchive_compare_input_t;

// Output structure for jpegarchive_compare
typedef struct {
    jpegarchive_error_code_t error_code;
    double metric;
} jpegarchive_compare_output_t;

// Input structure for jpegarchive_encode_rgb
typedef struct {
    const unsigned char *rgb;     // RGB pixel data (3 bytes per pixel, row-major)
    int width;
    int height;
    int quality;                  // JPEG quality (1-100)
    int subsample;                // SUBSAMPLE_DEFAULT(4:2:0), SUBSAMPLE_444, SUBSAMPLE_422
    int progressive;              // 1 = progressive, 0 = baseline
} jpegarchive_encode_rgb_input_t;

// Output structure for jpegarchive_encode_rgb
typedef struct {
    jpegarchive_error_code_t error_code;
    unsigned char *jpeg;
    int64_t length;
} jpegarchive_encode_rgb_output_t;

// Function declarations
jpegarchive_recompress_output_t jpegarchive_recompress(jpegarchive_recompress_input_t input);
void jpegarchive_free_recompress_output(jpegarchive_recompress_output_t *output);

jpegarchive_compare_output_t jpegarchive_compare(jpegarchive_compare_input_t input);
void jpegarchive_free_compare_output(jpegarchive_compare_output_t *output);

jpegarchive_encode_rgb_output_t jpegarchive_encode_rgb(jpegarchive_encode_rgb_input_t input);
void jpegarchive_free_encode_rgb_output(jpegarchive_encode_rgb_output_t *output);

// Detect subsampling from JPEG data (returns SUBSAMPLE_DEFAULT/444/422)
int jpegarchive_detect_subsampling(const unsigned char *jpeg, int64_t length);

// Estimate JPEG quality (1-100) from quantization tables
int jpegarchive_detect_quality(const unsigned char *jpeg, int64_t length);

#ifdef __cplusplus
}
#endif

#endif // JPEG_ARCHIVE_H