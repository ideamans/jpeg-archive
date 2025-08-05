/*
 * Copyright (c) 2025
 * Fast SSIM implementation with pre-computed model for multiple comparisons
 * 
 * The BSD License
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * - Neither the name of the contributors may be used to endorse or promote 
 *   products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE 
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "fast_ssim.h"
#include "convolve.h"
#include "decimate.h"
#include "math_utils.h"
#include "ssim.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

struct fast_ssim_model {
    /* Image dimensions */
    int width;
    int height;
    int scaled_width;      /* Dimensions after scaling */
    int scaled_height;
    int convolved_width;    /* Dimensions after convolution */
    int convolved_height;
    int scale;
    
    /* Algorithm parameters */
    float alpha;
    float beta;
    float gamma;
    int L;
    float K1;
    float K2;
    float C1;
    float C2;
    float C3;
    
    /* Window kernel */
    struct _kernel window;
    float *kernel_data;
    
    /* Pre-computed data for reference image */
    float *ref_f;           /* Reference image as float (scaled) */
    float *ref_mu;          /* Mean of reference (convolved) */
    float *ref_sigma_sqd;   /* Variance of reference (convolved) */
};

fast_ssim_model* fast_ssim_create_model(
    const unsigned char *ref,
    int w,
    int h,
    int stride,
    int gaussian,
    const struct iqa_ssim_args *args)
{
    fast_ssim_model *model;
    int scale;
    int x, y, src_offset, offset;
    struct _kernel low_pass;
    float *ref_sigma_sqd_tmp;
    int kernel_size;
    
    /* Allocate model structure */
    model = (fast_ssim_model*)calloc(1, sizeof(fast_ssim_model));
    if (!model)
        return NULL;
    
    /* Store dimensions */
    model->width = w;
    model->height = h;
    
    /* Initialize algorithm parameters */
    model->alpha = 1.0f;
    model->beta = 1.0f;
    model->gamma = 1.0f;
    model->L = 255;
    model->K1 = 0.01f;
    model->K2 = 0.03f;
    
    scale = _max(1, _round((float)_min(w, h) / 256.0f));
    if (args) {
        if (args->f)
            scale = args->f;
        model->alpha = args->alpha;
        model->beta = args->beta;
        model->gamma = args->gamma;
        model->L = args->L;
        model->K1 = args->K1;
        model->K2 = args->K2;
    }
    model->scale = scale;
    
    model->C1 = (model->K1 * model->L) * (model->K1 * model->L);
    model->C2 = (model->K2 * model->L) * (model->K2 * model->L);
    model->C3 = model->C2 / 2.0f;
    
    /* Setup window kernel */
    if (gaussian) {
        kernel_size = GAUSSIAN_LEN * GAUSSIAN_LEN;
        model->kernel_data = (float*)malloc(kernel_size * sizeof(float));
        if (!model->kernel_data) {
            free(model);
            return NULL;
        }
        memcpy(model->kernel_data, g_gaussian_window, kernel_size * sizeof(float));
        model->window.kernel = model->kernel_data;
        model->window.w = model->window.h = GAUSSIAN_LEN;
    } else {
        kernel_size = SQUARE_LEN * SQUARE_LEN;
        model->kernel_data = (float*)malloc(kernel_size * sizeof(float));
        if (!model->kernel_data) {
            free(model);
            return NULL;
        }
        memcpy(model->kernel_data, g_square_window, kernel_size * sizeof(float));
        model->window.kernel = model->kernel_data;
        model->window.w = model->window.h = SQUARE_LEN;
    }
    model->window.normalized = 1;
    model->window.bnd_opt = KBND_SYMMETRIC;
    
    /* Convert reference image to float */
    model->ref_f = (float*)malloc(w * h * sizeof(float));
    if (!model->ref_f) {
        free(model->kernel_data);
        free(model);
        return NULL;
    }
    
    for (y = 0; y < h; ++y) {
        src_offset = y * stride;
        offset = y * w;
        for (x = 0; x < w; ++x, ++offset, ++src_offset) {
            model->ref_f[offset] = (float)ref[src_offset];
        }
    }
    
    /* Scale the image down if required */
    if (scale > 1) {
        /* Generate simple low-pass filter */
        low_pass.kernel = (float*)malloc(scale * scale * sizeof(float));
        if (!low_pass.kernel) {
            free(model->ref_f);
            free(model->kernel_data);
            free(model);
            return NULL;
        }
        low_pass.w = low_pass.h = scale;
        low_pass.normalized = 0;
        low_pass.bnd_opt = KBND_SYMMETRIC;
        for (offset = 0; offset < scale * scale; ++offset)
            low_pass.kernel[offset] = 1.0f / (scale * scale);
        
        /* Resample */
        model->scaled_width = w;
        model->scaled_height = h;
        if (_iqa_decimate(model->ref_f, w, h, scale, &low_pass, 0, 
                         &model->scaled_width, &model->scaled_height)) {
            free(low_pass.kernel);
            free(model->ref_f);
            free(model->kernel_data);
            free(model);
            return NULL;
        }
        free(low_pass.kernel);
    } else {
        model->scaled_width = w;
        model->scaled_height = h;
    }
    
    /* Pre-compute mean and variance for reference image */
    w = model->scaled_width;
    h = model->scaled_height;
    
    model->ref_mu = (float*)malloc(w * h * sizeof(float));
    model->ref_sigma_sqd = (float*)malloc(w * h * sizeof(float));
    ref_sigma_sqd_tmp = (float*)malloc(w * h * sizeof(float));
    
    if (!model->ref_mu || !model->ref_sigma_sqd || !ref_sigma_sqd_tmp) {
        free(ref_sigma_sqd_tmp);
        free(model->ref_sigma_sqd);
        free(model->ref_mu);
        free(model->ref_f);
        free(model->kernel_data);
        free(model);
        return NULL;
    }
    
    /* Calculate mean of reference */
    _iqa_convolve(model->ref_f, w, h, &model->window, model->ref_mu, 0, 0);
    
    /* Calculate ref^2 for later use */
    for (y = 0; y < h; ++y) {
        offset = y * w;
        for (x = 0; x < w; ++x, ++offset) {
            ref_sigma_sqd_tmp[offset] = model->ref_f[offset] * model->ref_f[offset];
        }
    }
    
    /* Calculate variance of reference */
    _iqa_convolve(ref_sigma_sqd_tmp, w, h, &model->window, model->ref_sigma_sqd, &w, &h);
    
    /* Store convolved dimensions */
    model->convolved_width = w;
    model->convolved_height = h;
    
    /* Compute final variance: E[X^2] - E[X]^2 */
    for (y = 0; y < h; ++y) {
        offset = y * w;
        for (x = 0; x < w; ++x, ++offset) {
            model->ref_sigma_sqd[offset] = model->ref_sigma_sqd[offset] - 
                                          model->ref_mu[offset] * model->ref_mu[offset];
        }
    }
    
    free(ref_sigma_sqd_tmp);
    
    return model;
}

