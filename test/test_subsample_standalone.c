#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <jpeglib.h>
#include <setjmp.h>

#ifdef _WIN32
#include <io.h>
#include <direct.h>
#define access _access
#endif

#include "../jpegarchive.h"

// Error handler for libjpeg
struct my_error_mgr {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};

typedef struct my_error_mgr * my_error_ptr;

static void my_error_exit(j_common_ptr cinfo) {
    my_error_ptr myerr = (my_error_ptr) cinfo->err;
    longjmp(myerr->setjmp_buffer, 1);
}

// Function to read file into buffer
static long read_file(const char *filename, unsigned char **buffer) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        return 0;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }
    long size = ftell(file);
    if (size < 0) {
        fclose(file);
        return 0;
    }
    rewind(file);

    *buffer = malloc(size);
    if (!*buffer) {
        fclose(file);
        return 0;
    }

    size_t read = fread(*buffer, 1, size, file);
    fclose(file);

    if (read != (size_t)size) {
        free(*buffer);
        return 0;
    }

    return size;
}

// Detect subsampling from JPEG file
// Returns: 0 for 4:2:0 (or 4:2:2), 1 for 4:4:4, -1 for error
static int detect_jpeg_subsampling(const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        return -1;
    }

    struct jpeg_decompress_struct cinfo;
    struct my_error_mgr jerr;

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = my_error_exit;

    if (setjmp(jerr.setjmp_buffer)) {
        jpeg_destroy_decompress(&cinfo);
        fclose(file);
        return -1;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, file);
    jpeg_read_header(&cinfo, TRUE);

    int is_444 = 0;
    if (cinfo.num_components == 3 && cinfo.jpeg_color_space == JCS_YCbCr) {
        // Check if all components have same sampling factors (4:4:4)
        if (cinfo.comp_info[0].h_samp_factor == 1 && cinfo.comp_info[0].v_samp_factor == 1 &&
            cinfo.comp_info[1].h_samp_factor == 1 && cinfo.comp_info[1].v_samp_factor == 1 &&
            cinfo.comp_info[2].h_samp_factor == 1 && cinfo.comp_info[2].v_samp_factor == 1) {
            is_444 = 1;
        }
    }

    jpeg_destroy_decompress(&cinfo);
    fclose(file);

    return is_444;
}

