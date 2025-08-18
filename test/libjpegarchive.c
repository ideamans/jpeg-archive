#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include "../jpegarchive.h"

// Get current time in microseconds
static long long get_time_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000000LL + (long long)tv.tv_usec;
}

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
    
    // Initialize output buffer
    output[0] = '\0';
    
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

// Test jpegarchive_recompress function
static int test_recompress(const char *test_file) {
    printf("Testing jpegarchive_recompress with %s...\n", test_file);
    
    // Read test file
    unsigned char *input_buffer;
    long input_size = read_file(test_file, &input_buffer);
    if (!input_size) {
        printf("  ERROR: Failed to read test file\n");
        return 1;
    }
    
    // Run library version
    jpegarchive_recompress_input_t lib_input = {
        .jpeg = input_buffer,
        .length = input_size,
        .min = 40,
        .max = 95,
        .loops = 6,
        .quality = JPEGARCHIVE_QUALITY_MEDIUM,
        .method = JPEGARCHIVE_METHOD_SSIM,
        .target = 0  // Use quality preset
    };
    
    long long lib_start = get_time_us();
    jpegarchive_recompress_output_t lib_output = jpegarchive_recompress(lib_input);
    long long lib_time = get_time_us() - lib_start;
    
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
    #ifdef _WIN32
        // Windows: use current directory (MSYS2 prefers forward slashes)
        snprintf(temp_output, sizeof(temp_output), "./test_output_%d.jpg", getpid());
    #else
        snprintf(temp_output, sizeof(temp_output), "/tmp/test_output_%d.jpg", getpid());
    #endif
    
    // Run CLI version
    char cli_command[512];
    const char *exe_path = "../jpeg-recompress";
    const char *exe_ext = "";
    
    // Check if Windows executable exists
    #ifdef _WIN32
        exe_ext = ".exe";
    #endif
    
    snprintf(cli_command, sizeof(cli_command), 
             "%s%s -q medium -n 40 -x 95 -l 6 %s %s 2>&1",
             exe_path, exe_ext, test_file, temp_output);
    
    long long cli_start = get_time_us();
    char cli_output[4096];
    int ret = run_command_and_get_output(cli_command, cli_output, sizeof(cli_output));
    long long cli_time = get_time_us() - cli_start;
    
    if (ret != 0) {
        printf("  ERROR: CLI command failed with return code %d\n", ret);
        printf("  Command: %s\n", cli_command);
        printf("  Output: %s\n", cli_output);
        free(input_buffer);
        jpegarchive_free_recompress_output(&lib_output);
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
        jpegarchive_free_recompress_output(&lib_output);
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
    
    // Performance comparison
    double speedup = (double)cli_time / (double)lib_time;
    double speedup_percent = (speedup - 1.0) * 100.0;
    
    printf("  Time - CLI: %.2fms, Library: %.2fms\n", cli_time / 1000.0, lib_time / 1000.0);
    printf("  Performance: Library is %.1fx faster (%.0f%% speedup)\n", speedup, speedup_percent);
    
    // Cleanup
    unlink(temp_output);
    free(input_buffer);
    jpegarchive_free_recompress_output(&lib_output);
    
    return passed ? 0 : 1;
}

// Test jpegarchive_compare function
static int test_compare(const char *file1, const char *file2) {
    printf("Testing jpegarchive_compare with %s and %s...\n", file1, file2);
    
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
    jpegarchive_compare_input_t lib_input = {
        .jpeg1 = buffer1,
        .jpeg2 = buffer2,
        .length1 = size1,
        .length2 = size2,
        .method = JPEGARCHIVE_METHOD_SSIM
    };
    
    long long lib_start = get_time_us();
    jpegarchive_compare_output_t lib_output = jpegarchive_compare(lib_input);
    long long lib_time = get_time_us() - lib_start;
    
    if (lib_output.error_code != JPEGARCHIVE_OK) {
        printf("  ERROR: Library returned error code %d\n", lib_output.error_code);
        free(buffer1);
        free(buffer2);
        return 1;
    }
    
    // Run CLI version with SSIM method
    char cli_command[512];
    const char *exe_ext = "";
    #ifdef _WIN32
        exe_ext = ".exe";
    #endif
    
    snprintf(cli_command, sizeof(cli_command), 
             "../jpeg-compare%s -m ssim %s %s 2>&1",
             exe_ext, file1, file2);
    
    long long cli_start = get_time_us();
    char cli_output[1024];
    int ret = run_command_and_get_output(cli_command, cli_output, sizeof(cli_output));
    long long cli_time = get_time_us() - cli_start;
    
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
    
    // Performance comparison
    double speedup = (double)cli_time / (double)lib_time;
    double speedup_percent = (speedup - 1.0) * 100.0;
    
    printf("  Time - CLI: %.2fms, Library: %.2fms\n", cli_time / 1000.0, lib_time / 1000.0);
    printf("  Performance: Library is %.1fx faster (%.0f%% speedup)\n", speedup, speedup_percent);
    
    // Cleanup
    free(buffer1);
    free(buffer2);
    jpegarchive_free_compare_output(&lib_output);
    
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
    printf("=== Testing jpegarchive_recompress ===\n");
    for (int i = 0; i < num_files; i++) {
        int err = test_recompress(test_files[i]);
        total_errors += err;
    }
    
    // Test recompress with custom target value
    printf("\n=== Testing jpegarchive_recompress with custom target ===\n");
    if (num_files > 0) {
        printf("Testing custom target value with %s...\n", test_files[0]);
        
        // Read test file
        unsigned char *input_buffer;
        long input_size = read_file(test_files[0], &input_buffer);
        if (input_size) {
            // Test with custom target value (0.995)
            jpegarchive_recompress_input_t custom_input = {
                .jpeg = input_buffer,
                .length = input_size,
                .min = 40,
                .max = 95,
                .loops = 6,
                .quality = JPEGARCHIVE_QUALITY_MEDIUM,  // This will be ignored
                .method = JPEGARCHIVE_METHOD_SSIM,
                .target = 0.995  // Custom target value
            };
            
            jpegarchive_recompress_output_t custom_output = jpegarchive_recompress(custom_input);
            
            if (custom_output.error_code == JPEGARCHIVE_OK) {
                printf("  Custom target test: quality=%d, ssim=%f, size=%lld\n", 
                       custom_output.quality, custom_output.metric, (long long)custom_output.length);
                
                // The metric should be close to the target value (0.995)
                double target_diff = fabs(custom_output.metric - 0.995);
                if (target_diff < 0.01) {  // Allow some tolerance
                    printf("  ✓ Custom target test PASSED (metric close to target)\n");
                } else {
                    printf("  WARNING: Metric %f differs from target 0.995 by %f\n", 
                           custom_output.metric, target_diff);
                }
                
                jpegarchive_free_recompress_output(&custom_output);
            } else if (custom_output.error_code == JPEGARCHIVE_NOT_SUITABLE) {
                printf("  INFO: File not suitable for custom target test\n");
            } else {
                printf("  ERROR: Custom target test failed with error code %d\n", custom_output.error_code);
                total_errors++;
            }
            
            free(input_buffer);
        } else {
            printf("  ERROR: Failed to read test file for custom target test\n");
            total_errors++;
        }
    }
    
    // Test compare between original and compressed versions
    printf("\n=== Testing jpegarchive_compare ===\n");
    for (int i = 0; i < num_files && i < 3; i++) {  // Test first 3 files
        // First compress the file
        unsigned char *input_buffer;
        long input_size = read_file(test_files[i], &input_buffer);
        if (input_size) {
            jpegarchive_recompress_input_t input = {
                .jpeg = input_buffer,
                .length = input_size,
                .min = 40,
                .max = 95,
                .loops = 6,
                .quality = JPEGARCHIVE_QUALITY_MEDIUM,
                .method = JPEGARCHIVE_METHOD_SSIM,
                .target = 0  // Use quality preset
            };
            
            jpegarchive_recompress_output_t output = jpegarchive_recompress(input);
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
                jpegarchive_free_recompress_output(&output);
            }
            free(input_buffer);
        }
    }
    
    // Test CMYK JPEG handling
    printf("\n=== Testing CMYK JPEG handling ===\n");
    
    // Test if extra-test-data/colorspace_cmyk.jpg exists
    const char *cmyk_file = "extra-test-data/colorspace_cmyk.jpg";
    struct stat cmyk_stat;
    if (stat(cmyk_file, &cmyk_stat) == 0) {
        printf("Testing CMYK JPEG with jpegarchive_recompress...\n");
        
        // Read CMYK file
        unsigned char *cmyk_buffer;
        long cmyk_size = read_file(cmyk_file, &cmyk_buffer);
        if (cmyk_size) {
            // Test recompress - should return UNSUPPORTED error
            jpegarchive_recompress_input_t cmyk_input = {
                .jpeg = cmyk_buffer,
                .length = cmyk_size,
                .min = 40,
                .max = 95,
                .loops = 6,
                .quality = JPEGARCHIVE_QUALITY_MEDIUM,
                .method = JPEGARCHIVE_METHOD_SSIM,
                .target = 0  // Use quality preset
            };
            
            jpegarchive_recompress_output_t cmyk_output = jpegarchive_recompress(cmyk_input);
            
            if (cmyk_output.error_code == JPEGARCHIVE_UNSUPPORTED) {
                printf("  ✓ CMYK JPEG correctly rejected with UNSUPPORTED error\n");
            } else {
                printf("  ERROR: Expected UNSUPPORTED error for CMYK JPEG, got error code %d\n", cmyk_output.error_code);
                total_errors++;
            }
            
            jpegarchive_free_recompress_output(&cmyk_output);
            
            // Test compare - should also return UNSUPPORTED error
            printf("Testing CMYK JPEG with jpegarchive_compare...\n");
            
            // Compare CMYK with itself
            jpegarchive_compare_input_t cmyk_compare_input = {
                .jpeg1 = cmyk_buffer,
                .jpeg2 = cmyk_buffer,
                .length1 = cmyk_size,
                .length2 = cmyk_size,
                .method = JPEGARCHIVE_METHOD_SSIM
            };
            
            jpegarchive_compare_output_t cmyk_compare_output = jpegarchive_compare(cmyk_compare_input);
            
            if (cmyk_compare_output.error_code == JPEGARCHIVE_UNSUPPORTED) {
                printf("  ✓ CMYK JPEG correctly rejected with UNSUPPORTED error\n");
            } else {
                printf("  ERROR: Expected UNSUPPORTED error for CMYK JPEG, got error code %d\n", cmyk_compare_output.error_code);
                total_errors++;
            }
            
            jpegarchive_free_compare_output(&cmyk_compare_output);
            
            free(cmyk_buffer);
        } else {
            printf("  WARNING: Failed to read CMYK test file\n");
        }
    } else {
        printf("  INFO: CMYK test file not found at %s, skipping CMYK tests\n", cmyk_file);
    }
    
    printf("\n=== Test Summary ===\n");
    if (total_errors == 0) {
        printf("All tests PASSED!\n");
    } else {
        printf("Tests completed with %d errors\n", total_errors);
    }
    
    return total_errors > 0 ? 1 : 0;
}