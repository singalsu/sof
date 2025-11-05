/* SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright(c) 2017 Intel Corporation. All rights reserved.
 *
 * Author: Amery Song <chao.song@intel.com>
 *	   Keyon Jie <yang.jie@linux.intel.com>
 */

#ifndef __SOF_FFT_H__
#define __SOF_FFT_H__

#include <sof/audio/module_adapter/module/generic.h>
#include <sof/audio/format.h>
#include <sof/common.h>
#include <stdbool.h>
#include <stdint.h>

#define FFT_GENERIC
#if defined(__XCC__)

#include <xtensa/config/core-isa.h>
#if XCHAL_HAVE_HIFI3 || XCHAL_HAVE_HIFI4
#undef FFT_GENERIC
#define FFT_HIFI3
#endif

#endif

#define FFT_SIZE_MAX		1024
#define FFT_MULTI_COUNT_MAX	3


struct icomplex32 {
	int32_t real;
	int32_t imag;
};

/* Note: the add of packed attribute to icmplex16 would significantly increase
 * processing time of fft_execute_16() so it is not done. The optimized versions of
 * FFT for HiFi will need a different packed data structure vs. generic C.
 */
struct icomplex16 {
	int16_t real;
	int16_t imag;
};

struct fft_plan {
	uint32_t size;	/* fft size */
	uint32_t len;	/* fft length in exponent of 2 */
	uint16_t *bit_reverse_idx;	/* pointer to bit reverse index array */
	struct icomplex32 *inb32;	/* pointer to input integer complex buffer */
	struct icomplex32 *outb32;	/* pointer to output integer complex buffer */
	struct icomplex16 *inb16;	/* pointer to input integer complex buffer */
	struct icomplex16 *outb16;	/* pointer to output integer complex buffer */
};

struct fft_multi_plan {
	struct fft_plan *fft_plan[FFT_MULTI_COUNT_MAX];
	struct icomplex32 *tmp_i32[FFT_MULTI_COUNT_MAX]; /* pointer to input buffer */
	struct icomplex32 *tmp_o32[FFT_MULTI_COUNT_MAX]; /* pointer to output buffer */
	struct icomplex16 *tmp_i16[FFT_MULTI_COUNT_MAX]; /* pointer to input buffer */
	struct icomplex16 *tmp_o16[FFT_MULTI_COUNT_MAX]; /* pointer to output buffer */
	struct icomplex32 *inb32;	/* pointer to input integer complex buffer */
	struct icomplex32 *outb32;	/* pointer to output integer complex buffer */
	struct icomplex16 *inb16;	/* pointer to input integer complex buffer */
	struct icomplex16 *outb16;	/* pointer to output integer complex buffer */
	uint16_t *bit_reverse_idx;
	uint32_t total_size;
	uint32_t fft_size;
	int num_ffts;
};

/* interfaces of the library */
struct fft_plan *mod_fft_plan_new(struct processing_module *mod, void *inb,
				  void *outb, uint32_t size, int bits);
void fft_execute_16(struct fft_plan *plan, bool ifft);
void fft_execute_32(struct fft_plan *plan, bool ifft);
void mod_fft_plan_free(struct processing_module *mod, struct fft_plan *plan);

struct fft_multi_plan *mod_fft_multi_plan_new(struct processing_module *mod, void *inb,
					      void *outb, uint32_t size, int bits);
void fft_multi_execute_32(struct fft_multi_plan *plan, bool ifft);
void mod_fft_multi_plan_free(struct processing_module *mod, struct fft_multi_plan *plan);

#endif /* __SOF_FFT_H__ */
