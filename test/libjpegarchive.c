#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../jpegarchive.h"

// Function to read file into buffer
static long read_file(const char *filename, unsigned char **buffer) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        return 0;
    }
    
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
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

// Function to run CLI command and capture output
static int run_command_and_get_output(const char *command, char *output, int max_output) {
    FILE *pipe = popen(command, "r");
    if (!pipe) return -1;
    
    int total = 0;
    while (fgets(output + total, max_output - total, pipe) != NULL) {
        total = strlen(output);
        if (total >= max_output - 1) break;
    }
    
    int ret = pclose(pipe);
    return ret;
}

// Parse SSIM value from jpeg-recompress output
static double parse_ssim_from_recompress(const char *output) {
    const char *ssim_str = strstr(output, "ssim at q=");
    if (!ssim_str) {
        ssim_str = strstr(output, "Final optimized ssim at q=");
    }
    if (ssim_str) {
        const char *colon = strchr(ssim_str, ':');
        if (colon) {
            double value;
            if (sscanf(colon + 1, "%lf", &value) == 1) {
                return value;
            }
        }
    }
    return -1;
}

// Parse quality from jpeg-recompress output
static int parse_quality_from_recompress(const char *output) {
    const char *q_str = strstr(output, "Final optimized ssim at q=");
    if (q_str) {
        int quality;
        if (sscanf(q_str, "Final optimized ssim at q=%d", &quality) == 1) {
            return quality;
        }
    }
    return -1;
}

// Parse SSIM value from jpeg-compare output
static double parse_ssim_from_compare(const char *output) {
    double value;
    // Try parsing with "SSIM: " prefix first
    if (sscanf(output, "SSIM: %lf", &value) == 1) {
        return value;
    }
    // Fallback to just number
    if (sscanf(output, "%lf", &value) == 1) {
        return value;
    }
    return -1;
}

// Test jpeg_recompress function
static int test_recompress(const char *test_file) {
    printf("Testing jpeg_recompress with %s...\n", test_file);
    
    // Read test file
    unsigned char *input_buffer;
    long input_size = read_file(test_file, &input_buffer);
    if (!input_size) {
        printf("  ERROR: Failed to read test file\n");
        return 1;
    }
    
    // Run library version
    JpegRecompressInput lib_input = {
        .jpeg = input_buffer,
        .length = input_size,
        .min = 40,
        .max = 95,
        .loops = 6,
        .quality = JPEGARCHIVE_QUALITY_MEDIUM,
        .method = JPEGARCHIVE_METHOD_SSIM
    };
    
    JpegRecompressOutput lib_output = jpeg_recompress(lib_input);
    
    if (lib_output.error_code == JPEGARCHIVE_NOT_SUITABLE) {
        printf("  SKIPPED: File not suitable for recompression (e.g., already processed or would be larger)\n");
        free(input_buffer);
        return 0;  // Not an error, just skip this file
    }
    
    if (lib_output.error_code != JPEGARCHIVE_OK) {
        printf("  ERROR: Library returned error code %d\n", lib_output.error_code);
        free(input_buffer);
        return 1;
    }
    
    // Create temp output file
    char temp_output[256];
    snprintf(temp_output, sizeof(temp_output), "/tmp/test_output_%d.jpg", getpid());
    
    // Run CLI version
    char cli_command[512];
    snprintf(cli_command, sizeof(cli_command), 
             "../jpeg-recompress -q medium -n 40 -x 95 -l 6 %s %s 2>&1",
             test_file, temp_output);
    
    char cli_output[4096];
    int ret = run_command_and_get_output(cli_command, cli_output, sizeof(cli_output));
    
    if (ret != 0) {
        printf("  ERROR: CLI command failed\n");
        free(input_buffer);
        free_jpeg_recompress_output(&lib_output);
        return 1;
    }
    
    // Parse CLI output
    double cli_ssim = parse_ssim_from_recompress(cli_output);
    int cli_quality = parse_quality_from_recompress(cli_output);
    
    // Read CLI output file size
    struct stat st;
    if (stat(temp_output, &st) != 0) {
        printf("  ERROR: Failed to stat CLI output file\n");
        free(input_buffer);
        free_jpeg_recompress_output(&lib_output);
        return 1;
    }
    long cli_size = st.st_size;
    
    // Both CLI and library include the same comment now, so no adjustment needed
    long cli_size_adjusted = cli_size;
    
    // Compare results
    printf("  Library: quality=%d, ssim=%f, size=%lld\n", 
           lib_output.quality, lib_output.metric, (long long)lib_output.length);
    printf("  CLI:     quality=%d, ssim=%f, size=%ld\n", 
           cli_quality, cli_ssim, cli_size);
    
    // Check if results are within tolerance
    // Allow 5% for SSIM and quality, 1% for size (should be nearly identical now)
    double ssim_diff = fabs(lib_output.metric - cli_ssim) / cli_ssim * 100;
    double quality_diff = abs(lib_output.quality - cli_quality) / (double)cli_quality * 100;
    double size_diff = labs(lib_output.length - cli_size_adjusted) / (double)cli_size_adjusted * 100;
    
    int passed = 1;
    if (ssim_diff > 5.0) {
        printf("  WARNING: SSIM difference %.2f%% exceeds 5%%\n", ssim_diff);
        passed = 0;
    }
    if (quality_diff > 5.0) {
        printf("  WARNING: Quality difference %.2f%% exceeds 5%%\n", quality_diff);
        passed = 0;
    }
    if (size_diff > 1.0) {
        printf("  WARNING: Size difference %.2f%% exceeds 1%%\n", size_diff);
        passed = 0;
    }
    
    // Cleanup
    unlink(temp_output);
    free(input_buffer);
    free_jpeg_recompress_output(&lib_output);
    
    return passed ? 0 : 1;
}