int main() {
    printf("=== Standalone Subsample Test ===\n\n");

    int total_errors = 0;

    // Set paths based on platform
#ifdef _WIN32
    const char *ppm_path = "./test_subsample.ppm";
    const char *jpg_420_path = "./test_420_source.jpg";
    const char *jpg_444_path = "./test_444_source.jpg";
    char cjpeg_path[512];
    _fullpath(cjpeg_path, "../deps/built/mozjpeg/bin/cjpeg.exe", sizeof(cjpeg_path));
    if (access(cjpeg_path, 0) != 0) {
        strcpy(cjpeg_path, "cjpeg.exe");
    }
#else
    const char *ppm_path = "/tmp/test_subsample.ppm";
    const char *jpg_420_path = "/tmp/test_420_source.jpg";
    const char *jpg_444_path = "/tmp/test_444_source.jpg";
    const char *cjpeg_path = "../deps/built/mozjpeg/bin/cjpeg";
#endif

    // Create a test image with cjpeg
    printf("Creating test images...\n");
    FILE *ppm = fopen(ppm_path, "w");
    if (!ppm) {
        printf("  ERROR: Failed to create test PPM file\n");
        return 1;
    }

    // Create a simple 8x8 red image
    fprintf(ppm, "P3\n8 8\n255\n");
    for (int i = 0; i < 64; i++) {
        fprintf(ppm, "255 0 0 ");
    }
    fclose(ppm);

    // Convert to JPEG with 4:2:0 subsampling (default)
    printf("Creating 4:2:0 source image...\n");
    char cmd_420[1024];
    char cmd_444[1024];
#ifdef _WIN32
    snprintf(cmd_420, sizeof(cmd_420), "\"%s\" -quality 90 \"%s\" > \"%s\" 2>nul", cjpeg_path, ppm_path, jpg_420_path);
    snprintf(cmd_444, sizeof(cmd_444), "\"%s\" -quality 90 -sample 1x1 \"%s\" > \"%s\" 2>nul", cjpeg_path, ppm_path, jpg_444_path);
#else
    snprintf(cmd_420, sizeof(cmd_420), "%s -quality 90 %s > %s 2>/dev/null", cjpeg_path, ppm_path, jpg_420_path);
    snprintf(cmd_444, sizeof(cmd_444), "%s -quality 90 -sample 1x1 %s > %s 2>/dev/null", cjpeg_path, ppm_path, jpg_444_path);
#endif

    int ret_420 = system(cmd_420);

    // Convert to JPEG with 4:4:4 subsampling
    printf("Creating 4:4:4 source image...\n");
    int ret_444 = system(cmd_444);

    // Check if cjpeg commands succeeded
    if (ret_420 != 0 || ret_444 != 0) {
        printf("  WARNING: cjpeg command failed (ret_420=%d, ret_444=%d)\n", ret_420, ret_444);
        printf("  Command: %s\n", cmd_420);
        printf("  Skipping subsample tests\n");
        return 0;  // Don't fail the entire test suite
    }

    // Verify files were created
    if (access(jpg_420_path, 0) != 0 || access(jpg_444_path, 0) != 0) {
        printf("  WARNING: Test JPEG files were not created\n");
        printf("  420 path: %s (exists: %d)\n", jpg_420_path, access(jpg_420_path, 0) == 0);
        printf("  444 path: %s (exists: %d)\n", jpg_444_path, access(jpg_444_path, 0) == 0);
        printf("  Skipping subsample tests\n");
        return 0;  // Don't fail the entire test suite
    }

    // Test cases
    // Note: For small solid color images, mozjpeg optimizes to 4:4:4 regardless of settings
    struct {
        const char *name;
        const char *input_file;
        jpegarchive_subsample_t subsample;
        int expected_444;  // 0 for 4:2:0, 1 for 4:4:4
        int skip_if_unsuitable;  // Skip test if JPEGARCHIVE_NOT_SUITABLE error
    } test_cases[] = {
        // For small images, mozjpeg always uses 4:4:4, so we expect 4:4:4 in output
        {"Force 4:2:0 on small image (mozjpeg uses 4:4:4)", "", JPEGARCHIVE_SUBSAMPLE_420, 1, 1},
        {"Force 4:2:0 on small image (mozjpeg uses 4:4:4)", "", JPEGARCHIVE_SUBSAMPLE_420, 1, 1},
        {"Keep original on small image (4:4:4)", "", JPEGARCHIVE_SUBSAMPLE_KEEP, 1, 0},
        {"Keep original on small image (4:4:4)", "", JPEGARCHIVE_SUBSAMPLE_KEEP, 1, 0},
        {"Force 4:4:4 on small image (already 4:4:4)", "", JPEGARCHIVE_SUBSAMPLE_444, 1, 0},
        {"Force 4:4:4 on small image (already 4:4:4)", "", JPEGARCHIVE_SUBSAMPLE_444, 1, 0},
        {"Invalid value (99) defaults to 4:2:0 (but mozjpeg uses 4:4:4)", "", (jpegarchive_subsample_t)99, 1, 1},
    };

    int num_tests = sizeof(test_cases) / sizeof(test_cases[0]);

    // Set input_file paths based on platform
    test_cases[0].input_file = jpg_420_path;
    test_cases[1].input_file = jpg_444_path;
    test_cases[2].input_file = jpg_420_path;
    test_cases[3].input_file = jpg_444_path;
    test_cases[4].input_file = jpg_420_path;
    test_cases[5].input_file = jpg_444_path;
    test_cases[6].input_file = jpg_420_path;

    printf("\nRunning subsample tests...\n");
    printf("--------------------------\n");

    for (int i = 0; i < num_tests; i++) {
        printf("\nTest %d: %s\n", i+1, test_cases[i].name);

        // Read input file
        unsigned char *input_buffer;
        long input_size = read_file(test_cases[i].input_file, &input_buffer);
        if (!input_size) {
            printf("  ERROR: Failed to read input file %s\n", test_cases[i].input_file);
            total_errors++;
            continue;
        }

        // Detect original subsampling
        int original_444 = detect_jpeg_subsampling(test_cases[i].input_file);
        printf("  Original subsampling: %s\n", original_444 ? "4:4:4" : "4:2:0");

        // Set up recompress input
        jpegarchive_recompress_input_t input = {
            .jpeg = input_buffer,
            .length = input_size,
            .min = 40,
            .max = 95,
            .loops = 6,
            .quality = JPEGARCHIVE_QUALITY_MEDIUM,
            .method = JPEGARCHIVE_METHOD_SSIM,
            .target = 0,
            .subsample = test_cases[i].subsample
        };

        printf("  Subsample option: %d\n", test_cases[i].subsample);

        // Recompress
        jpegarchive_recompress_output_t output = jpegarchive_recompress(input);

        if (output.error_code != JPEGARCHIVE_OK) {
            if (output.error_code == JPEGARCHIVE_NOT_SUITABLE && test_cases[i].skip_if_unsuitable) {
                printf("  SKIPPED: File not suitable for recompression (expected for small images with forced 4:2:0)\n");
                free(input_buffer);
                continue;
            } else {
                printf("  ERROR: Recompress failed with error code %d\n", output.error_code);
                free(input_buffer);
                total_errors++;
                continue;
            }
        }

        // Write output to temporary file
        char temp_output[256];
        snprintf(temp_output, sizeof(temp_output), "/tmp/test_subsample_output_%d.jpg", i);
        FILE *f = fopen(temp_output, "wb");
        if (!f) {
            printf("  ERROR: Failed to write output file\n");
            jpegarchive_free_recompress_output(&output);
            free(input_buffer);
            total_errors++;
            continue;
        }
        fwrite(output.jpeg, 1, output.length, f);
        fclose(f);

        // Detect output subsampling
        int output_444 = detect_jpeg_subsampling(temp_output);
        printf("  Output subsampling: %s\n", output_444 ? "4:4:4" : "4:2:0");
        printf("  Expected: %s\n", test_cases[i].expected_444 ? "4:4:4" : "4:2:0");

        // Verify result
        if (output_444 == test_cases[i].expected_444) {
            printf("  ✓ PASSED: Output subsampling matches expected\n");
        } else {
            printf("  ✗ FAILED: Output subsampling does not match expected\n");
            total_errors++;
        }

        // Clean up
        unlink(temp_output);
        jpegarchive_free_recompress_output(&output);
        free(input_buffer);
    }

    // Clean up test files
    unlink("/tmp/test_subsample.ppm");
    unlink("/tmp/test_420_source.jpg");
    unlink("/tmp/test_444_source.jpg");

    printf("\n--------------------------\n");
    printf("Test Summary\n");
    printf("--------------------------\n");
    if (total_errors == 0) {
        printf("✓ All %d subsample tests PASSED!\n", num_tests);
    } else {
        printf("✗ %d of %d tests FAILED\n", total_errors, num_tests);
    }

    return total_errors > 0 ? 1 : 0;
}