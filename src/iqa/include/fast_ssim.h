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

#ifndef _FAST_SSIM_H_
#define _FAST_SSIM_H_

#include "iqa.h"

/**
 * Opaque structure holding pre-computed data for the reference image
 */
typedef struct fast_ssim_model fast_ssim_model;

/**
 * Creates a fast SSIM model from a reference image.
 * The model pre-computes values that can be reused when comparing
 * the reference image against multiple comparison images.
 *
 * @param ref Original reference image
 * @param w Width of the image
 * @param h Height of the image
 * @param stride The length (in bytes) of each horizontal line in the image.
 *               This may be different from the image width.
 * @param gaussian 0 = 8x8 square window, 1 = 11x11 circular-symmetric Gaussian
 * weighting.
 * @param args Optional SSIM arguments for fine control of the algorithm. 0 for
 * defaults. Defaults are a=b=g=1.0, L=255, K1=0.01, K2=0.03
 * @return The model handle, or NULL if error.
 */
fast_ssim_model* fast_ssim_create_model(
    const unsigned char *ref,
    int w,
    int h,
    int stride,
    int gaussian,
    const struct iqa_ssim_args *args
);

/**
 * Compares an image against the pre-computed reference model.
 * This function uses the pre-computed values from the model to speed up
 * the SSIM calculation.
 *
 * @param model The pre-computed model created by fast_ssim_create_model
 * @param cmp Distorted image to compare
 * @param stride The length (in bytes) of each horizontal line in the comparison image.
 *               This may be different from the image width.
 * @return The mean SSIM over the entire image (MSSIM), or INFINITY if error.
 */
float fast_ssim_compare(
    const fast_ssim_model *model,
    const unsigned char *cmp,
    int stride
);

/**
 * Destroys a fast SSIM model and frees all associated memory.
 *
 * @param model The model to destroy
 */
void fast_ssim_destroy_model(fast_ssim_model *model);

#endif /* _FAST_SSIM_H_ */