float fast_ssim_compare(
    const fast_ssim_model *model,
    const unsigned char *cmp,
    int stride)
{
    float *cmp_f, *cmp_mu, *cmp_sigma_sqd, *sigma_both;
    float *cmp_sq;
    struct _kernel low_pass;
    int w, h, scale;
    int x, y, src_offset, offset;
    double ssim_sum = 0.0;
    double numerator, denominator;
    double luminance_comp, contrast_comp, structure_comp, sigma_root;
    
    if (!model || !cmp)
        return INFINITY;
    
    w = model->width;
    h = model->height;
    scale = model->scale;
    
    /* Convert comparison image to float */
    cmp_f = (float*)malloc(w * h * sizeof(float));
    if (!cmp_f)
        return INFINITY;
    
    for (y = 0; y < h; ++y) {
        src_offset = y * stride;
        offset = y * w;
        for (x = 0; x < w; ++x, ++offset, ++src_offset) {
            cmp_f[offset] = (float)cmp[src_offset];
        }
    }
    
    /* Scale the comparison image down if required */
    if (scale > 1) {
        /* Generate simple low-pass filter */
        low_pass.kernel = (float*)malloc(scale * scale * sizeof(float));
        if (!low_pass.kernel) {
            free(cmp_f);
            return INFINITY;
        }
        low_pass.w = low_pass.h = scale;
        low_pass.normalized = 0;
        low_pass.bnd_opt = KBND_SYMMETRIC;
        for (offset = 0; offset < scale * scale; ++offset)
            low_pass.kernel[offset] = 1.0f / (scale * scale);
        
        /* Resample */
        if (_iqa_decimate(cmp_f, w, h, scale, &low_pass, 0, 0, 0)) {
            free(low_pass.kernel);
            free(cmp_f);
            return INFINITY;
        }
        free(low_pass.kernel);
    }
    
    /* Use scaled dimensions for allocation */
    w = model->scaled_width;
    h = model->scaled_height;
    
    /* Allocate memory for comparison image statistics */
    cmp_mu = (float*)malloc(w * h * sizeof(float));
    cmp_sigma_sqd = (float*)malloc(w * h * sizeof(float));
    sigma_both = (float*)malloc(w * h * sizeof(float));
    cmp_sq = (float*)malloc(w * h * sizeof(float));
    
    if (!cmp_mu || !cmp_sigma_sqd || !sigma_both || !cmp_sq) {
        free(cmp_sq);
        free(sigma_both);
        free(cmp_sigma_sqd);
        free(cmp_mu);
        free(cmp_f);
        return INFINITY;
    }
    
    /* Calculate mean of comparison - use scaled dimensions */
    _iqa_convolve(cmp_f, model->scaled_width, model->scaled_height, &model->window, cmp_mu, 0, 0);
    
    /* Calculate cmp^2 and ref*cmp */
    for (y = 0; y < model->scaled_height; ++y) {
        offset = y * model->scaled_width;
        for (x = 0; x < model->scaled_width; ++x, ++offset) {
            cmp_sq[offset] = cmp_f[offset] * cmp_f[offset];
            sigma_both[offset] = model->ref_f[offset] * cmp_f[offset];
        }
    }
    
    /* Calculate variance and covariance */
    _iqa_convolve(cmp_sq, model->scaled_width, model->scaled_height, &model->window, cmp_sigma_sqd, 0, 0);
    _iqa_convolve(sigma_both, model->scaled_width, model->scaled_height, &model->window, 0, &w, &h);
    
    /* Compute final statistics */
    for (y = 0; y < h; ++y) {
        offset = y * w;
        for (x = 0; x < w; ++x, ++offset) {
            cmp_sigma_sqd[offset] -= cmp_mu[offset] * cmp_mu[offset];
            sigma_both[offset] -= model->ref_mu[offset] * cmp_mu[offset];
        }
    }
    
    /* Use convolved dimensions for SSIM calculation */
    w = model->convolved_width;
    h = model->convolved_height;
    
    /* Calculate SSIM */
    if (model->alpha == 1.0f && model->beta == 1.0f && model->gamma == 1.0f) {
        /* Default case - faster computation */
        for (y = 0; y < h; ++y) {
            offset = y * w;
            for (x = 0; x < w; ++x, ++offset) {
                numerator = (2.0 * model->ref_mu[offset] * cmp_mu[offset] + model->C1) * 
                           (2.0 * sigma_both[offset] + model->C2);
                denominator = (model->ref_mu[offset] * model->ref_mu[offset] + 
                              cmp_mu[offset] * cmp_mu[offset] + model->C1) * 
                             (model->ref_sigma_sqd[offset] + cmp_sigma_sqd[offset] + model->C2);
                ssim_sum += numerator / denominator;
            }
        }
    } else {
        /* Custom alpha, beta, gamma */
        for (y = 0; y < h; ++y) {
            offset = y * w;
            for (x = 0; x < w; ++x, ++offset) {
                /* Handle negative variance */
                float ref_sigma_sqd_safe = model->ref_sigma_sqd[offset];
                float cmp_sigma_sqd_safe = cmp_sigma_sqd[offset];
                if (ref_sigma_sqd_safe < 0.0f)
                    ref_sigma_sqd_safe = 0.0f;
                if (cmp_sigma_sqd_safe < 0.0f)
                    cmp_sigma_sqd_safe = 0.0f;
                
                sigma_root = sqrt(ref_sigma_sqd_safe * cmp_sigma_sqd_safe);
                
                /* Luminance */
                if (model->C1 == 0 && model->ref_mu[offset] * model->ref_mu[offset] == 0 && 
                    cmp_mu[offset] * cmp_mu[offset] == 0) {
                    luminance_comp = 1.0;
                } else {
                    double result = (2.0 * model->ref_mu[offset] * cmp_mu[offset] + model->C1) / 
                                   (model->ref_mu[offset] * model->ref_mu[offset] + 
                                    cmp_mu[offset] * cmp_mu[offset] + model->C1);
                    if (model->alpha == 1.0f) {
                        luminance_comp = result;
                    } else {
                        float sign = result < 0.0 ? -1.0f : 1.0f;
                        luminance_comp = sign * pow(fabs(result), (double)model->alpha);
                    }
                }
                
                /* Contrast */
                if (model->C2 == 0 && ref_sigma_sqd_safe + cmp_sigma_sqd_safe == 0) {
                    contrast_comp = 1.0;
                } else {
                    double result = (2.0 * sigma_root + model->C2) / 
                                   (ref_sigma_sqd_safe + cmp_sigma_sqd_safe + model->C2);
                    if (model->beta == 1.0f) {
                        contrast_comp = result;
                    } else {
                        float sign = result < 0.0 ? -1.0f : 1.0f;
                        contrast_comp = sign * pow(fabs(result), (double)model->beta);
                    }
                }
                
                /* Structure */
                if (model->C3 == 0 && sigma_root == 0) {
                    structure_comp = 1.0;
                } else {
                    double result = (sigma_both[offset] + model->C3) / (sigma_root + model->C3);
                    if (model->gamma == 1.0f) {
                        structure_comp = result;
                    } else {
                        float sign = result < 0.0 ? -1.0f : 1.0f;
                        structure_comp = sign * pow(fabs(result), (double)model->gamma);
                    }
                }
                
                ssim_sum += luminance_comp * contrast_comp * structure_comp;
            }
        }
    }
    
    /* Clean up */
    free(cmp_sq);
    free(sigma_both);
    free(cmp_sigma_sqd);
    free(cmp_mu);
    free(cmp_f);
    
    return (float)(ssim_sum / (double)(w * h));
}

void fast_ssim_destroy_model(fast_ssim_model *model)
{
    if (model) {
        free(model->ref_sigma_sqd);
        free(model->ref_mu);
        free(model->ref_f);
        free(model->kernel_data);
        free(model);
    }
}