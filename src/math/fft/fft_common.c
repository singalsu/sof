// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2020 Intel Corporation. All rights reserved.
//
// Author: Amery Song <chao.song@intel.com>
//	   Keyon Jie <yang.jie@linux.intel.com>

#include <sof/audio/module_adapter/module/generic.h>
#include <sof/audio/buffer.h>
#include <sof/audio/format.h>
#include <sof/trace/trace.h>
#include <sof/lib/uuid.h>
#include <sof/common.h>
#include <rtos/alloc.h>
#include <sof/math/fft.h>

LOG_MODULE_REGISTER(math_fft, CONFIG_SOF_LOG_LEVEL);
SOF_DEFINE_REG_UUID(math_fft);
DECLARE_TR_CTX(math_fft_tr, SOF_UUID(math_fft_uuid), LOG_LEVEL_INFO);

static int round_up_to_pow2(int n)
{
	int p = 1;

	while (p < n)
		p *= 2;

	return p;
}

static struct fft_plan *fft_plan_common_new(struct processing_module *mod, void *inb,
					    void *outb, uint32_t size, int bits)
{
	struct fft_plan *plan;
	int lim = 1;
	int len = 0;
	const int size_pow2 = round_up_to_pow2(size);

	if (!inb || !outb) {
		comp_cl_err(mod->dev, "NULL input/output buffers.");
		return NULL;
	}

	if (size != size_pow2) {
		comp_cl_err(mod->dev, "The FFT size must be a power of two.");
		return NULL;
	}

	plan = mod_zalloc(mod, sizeof(struct fft_plan));
	if (!plan) {
		comp_cl_err(mod->dev, "Failed to allocate FFT plan.");
		return NULL;
	}

	switch (bits) {
	case 16:
		plan->inb16 = inb;
		plan->outb16 = outb;
		break;
	case 32:
		plan->inb32 = inb;
		plan->outb32 = outb;
		break;
	default:
		comp_cl_err(mod->dev, "Illegal word length.");
		return NULL;
	}

	/* calculate the exponent of 2 */
	while (lim < size) {
		lim <<= 1;
		len++;
	}

	plan->size = lim;
	plan->len = len;
	return plan;
}

static void fft_plan_init_bit_reverse(uint16_t *bit_reverse_idx, int size, int len)
{
	int i;

	/* set up the bit reverse index */
	for (i = 1; i < size; ++i)
		bit_reverse_idx[i] = (bit_reverse_idx[i >> 1] >> 1) | ((i & 1) << (len - 1));
}

struct fft_plan *mod_fft_plan_new(struct processing_module *mod, void *inb,
				  void *outb, uint32_t size, int bits)
{
	struct fft_plan *plan;

	if (size > FFT_SIZE_MAX || size < FFT_SIZE_MIN) {
		comp_cl_err(mod->dev, "Illegal FFT size %d", size);
		return NULL;
	}

	plan = fft_plan_common_new(mod, inb, outb, size, bits);
	if (!plan)
		return NULL;

	plan->bit_reverse_idx = mod_zalloc(mod,	plan->size * sizeof(uint16_t));
	if (!plan->bit_reverse_idx) {
		comp_cl_err(mod->dev, "Failed to allocate bit reverse table.");
		mod_free(mod, plan);
		return NULL;
	}

	fft_plan_init_bit_reverse(plan->bit_reverse_idx, plan->size, plan->len);
	return plan;
}

