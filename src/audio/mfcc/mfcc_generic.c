// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2022-2026 Intel Corporation.
//
// Author: Seppo Ingalsuo <seppo.ingalsuo@linux.intel.com>

#include <sof/audio/mfcc/mfcc_comp.h>
#ifdef MFCC_GENERIC

#include <sof/audio/component.h>
#include <sof/math/auditory.h>
#include <sof/math/icomplex16.h>
#include <sof/math/icomplex32.h>
#include <sof/math/matrix.h>
#include <sof/math/sqrt.h>
#include <sof/math/trig.h>
#include <sof/math/window.h>
#include <sof/trace/trace.h>
#include <user/mfcc.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

/*
 * MFCC algorithm code
 */

void mfcc_fill_prev_samples(struct mfcc_buffer *buf, int16_t *prev_data,
			    int prev_data_length)
{
	/* Fill prev_data from input buffer */
	int16_t *r = buf->r_ptr;
	int16_t *p = prev_data;
	int copied;
	int nmax;
	int n;

	for (copied = 0; copied < prev_data_length; copied += n) {
		nmax = prev_data_length - copied;
		n = mfcc_buffer_samples_without_wrap(buf, r);
		n = MIN(n, nmax);
		memcpy(p, r, sizeof(int16_t) * n); /* Not using memcpy_s() due to speed need */
		p += n;
		r += n;
		r = mfcc_buffer_wrap(buf, r);
	}

	buf->s_avail -= copied;
	buf->s_free += copied;
	buf->r_ptr = r;
}

void mfcc_apply_window(struct mfcc_state *state, int input_shift)
{
	struct mfcc_fft *fft = &state->fft;
	int j;
	int i = fft->fft_fill_start_idx;

	/* TODO: Use proper multiply and saturate function to make sure no overflows */
	int s = input_shift + 1; /* To convert 16 -> 32 with Q1.15 x Q1.15 -> Q30 -> Q31 */

	for (j = 0; j < fft->fft_size; j++)
		fft->fft_buf[i + j].real = (fft->fft_buf[i + j].real * state->window[j]) << s;
}

#endif /* MFCC_GENERIC */

/*
 * Source/sink API based source copy functions.
 * These use sof_source API and are compiled on all platforms (generic, HiFi3, HiFi4).
 */

#include <module/audio/source_api.h>

#if CONFIG_FORMAT_S16LE
void mfcc_source_copy_s16(struct sof_source *source, struct mfcc_buffer *buf,
			  struct mfcc_pre_emph *emph, int frames, int source_channel)
{
	int16_t const *src_ptr;
	int16_t const *src_start;
	int src_samples;
	int num_channels = source_get_channels(source);
	size_t req_bytes = frames * num_channels * sizeof(int16_t);
	int16_t *w = buf->w_ptr;
	int16_t const *x;
	int32_t s;
	int ret;
	int i;

	ret = source_get_data_s16(source, req_bytes, &src_ptr, &src_start, &src_samples);
	if (ret)
		return;

	x = src_ptr + source_channel;
	for (i = 0; i < frames; i++) {
		if (emph->enable) {
			s = (int32_t)emph->delay * emph->coef + Q_SHIFT_LEFT(*x, 15, 30);
			*w = sat_int16(Q_SHIFT_RND(s, 30, 15));
			emph->delay = *x;
		} else {
			*w = *x;
		}
		x += num_channels;
		/* Wrap source pointer */
		if (x >= src_start + src_samples)
			x -= src_samples;

		w++;
		w = mfcc_buffer_wrap(buf, w);
	}

	buf->s_avail += frames;
	buf->s_free -= frames;
	buf->w_ptr = w;
	source_release_data(source, req_bytes);
}
#endif /* CONFIG_FORMAT_S16LE */

#if CONFIG_FORMAT_S24LE
void mfcc_source_copy_s24(struct sof_source *source, struct mfcc_buffer *buf,
			  struct mfcc_pre_emph *emph, int frames, int source_channel)
{
	int32_t const *src_ptr;
	int32_t const *src_start;
	int src_samples;
	int num_channels = source_get_channels(source);
	size_t req_bytes = frames * num_channels * sizeof(int32_t);
	int16_t *w = buf->w_ptr;
	int32_t const *x;
	int32_t s, tmp;
	int ret;
	int i;

	ret = source_get_data_s32(source, req_bytes, &src_ptr, &src_start, &src_samples);
	if (ret)
		return;

	x = src_ptr + source_channel;
	for (i = 0; i < frames; i++) {
		if (emph->enable) {
			s = (int32_t)((uint32_t)*x << 8);
			tmp = (int32_t)emph->delay * emph->coef + Q_SHIFT(s, 31, 30);
			*w = sat_int16(Q_SHIFT_RND(tmp, 30, 15));
			emph->delay = sat_int16(Q_SHIFT_RND(s, 31, 15));
		} else {
			s = (int32_t)((uint32_t)*x << 8);
			*w = sat_int16(Q_SHIFT_RND(s, 31, 15));
		}
		x += num_channels;
		if (x >= src_start + src_samples)
			x -= src_samples;

		w++;
		w = mfcc_buffer_wrap(buf, w);
	}

	buf->s_avail += frames;
	buf->s_free -= frames;
	buf->w_ptr = w;
	source_release_data(source, req_bytes);
}
#endif /* CONFIG_FORMAT_S24LE */

#if CONFIG_FORMAT_S32LE
void mfcc_source_copy_s32(struct sof_source *source, struct mfcc_buffer *buf,
			  struct mfcc_pre_emph *emph, int frames, int source_channel)
{
	int32_t const *src_ptr;
	int32_t const *src_start;
	int src_samples;
	int num_channels = source_get_channels(source);
	size_t req_bytes = frames * num_channels * sizeof(int32_t);
	int16_t *w = buf->w_ptr;
	int32_t const *x;
	int32_t s;
	int ret;
	int i;

	ret = source_get_data_s32(source, req_bytes, &src_ptr, &src_start, &src_samples);
	if (ret)
		return;

	x = src_ptr + source_channel;
	for (i = 0; i < frames; i++) {
		if (emph->enable) {
			s = (int32_t)emph->delay * emph->coef + Q_SHIFT(*x, 31, 30);
			*w = sat_int16(Q_SHIFT_RND(s, 30, 15));
			emph->delay = sat_int16(Q_SHIFT_RND(*x, 31, 15));
		} else {
			*w = sat_int16(Q_SHIFT_RND(*x, 31, 15));
		}
		x += num_channels;
		if (x >= src_start + src_samples)
			x -= src_samples;

		w++;
		w = mfcc_buffer_wrap(buf, w);
	}

	buf->s_avail += frames;
	buf->s_free -= frames;
	buf->w_ptr = w;
	source_release_data(source, req_bytes);
}
#endif /* CONFIG_FORMAT_S32LE */
