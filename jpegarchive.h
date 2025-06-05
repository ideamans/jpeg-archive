#ifndef JPEG_RECOMPRESS_LIB_H
#define JPEG_RECOMPRESS_LIB_H

#ifdef __cplusplus
extern "C" {
#endif

// Quality presets
#define JPEG_RECOMPRESS_QUALITY_LOW      0
#define JPEG_RECOMPRESS_QUALITY_MEDIUM   1
#define JPEG_RECOMPRESS_QUALITY_HIGH     2
#define JPEG_RECOMPRESS_QUALITY_VERYHIGH 3

// Result structure
typedef struct {
    int exit_code;      // 0 for success, non-zero for error
    int quality;        // Final JPEG quality used (0-100)
    double ssim;        // Final SSIM value
    char *error;        // Error message (NULL if no error)
} jpeg_recompress_result;

// Main library function
void jpeg_recompress(const char *input_path, const char *output_path, int quality_preset, jpeg_recompress_result *result);

// Helper function to free error message
void jpeg_recompress_free_result(jpeg_recompress_result *result);

#ifdef __cplusplus
}
#endif

#endif // JPEG_RECOMPRESS_LIB_H