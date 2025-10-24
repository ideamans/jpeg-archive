#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jpeglib.h>

#ifdef _WIN32
#include <io.h>
#include <direct.h>
#define access _access
#endif

#include "../jpegarchive.h"

// Function to detect subsampling from JPEG file
const char* detect_subsampling_from_file(const char* filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        return "ERROR: Cannot open file";
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Read file into buffer
    unsigned char *buffer = malloc(file_size);
    fread(buffer, 1, file_size, file);
    fclose(file);

    // Setup JPEG decompression
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, buffer, file_size);
    jpeg_read_header(&cinfo, TRUE);

    // Detect subsampling
    const char* result = "UNKNOWN";
    if (cinfo.num_components == 3 && cinfo.jpeg_color_space == JCS_YCbCr) {
        int h0 = cinfo.comp_info[0].h_samp_factor;
        int v0 = cinfo.comp_info[0].v_samp_factor;
        int h1 = cinfo.comp_info[1].h_samp_factor;
        int v1 = cinfo.comp_info[1].v_samp_factor;
        int h2 = cinfo.comp_info[2].h_samp_factor;
        int v2 = cinfo.comp_info[2].v_samp_factor;

        if (h0 == 1 && v0 == 1 && h1 == 1 && v1 == 1 && h2 == 1 && v2 == 1) {
            result = "4:4:4";
        } else if (h0 == 2 && v0 == 1 && h1 == 1 && v1 == 1 && h2 == 1 && v2 == 1) {
            result = "4:2:2";
        } else if (h0 == 2 && v0 == 2 && h1 == 1 && v1 == 1 && h2 == 1 && v2 == 1) {
            result = "4:2:0";
        } else if (h0 == 4 && v0 == 1 && h1 == 1 && v1 == 1 && h2 == 1 && v2 == 1) {
            result = "4:1:1";
        } else {
            static char custom[64];
            snprintf(custom, sizeof(custom), "%d:%d:%d x %d:%d:%d",
                     h0, h1, h2, v0, v1, v2);
            result = custom;
        }
    }

    jpeg_destroy_decompress(&cinfo);
    free(buffer);

    return result;
}