struct fft_multi_plan *mod_fft_multi_plan_new(struct processing_module *mod, void *inb,
					      void *outb, uint32_t size, int bits)
{
	struct fft_multi_plan *plan;
	size_t tmp_size;
	const int size_pow2 = round_up_to_pow2(size);
	const int size_div3 = size / 3;
	int i;

	if (!inb || !outb) {
		comp_cl_err(mod->dev, "Null buffers");
		return NULL;
	}

	if (size < FFT_SIZE_MIN) {
		comp_cl_err(mod->dev, "Illegal FFT size %d", size);
		return NULL;
	}

	plan = mod_zalloc(mod, sizeof(struct fft_multi_plan));
	if (!plan)
		return NULL;

	if (size == size_pow2) {
		plan->num_ffts = 1;
	} else if (size_div3 * 3 == size) {
		plan->num_ffts = 3;
	} else {
		comp_cl_err(mod->dev, "Not supported FFT size %d", size);
		goto err;
	}

	/* Allocate common bit reverse table for all FFT plans */
	plan->total_size = size;
	plan->fft_size = size / plan->num_ffts;
	if (plan->fft_size > FFT_SIZE_MAX) {
		comp_cl_err(mod->dev, "Requested size %d FFT is too large", size);
		goto err;
	}

	plan->bit_reverse_idx = mod_zalloc(mod, plan->fft_size * sizeof(uint16_t));
	if (!plan->bit_reverse_idx) {
		comp_cl_err(mod->dev, "Failed to allocate FFT plan");
		goto err;
	}

	switch (bits) {
	case 16:
		plan->inb16 = inb;
		plan->outb16 = outb;
		break;
	case 32:
		plan->inb32 = inb;
		plan->outb32 = outb;

		if (plan->num_ffts > 1) {
			/* Allocate input/output buffers for FFTs */
			tmp_size = 2 * plan->num_ffts * plan->fft_size * sizeof(struct icomplex32);
			plan->tmp_i32[0] = mod_balloc(mod, tmp_size);
			if (!plan->tmp_i32[0]) {
				comp_cl_err(mod->dev, "Failed to allocate FFT buffers");
				goto err_free_bit_reverse;
			}

			/* Set up buffers */
			plan->tmp_o32[0] = plan->tmp_i32[0] + plan->fft_size;
			for (i = 1; i < plan->num_ffts; i++) {
				plan->tmp_i32[i] = plan->tmp_o32[i - 1] + plan->fft_size;
				plan->tmp_o32[i] = plan->tmp_i32[i] + plan->fft_size;
			}
		} else {
			plan->tmp_i32[0] = inb;
			plan->tmp_o32[0] = outb;
		}

		for (i = 0; i < plan->num_ffts; i++) {
			plan->fft_plan[i] = fft_plan_common_new(mod,
								plan->tmp_i32[i],
								plan->tmp_o32[i],
								plan->fft_size, 32);
			if (!plan->fft_plan[i])
				goto err_free_buffer;

			plan->fft_plan[i]->bit_reverse_idx = plan->bit_reverse_idx;
		}
		break;
	default:
		comp_cl_err(mod->dev, "Not supported word length %d", bits);
		goto err;
	}

	/* Set up common bit index reverse table */
	fft_plan_init_bit_reverse(plan->bit_reverse_idx, plan->fft_plan[0]->size,
				  plan->fft_plan[0]->len);
	return plan;

err_free_buffer:
	mod_free(mod, plan->tmp_i32[0]);

err_free_bit_reverse:
	mod_free(mod, plan->bit_reverse_idx);

err:
	mod_free(mod, plan);
	return NULL;
}

void mod_fft_plan_free(struct processing_module *mod, struct fft_plan *plan)
{
	if (!plan)
		return;

	mod_free(mod, plan->bit_reverse_idx);
	mod_free(mod, plan);
}

void mod_fft_multi_plan_free(struct processing_module *mod, struct fft_multi_plan *plan)
{
	int i;

	if (!plan)
		return;

	for (i = 0; i < plan->num_ffts; i++)
		mod_free(mod, plan->fft_plan[i]);

	/* If single FFT, the internal buffers were not allocated. */
	if (plan->num_ffts > 1)
		mod_free(mod, plan->tmp_i32[0]);

	mod_free(mod, plan->bit_reverse_idx);
	mod_free(mod, plan);
}
