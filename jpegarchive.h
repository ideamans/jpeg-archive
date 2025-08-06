#ifndef JPEGARCHIVE_H
#define JPEGARCHIVE_H

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
} JpegArchiveErrorCode;

// Method enum
typedef enum {
    JPEGARCHIVE_METHOD_SSIM = 0
} JpegArchiveMethod;

// Quality preset
typedef enum {
    JPEGARCHIVE_QUALITY_LOW = 0,
    JPEGARCHIVE_QUALITY_MEDIUM,
    JPEGARCHIVE_QUALITY_HIGH,
    JPEGARCHIVE_QUALITY_VERYHIGH
} JpegArchiveQuality;

// Input structure for jpeg_recompress
typedef struct {
    const unsigned char *jpeg;
    int64_t length;
    int min;
    int max;
    int loops;
    JpegArchiveQuality quality;
    JpegArchiveMethod method;
} JpegRecompressInput;

// Output structure for jpeg_recompress
typedef struct {
    JpegArchiveErrorCode error_code;
    unsigned char *jpeg;
    int64_t length;
    int quality;
    double metric;
} JpegRecompressOutput;

// Input structure for jpeg_compare
typedef struct {
    const unsigned char *jpeg1;
    const unsigned char *jpeg2;
    int64_t length1;
    int64_t length2;
    JpegArchiveMethod method;
} JpegCompareInput;

// Output structure for jpeg_compare
typedef struct {
    JpegArchiveErrorCode error_code;
    double metric;
} JpegCompareOutput;

// Function declarations
JpegRecompressOutput jpeg_recompress(JpegRecompressInput input);
void free_jpeg_recompress_output(JpegRecompressOutput *output);

JpegCompareOutput jpeg_compare(JpegCompareInput input);
void free_jpeg_compare_output(JpegCompareOutput *output);

#ifdef __cplusplus
}
#endif

#endif // JPEGARCHIVE_H