// Test jpeg_compare function
static int test_compare(const char *file1, const char *file2) {
    printf("Testing jpeg_compare with %s and %s...\n", file1, file2);
    
    // Read test files
    unsigned char *buffer1, *buffer2;
    long size1 = read_file(file1, &buffer1);
    long size2 = read_file(file2, &buffer2);
    
    if (!size1 || !size2) {
        printf("  ERROR: Failed to read test files\n");
        if (size1) free(buffer1);
        if (size2) free(buffer2);
        return 1;
    }
    
    // Run library version
    JpegCompareInput lib_input = {
        .jpeg1 = buffer1,
        .jpeg2 = buffer2,
        .length1 = size1,
        .length2 = size2,
        .method = JPEGARCHIVE_METHOD_SSIM
    };
    
    JpegCompareOutput lib_output = jpeg_compare(lib_input);
    
    if (lib_output.error_code != JPEGARCHIVE_OK) {
        printf("  ERROR: Library returned error code %d\n", lib_output.error_code);
        free(buffer1);
        free(buffer2);
        return 1;
    }
    
    // Run CLI version with SSIM method
    char cli_command[512];
    snprintf(cli_command, sizeof(cli_command), 
             "../jpeg-compare -m ssim %s %s 2>&1",
             file1, file2);
    
    char cli_output[1024];
    int ret = run_command_and_get_output(cli_command, cli_output, sizeof(cli_output));
    
    if (ret != 0) {
        printf("  ERROR: CLI command failed\n");
        free(buffer1);
        free(buffer2);
        return 1;
    }
    
    // Parse CLI output
    double cli_ssim = parse_ssim_from_compare(cli_output);
    
    // Compare results
    printf("  Library: ssim=%f\n", lib_output.metric);
    printf("  CLI:     ssim=%f\n", cli_ssim);
    
    // Check if results match (very small tolerance for floating point)
    double diff = fabs(lib_output.metric - cli_ssim);
    int passed = (diff < 0.0001);
    
    if (!passed) {
        printf("  ERROR: SSIM values differ by %f\n", diff);
    }
    
    // Cleanup
    free(buffer1);
    free(buffer2);
    free_jpeg_compare_output(&lib_output);
    
    return passed ? 0 : 1;
}

int main(int argc, char **argv) {
    printf("Testing libjpegarchive...\n\n");
    
    int total_errors = 0;
    
    // Test jpeg_recompress with all test files
    DIR *dir = opendir("test-files");
    if (!dir) {
        printf("ERROR: Cannot open test-files directory\n");
        return 1;
    }
    
    struct dirent *entry;
    char test_files[10][256];
    int num_files = 0;
    
    while ((entry = readdir(dir)) != NULL && num_files < 10) {
        if (strstr(entry->d_name, ".jpg")) {
            snprintf(test_files[num_files], 256, "test-files/%s", entry->d_name);
            num_files++;
        }
    }
    closedir(dir);
    
    // Test recompress for each file
    printf("=== Testing jpeg_recompress ===\n");
    for (int i = 0; i < num_files; i++) {
        int err = test_recompress(test_files[i]);
        total_errors += err;
    }
    
    // Test compare between original and compressed versions
    printf("\n=== Testing jpeg_compare ===\n");
    for (int i = 0; i < num_files && i < 3; i++) {  // Test first 3 files
        // First compress the file
        unsigned char *input_buffer;
        long input_size = read_file(test_files[i], &input_buffer);
        if (input_size) {
            JpegRecompressInput input = {
                .jpeg = input_buffer,
                .length = input_size,
                .min = 40,
                .max = 95,
                .loops = 6,
                .quality = JPEGARCHIVE_QUALITY_MEDIUM,
                .method = JPEGARCHIVE_METHOD_SSIM
            };
            
            JpegRecompressOutput output = jpeg_recompress(input);
            if (output.error_code == JPEGARCHIVE_OK) {
                // Write compressed to temp file
                char temp_file[256];
                snprintf(temp_file, sizeof(temp_file), "/tmp/compressed_%d.jpg", i);
                FILE *f = fopen(temp_file, "wb");
                if (f) {
                    fwrite(output.jpeg, 1, output.length, f);
                    fclose(f);
                    
                    // Compare original with compressed
                    int err = test_compare(test_files[i], temp_file);
                    total_errors += err;
                    
                    unlink(temp_file);
                }
                free_jpeg_recompress_output(&output);
            }
            free(input_buffer);
        }
    }
    
    printf("\n=== Test Summary ===\n");
    if (total_errors == 0) {
        printf("All tests PASSED!\n");
    } else {
        printf("Tests completed with %d errors\n", total_errors);
    }
    
    return total_errors > 0 ? 1 : 0;
}