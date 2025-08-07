/*
 * Performance test for fast_ssim with larger images
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../include/fast_ssim.h"
#include "../include/iqa.h"

/* Create a test image with random noise */
static unsigned char* create_test_image(int width, int height, unsigned int seed)
{
    unsigned char *img = (unsigned char*)malloc(width * height);
    int i;
    
    if (!img) return NULL;
    
    srand(seed);
    for (i = 0; i < width * height; i++) {
        img[i] = rand() % 256;
    }
    
    return img;
}

/* Add noise to an image */
static unsigned char* add_noise(const unsigned char *src, int size, int noise_level)
{
    unsigned char *dst = (unsigned char*)malloc(size);
    int i;
    
    if (!dst) return NULL;
    
    for (i = 0; i < size; i++) {
        int val = src[i] + (rand() % (2 * noise_level + 1)) - noise_level;
        dst[i] = (val < 0) ? 0 : (val > 255) ? 255 : val;
    }
    
    return dst;
}

int main(int argc, char *argv[])
{
    int width = 512;
    int height = 512;
    int num_comparisons = 20;
    unsigned char *ref_image;
    unsigned char **test_images;
    fast_ssim_model *model;
    clock_t start, end;
    double fast_time, normal_time;
    float result;
    int i;
    
    printf("=== Fast SSIM Performance Test ===\n");
    printf("Image size: %dx%d\n", width, height);
    printf("Number of comparisons: %d\n\n", num_comparisons);
    
    /* Create reference image */
    ref_image = create_test_image(width, height, 42);
    if (!ref_image) {
        printf("Failed to create reference image\n");
        return 1;
    }
    
    /* Create test images with different noise levels */
    test_images = (unsigned char**)malloc(num_comparisons * sizeof(unsigned char*));
    if (!test_images) {
        printf("Failed to allocate test image array\n");
        free(ref_image);
        return 1;
    }
    
    for (i = 0; i < num_comparisons; i++) {
        test_images[i] = add_noise(ref_image, width * height, i * 2);
        if (!test_images[i]) {
            printf("Failed to create test image %d\n", i);
            while (--i >= 0) free(test_images[i]);
            free(test_images);
            free(ref_image);
            return 1;
        }
    }
    
    /* Test 1: Fast SSIM with pre-computed model */
    printf("Testing Fast SSIM...\n");
    start = clock();
    
    model = fast_ssim_create_model(ref_image, width, height, width, 1, NULL);
    if (!model) {
        printf("Failed to create fast SSIM model\n");
        for (i = 0; i < num_comparisons; i++) free(test_images[i]);
        free(test_images);
        free(ref_image);
        return 1;
    }
    
    for (i = 0; i < num_comparisons; i++) {
        result = fast_ssim_compare(model, test_images[i], width);
    }
    
    fast_ssim_destroy_model(model);
    end = clock();
    fast_time = ((double)(end - start)) / CLOCKS_PER_SEC;
    
    /* Test 2: Normal SSIM */
    printf("Testing Normal SSIM...\n");
    start = clock();
    
    for (i = 0; i < num_comparisons; i++) {
        result = iqa_ssim(ref_image, test_images[i], width, height, width, 1, NULL);
    }
    
    end = clock();
    normal_time = ((double)(end - start)) / CLOCKS_PER_SEC;
    
    /* Results */
    printf("\n=== Results ===\n");
    printf("Fast SSIM total time: %.4f seconds\n", fast_time);
    printf("Normal SSIM total time: %.4f seconds\n", normal_time);
    printf("Speedup: %.2fx\n", normal_time / fast_time);
    printf("Time saved: %.4f seconds (%.1f%%)\n", 
           normal_time - fast_time, 
           ((normal_time - fast_time) / normal_time) * 100);
    
    /* Clean up */
    for (i = 0; i < num_comparisons; i++) {
        free(test_images[i]);
    }
    free(test_images);
    free(ref_image);
    
    return 0;
}