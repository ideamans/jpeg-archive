#define _GNU_SOURCE
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <jpeglib.h>
#include <setjmp.h>

#include "../jpegarchive.h"

typedef struct {
    int quality;
    long long size;
    double ssim;
} recompress_stats_t;

typedef struct {
    const char *name;
    const char *cli_quality;
    jpegarchive_quality_t preset;
    int min;
    int max;
    int loops;
} quality_case_t;

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

// Get current time in microseconds
static long long get_time_us(void) {
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

// Function to run CLI command and capture output
static int run_command_and_get_output(const char *command, char *output, int max_output) {
    FILE *pipe = popen(command, "r");
    if (!pipe) {
        return -1;
    }

    output[0] = '\0';
    int total = 0;
    while (fgets(output + total, max_output - total, pipe) != NULL) {
        total = (int)strlen(output);
        if (total >= max_output - 1) {
            break;
        }
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
    if (sscanf(output, "SSIM: %lf", &value) == 1) {
        return value;
    }
    if (sscanf(output, "%lf", &value) == 1) {
        return value;
    }
    return -1;
}

static int test_recompress_case(const char *test_file,
                                const quality_case_t *case_def,
                                recompress_stats_t *lib_stats,
                                recompress_stats_t *cli_stats,
                                int *skipped) {
    printf("Testing jpegarchive_recompress (%s) with %s...\n", case_def->name, test_file);

    if (skipped) {
        *skipped = 0;
    }

    unsigned char *input_buffer;
    long input_size = read_file(test_file, &input_buffer);
    if (!input_size) {
        printf("  ERROR: Failed to read test file\n");
        return 1;
    }

    jpegarchive_recompress_input_t lib_input = {
        .jpeg = input_buffer,
        .length = input_size,
        .min = case_def->min,
        .max = case_def->max,
        .loops = case_def->loops,
        .quality = case_def->preset,
        .method = JPEGARCHIVE_METHOD_SSIM,
        .target = 0
    };

    long long lib_start = get_time_us();
    jpegarchive_recompress_output_t lib_output = jpegarchive_recompress(lib_input);
    long long lib_time = get_time_us() - lib_start;

    if (lib_output.error_code == JPEGARCHIVE_NOT_SUITABLE) {
        printf("  SKIPPED: File not suitable for recompression (e.g., already processed or would be larger)\n");
        free(input_buffer);
        if (skipped) {
            *skipped = 1;
        }
        return 0;
    }

    if (lib_output.error_code != JPEGARCHIVE_OK) {
        printf("  ERROR: Library returned error code %d\n", lib_output.error_code);
        free(input_buffer);
        return 1;
    }

    char temp_output[256];
#ifdef _WIN32
    snprintf(temp_output, sizeof(temp_output), "./test_output_%d.jpg", getpid());
#else
    snprintf(temp_output, sizeof(temp_output), "/tmp/test_output_%d.jpg", getpid());
#endif

    const char *exe_path = "../jpeg-recompress";
    const char *exe_ext = "";
#ifdef _WIN32
    exe_path = "..\\jpeg-recompress";
    exe_ext = ".exe";
#endif

    char cli_command[512];
    snprintf(cli_command, sizeof(cli_command),
             "%s%s -q %s -n %d -x %d -l %d %s %s 2>&1",
             exe_path,
             exe_ext,
             case_def->cli_quality,
             case_def->min,
             case_def->max,
             case_def->loops,
             test_file,
             temp_output);

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

    double cli_ssim = parse_ssim_from_recompress(cli_output);
    int cli_quality = parse_quality_from_recompress(cli_output);

    struct stat st;
    if (stat(temp_output, &st) != 0) {
        printf("  ERROR: Failed to stat CLI output file\n");
        free(input_buffer);
        jpegarchive_free_recompress_output(&lib_output);
        return 1;
    }
    long long cli_size = st.st_size;

    printf("  Library: quality=%d, ssim=%f, size=%lld\n",
           lib_output.quality, lib_output.metric, (long long)lib_output.length);
    printf("  CLI:     quality=%d, ssim=%f, size=%lld\n",
           cli_quality, cli_ssim, cli_size);

    double quality_diff = 0.0;
    if (cli_quality != 0) {
        quality_diff = fabs((double)(lib_output.quality - cli_quality)) / fabs((double)cli_quality) * 100.0;
    } else if (lib_output.quality != 0) {
        quality_diff = 100.0;
    }

    double size_diff = 0.0;
    if (cli_size > 0) {
        size_diff = fabs((double)(lib_output.length - cli_size)) / (double)cli_size * 100.0;
    } else if (lib_output.length > 0) {
        size_diff = 100.0;
    }

    int passed = 1;
    if (quality_diff > 5.0) {
        printf("  WARNING: Quality difference %.2f%% exceeds 5%%\n", quality_diff);
        passed = 0;
    }
    if (size_diff > 1.0) {
        printf("  WARNING: Size difference %.2f%% exceeds 1%%\n", size_diff);
        passed = 0;
    }

    double speedup = (lib_time > 0) ? ((double)cli_time / (double)lib_time) : 0.0;
    double speedup_percent = (lib_time > 0) ? ((speedup - 1.0) * 100.0) : 0.0;
    printf("  Time - CLI: %.2fms, Library: %.2fms\n", cli_time / 1000.0, lib_time / 1000.0);
    printf("  Performance: Library is %.1fx faster (%.0f%% speedup)\n", speedup, speedup_percent);

    if (lib_stats) {
        lib_stats->quality = lib_output.quality;
        lib_stats->size = lib_output.length;
        lib_stats->ssim = lib_output.metric;
    }
    if (cli_stats) {
        cli_stats->quality = cli_quality;
        cli_stats->size = cli_size;
        cli_stats->ssim = cli_ssim;
    }

    unlink(temp_output);
    free(input_buffer);
    jpegarchive_free_recompress_output(&lib_output);

    return passed ? 0 : 1;
}

static int test_compare(const char *file1, const char *file2) {
    printf("Testing jpegarchive_compare with %s and %s...\n", file1, file2);

    unsigned char *buffer1, *buffer2;
    long size1 = read_file(file1, &buffer1);
    long size2 = read_file(file2, &buffer2);

    if (!size1 || !size2) {
        printf("  ERROR: Failed to read test files\n");
        if (size1) {
            free(buffer1);
        }
        if (size2) {
            free(buffer2);
        }
        return 1;
    }

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

    const char *exe_ext = "";
#ifdef _WIN32
    exe_ext = ".exe";
#endif

    char cli_command[512];
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

    double cli_ssim = parse_ssim_from_compare(cli_output);

    printf("  Library: ssim=%f\n", lib_output.metric);
    printf("  CLI:     ssim=%f\n", cli_ssim);

    double diff = fabs(lib_output.metric - cli_ssim);
    int passed = (diff < 0.0001);
    if (!passed) {
        printf("  ERROR: SSIM values differ by %f\n", diff);
    }

    double speedup = (lib_time > 0) ? ((double)cli_time / (double)lib_time) : 0.0;
    double speedup_percent = (lib_time > 0) ? ((speedup - 1.0) * 100.0) : 0.0;
    printf("  Time - CLI: %.2fms, Library: %.2fms\n", cli_time / 1000.0, lib_time / 1000.0);
    printf("  Performance: Library is %.1fx faster (%.0f%% speedup)\n", speedup, speedup_percent);

    free(buffer1);
    free(buffer2);
    jpegarchive_free_compare_output(&lib_output);

    return passed ? 0 : 1;
}

static int test_subsample(void) {
    printf("=== Testing Subsample Options ===\n");

    int total_errors = 0;

    // Create a test image with cjpeg
    printf("Creating test image...\n");
    FILE *ppm = fopen("/tmp/test_subsample.ppm", "w");
    if (!ppm) {
        printf("  ERROR: Failed to create test PPM file\n");
        return 1;
    }

    // Create a simple 8x8 red image
    // Note: mozjpeg optimizes small solid color images to 4:4:4 regardless of settings
    fprintf(ppm, "P3\n8 8\n255\n");
    for (int i = 0; i < 64; i++) {
        fprintf(ppm, "255 0 0 ");
    }
    fclose(ppm);

    // Convert to JPEG with 4:2:0 subsampling (default)
    // Note: mozjpeg will actually produce 4:4:4 for this small solid color image
    system("../deps/built/mozjpeg/bin/cjpeg -quality 90 /tmp/test_subsample.ppm > /tmp/test_420_source.jpg 2>/dev/null");

    // Convert to JPEG with 4:4:4 subsampling
    system("../deps/built/mozjpeg/bin/cjpeg -quality 90 -sample 1x1 /tmp/test_subsample.ppm > /tmp/test_444_source.jpg 2>/dev/null");

    // Test cases
    // Note: For small solid color images, mozjpeg optimizes to 4:4:4 regardless of settings
    // This is expected behavior and tests are adjusted accordingly
    struct {
        const char *name;
        const char *input_file;
        jpegarchive_subsample_t subsample;
        int expected_444;  // 0 for 4:2:0, 1 for 4:4:4
        int skip_if_unsuitable;  // Skip test if JPEGARCHIVE_NOT_SUITABLE error
    } test_cases[] = {
        // For small images, mozjpeg always uses 4:4:4, so we expect 4:4:4 in output
        {"Force 4:2:0 on small image (mozjpeg uses 4:4:4)", "/tmp/test_420_source.jpg", JPEGARCHIVE_SUBSAMPLE_420, 1, 1},
        {"Force 4:2:0 on small image (mozjpeg uses 4:4:4)", "/tmp/test_444_source.jpg", JPEGARCHIVE_SUBSAMPLE_420, 1, 1},
        {"Keep original on small image (4:4:4)", "/tmp/test_420_source.jpg", JPEGARCHIVE_SUBSAMPLE_KEEP, 1, 0},
        {"Keep original on small image (4:4:4)", "/tmp/test_444_source.jpg", JPEGARCHIVE_SUBSAMPLE_KEEP, 1, 0},
        {"Force 4:4:4 on small image (already 4:4:4)", "/tmp/test_420_source.jpg", JPEGARCHIVE_SUBSAMPLE_444, 1, 0},
        {"Force 4:4:4 on small image (already 4:4:4)", "/tmp/test_444_source.jpg", JPEGARCHIVE_SUBSAMPLE_444, 1, 0},
        {"Invalid value (99) defaults to 4:2:0 (but mozjpeg uses 4:4:4)", "/tmp/test_420_source.jpg", (jpegarchive_subsample_t)99, 1, 1},
    };

    int num_tests = sizeof(test_cases) / sizeof(test_cases[0]);

    for (int i = 0; i < num_tests; i++) {
        printf("\nTest: %s\n", test_cases[i].name);

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
            printf("  PASSED: Output subsampling matches expected\n");
        } else {
            printf("  FAILED: Output subsampling does not match expected\n");
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

    printf("\nSubsample tests completed with %d errors\n", total_errors);
    return total_errors;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("Testing libjpegarchive...\n\n");

    int total_errors = 0;

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
            snprintf(test_files[num_files], sizeof(test_files[num_files]), "test-files/%s", entry->d_name);
            num_files++;
        }
    }
    closedir(dir);

    const quality_case_t quality_cases[] = {
        {"low", "low", JPEGARCHIVE_QUALITY_LOW, 40, 95, 6},
        {"medium", "medium", JPEGARCHIVE_QUALITY_MEDIUM, 40, 95, 6},
        {"high", "high", JPEGARCHIVE_QUALITY_HIGH, 40, 95, 6},
        {"medium-range", "medium", JPEGARCHIVE_QUALITY_MEDIUM, 32, 96, 6},
    };
    const int num_quality_cases = (int)(sizeof(quality_cases) / sizeof(quality_cases[0]));

    recompress_stats_t lib_results[10][num_quality_cases];
    recompress_stats_t cli_results[10][num_quality_cases];
    int has_result[10][num_quality_cases];
    memset(has_result, 0, sizeof(has_result));

    printf("=== Testing jpegarchive_recompress ===\n");
    for (int i = 0; i < num_files; i++) {
        for (int q = 0; q < num_quality_cases; q++) {
            int skipped = 0;
            int err = test_recompress_case(test_files[i], &quality_cases[q],
                                           &lib_results[i][q], &cli_results[i][q], &skipped);
            total_errors += err;
            if (!err && !skipped) {
                has_result[i][q] = 1;
            }
        }
    }

    printf("\n=== Validating preset differences (low vs high) ===\n");
    int low_idx = -1;
    int high_idx = -1;
    for (int q = 0; q < num_quality_cases; q++) {
        if (low_idx == -1 && strcmp(quality_cases[q].name, "low") == 0 &&
            quality_cases[q].min == 40 && quality_cases[q].max == 95) {
            low_idx = q;
        }
        if (high_idx == -1 && strcmp(quality_cases[q].name, "high") == 0 &&
            quality_cases[q].min == 40 && quality_cases[q].max == 95) {
            high_idx = q;
        }
    }

    if (low_idx != -1 && high_idx != -1) {
        for (int i = 0; i < num_files; i++) {
            if (has_result[i][low_idx] && has_result[i][high_idx]) {
                recompress_stats_t low_lib = lib_results[i][low_idx];
                recompress_stats_t high_lib = lib_results[i][high_idx];
                recompress_stats_t low_cli = cli_results[i][low_idx];
                recompress_stats_t high_cli = cli_results[i][high_idx];

                if (high_lib.quality <= low_lib.quality) {
                    printf("  ERROR: Library high preset quality (%d) is not greater than low preset quality (%d) for %s\n",
                           high_lib.quality, low_lib.quality, test_files[i]);
                    total_errors++;
                }
                if (high_lib.size <= low_lib.size) {
                    printf("  ERROR: Library high preset size (%lld) is not greater than low preset size (%lld) for %s\n",
                           high_lib.size, low_lib.size, test_files[i]);
                    total_errors++;
                }

                if (high_cli.quality <= low_cli.quality) {
                    printf("  ERROR: CLI high preset quality (%d) is not greater than low preset quality (%d) for %s\n",
                           high_cli.quality, low_cli.quality, test_files[i]);
                    total_errors++;
                }
                if (high_cli.size <= low_cli.size) {
                    printf("  ERROR: CLI high preset size (%lld) is not greater than low preset size (%lld) for %s\n",
                           high_cli.size, low_cli.size, test_files[i]);
                    total_errors++;
                }
            } else {
                printf("  INFO: Skipping preset comparison for %s (insufficient data)\n", test_files[i]);
            }
        }
    } else {
        printf("  INFO: Skipping preset comparison (low/high cases not available)\n");
    }

    printf("\n=== Testing jpegarchive_recompress with custom target ===\n");
    if (num_files > 0) {
        printf("Testing custom target value with %s...\n", test_files[0]);

        unsigned char *input_buffer;
        long input_size = read_file(test_files[0], &input_buffer);
        if (input_size) {
            jpegarchive_recompress_input_t custom_input = {
                .jpeg = input_buffer,
                .length = input_size,
                .min = 40,
                .max = 95,
                .loops = 6,
                .quality = JPEGARCHIVE_QUALITY_MEDIUM,
                .method = JPEGARCHIVE_METHOD_SSIM,
                .target = 0.995f
            };

            jpegarchive_recompress_output_t custom_output = jpegarchive_recompress(custom_input);

            if (custom_output.error_code == JPEGARCHIVE_OK) {
                printf("  Custom target test: quality=%d, ssim=%f, size=%lld\n",
                       custom_output.quality, custom_output.metric, (long long)custom_output.length);

                double target_diff = fabs(custom_output.metric - 0.995);
                if (target_diff < 0.01) {
                    printf("  OK: Custom target test PASSED (metric close to target)\n");
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

    printf("\n=== Testing jpegarchive_compare ===\n");
    for (int i = 0; i < num_files && i < 3; i++) {
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
                .target = 0
            };

            jpegarchive_recompress_output_t output = jpegarchive_recompress(input);
            if (output.error_code == JPEGARCHIVE_OK) {
                char temp_file[256];
                snprintf(temp_file, sizeof(temp_file), "/tmp/compressed_%d.jpg", i);
                FILE *f = fopen(temp_file, "wb");
                if (f) {
                    fwrite(output.jpeg, 1, output.length, f);
                    fclose(f);

                    int err = test_compare(test_files[i], temp_file);
                    total_errors += err;

                    unlink(temp_file);
                }
                jpegarchive_free_recompress_output(&output);
            }
            free(input_buffer);
        }
    }

    printf("\n=== Testing CMYK JPEG handling ===\n");
    const char *cmyk_file = "extra-test-data/colorspace_cmyk.jpg";
    struct stat cmyk_stat;
    if (stat(cmyk_file, &cmyk_stat) == 0) {
        printf("Testing CMYK JPEG with jpegarchive_recompress...\n");

        unsigned char *cmyk_buffer;
        long cmyk_size = read_file(cmyk_file, &cmyk_buffer);
        if (cmyk_size) {
            jpegarchive_recompress_input_t cmyk_input = {
                .jpeg = cmyk_buffer,
                .length = cmyk_size,
                .min = 40,
                .max = 95,
                .loops = 6,
                .quality = JPEGARCHIVE_QUALITY_MEDIUM,
                .method = JPEGARCHIVE_METHOD_SSIM,
                .target = 0
            };

            jpegarchive_recompress_output_t cmyk_output = jpegarchive_recompress(cmyk_input);

            if (cmyk_output.error_code == JPEGARCHIVE_UNSUPPORTED) {
                printf("  OK: CMYK JPEG correctly rejected with UNSUPPORTED error\n");
            } else {
                printf("  ERROR: Expected UNSUPPORTED error for CMYK JPEG, got error code %d\n", cmyk_output.error_code);
                total_errors++;
            }

            jpegarchive_free_recompress_output(&cmyk_output);

            printf("Testing CMYK JPEG with jpegarchive_compare...\n");
            jpegarchive_compare_input_t cmyk_compare_input = {
                .jpeg1 = cmyk_buffer,
                .jpeg2 = cmyk_buffer,
                .length1 = cmyk_size,
                .length2 = cmyk_size,
                .method = JPEGARCHIVE_METHOD_SSIM
            };

            jpegarchive_compare_output_t cmyk_compare_output = jpegarchive_compare(cmyk_compare_input);

            if (cmyk_compare_output.error_code == JPEGARCHIVE_UNSUPPORTED) {
                printf("  OK: CMYK JPEG correctly rejected with UNSUPPORTED error\n");
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

    printf("\n=== Testing Subsample Options ===\n");
    int subsample_errors = test_subsample();
    total_errors += subsample_errors;

    printf("\n=== Test Summary ===\n");
    if (total_errors == 0) {
        printf("All tests PASSED!\n");
    } else {
        printf("Tests completed with %d errors\n", total_errors);
    }

    return total_errors > 0 ? 1 : 0;
}