// Test function for a single file
int test_subsampling_preservation(const char* input_file, const char* expected_subsampling) {
    printf("\n=== Testing %s (expected: %s) ===\n", input_file, expected_subsampling);

    // Read input file
    FILE *file = fopen(input_file, "rb");
    if (!file) {
        printf("ERROR: Cannot open input file\n");
        return 1;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    unsigned char *input_buffer = malloc(file_size);
    fread(input_buffer, 1, file_size, file);
    fclose(file);

    // Detect original subsampling
    const char* original_subsampling = detect_subsampling_from_file(input_file);
    printf("Original subsampling: %s\n", original_subsampling);

    // Test with JPEGARCHIVE_SUBSAMPLE_KEEP
    jpegarchive_recompress_input_t input = {0};
    input.jpeg = input_buffer;
    input.length = file_size;
    input.min = 40;
    input.max = 95;
    input.loops = 6;
    input.quality = JPEGARCHIVE_QUALITY_MEDIUM;
    input.method = JPEGARCHIVE_METHOD_SSIM;
    input.target = 0.9999;
    input.subsample = JPEGARCHIVE_SUBSAMPLE_KEEP;  // This should preserve original

    jpegarchive_recompress_output_t output = jpegarchive_recompress(input);

    if (output.error_code != JPEGARCHIVE_OK) {
        if (output.error_code == JPEGARCHIVE_NOT_SUITABLE) {
            printf("File not suitable for recompression (may already be optimized)\n");
            free(input_buffer);
            return 0;  // Not a failure, just skip
        }
        printf("ERROR: Recompression failed with code %d\n", output.error_code);
        free(input_buffer);
        return 1;
    }

    printf("Recompression successful: quality=%d, metric=%f, size=%lld\n",
           output.quality, output.metric, (long long)output.length);

    // Save output to temp file and check subsampling
#ifdef _WIN32
    const char* temp_file = "./test_output.jpg";
#else
    const char* temp_file = "/tmp/test_output.jpg";
#endif
    FILE *out = fopen(temp_file, "wb");
    fwrite(output.jpeg, 1, output.length, out);
    fclose(out);

    const char* output_subsampling = detect_subsampling_from_file(temp_file);
    printf("Output subsampling: %s\n", output_subsampling);

    // Check if subsampling was preserved
    int success = strcmp(original_subsampling, output_subsampling) == 0;
    if (success) {
        printf("✓ Subsampling preserved correctly\n");
    } else {
        printf("✗ FAILED: Subsampling changed from %s to %s\n",
               original_subsampling, output_subsampling);
    }

    // Cleanup
    jpegarchive_free_recompress_output(&output);
    free(input_buffer);

    return !success;
}

int main() {
    printf("Testing JPEGARCHIVE_SUBSAMPLE_KEEP functionality\n");
    printf("================================================\n");

    int failures = 0;

    // Test each subsampling pattern
    failures += test_subsampling_preservation("test-files/subsampling/test_444.jpg", "4:4:4");
    failures += test_subsampling_preservation("test-files/subsampling/test_422.jpg", "4:2:2");
    failures += test_subsampling_preservation("test-files/subsampling/test_420.jpg", "4:2:0");

    // Special case: 4:1:1 should be converted to 4:2:0 for better compatibility
    printf("\n=== Testing 4:1:1 to 4:2:0 conversion ===\n");
    printf("Note: 4:1:1 is automatically converted to 4:2:0 for better compatibility\n");

    // Read and test 4:1:1 file
    const char* input_411 = "test-files/subsampling/test_411.jpg";
    FILE *file = fopen(input_411, "rb");
    if (!file) {
        printf("ERROR: Cannot open test_411.jpg\n");
        failures++;
    } else {
        fseek(file, 0, SEEK_END);
        long file_size = ftell(file);
        fseek(file, 0, SEEK_SET);

        unsigned char *input_buffer = malloc(file_size);
        fread(input_buffer, 1, file_size, file);
        fclose(file);

        const char* original_subsampling = detect_subsampling_from_file(input_411);
        printf("Original subsampling: %s\n", original_subsampling);

        jpegarchive_recompress_input_t input = {0};
        input.jpeg = input_buffer;
        input.length = file_size;
        input.min = 40;
        input.max = 95;
        input.loops = 6;
        input.quality = JPEGARCHIVE_QUALITY_MEDIUM;
        input.method = JPEGARCHIVE_METHOD_SSIM;
        input.target = 0.9999;
        input.subsample = JPEGARCHIVE_SUBSAMPLE_KEEP;

        jpegarchive_recompress_output_t output = jpegarchive_recompress(input);

        if (output.error_code == JPEGARCHIVE_OK || output.error_code == JPEGARCHIVE_NOT_SUITABLE) {
            if (output.error_code == JPEGARCHIVE_OK) {
#ifdef _WIN32
                const char* temp_file = "./test_411_output.jpg";
#else
                const char* temp_file = "/tmp/test_411_output.jpg";
#endif
                FILE *out = fopen(temp_file, "wb");
                fwrite(output.jpeg, 1, output.length, out);
                fclose(out);

                const char* output_subsampling = detect_subsampling_from_file(temp_file);
                printf("Output subsampling: %s (converted from 4:1:1)\n", output_subsampling);

                if (strcmp(output_subsampling, "4:2:0") == 0) {
                    printf("✓ 4:1:1 correctly converted to 4:2:0\n");
                } else {
                    printf("✗ FAILED: Expected 4:2:0 but got %s\n", output_subsampling);
                    failures++;
                }

                jpegarchive_free_recompress_output(&output);
            } else {
                printf("File not suitable for recompression (may already be optimized)\n");
            }
        } else {
            printf("ERROR: Recompression failed with code %d\n", output.error_code);
            failures++;
        }

        free(input_buffer);
    }

    printf("\n================================================\n");
    if (failures == 0) {
        printf("✓ All tests passed!\n");
    } else {
        printf("✗ %d tests failed\n", failures);
    }

    return failures;
}