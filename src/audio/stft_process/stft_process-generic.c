// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2025 Intel Corporation.

#include <sof/audio/module_adapter/module/generic.h>
#include <sof/audio/component.h>
#include <sof/audio/sink_api.h>
#include <sof/audio/sink_source_utils.h>
#include <sof/audio/source_api.h>
#include <stdint.h>
#include "stft_process.h"

#if CONFIG_FORMAT_S16LE
/**
 * stft_process_s16() - Process S16_LE format.
 * @mod: Pointer to module data.
 * @source: Source for PCM samples data.
 * @sink: Sink for PCM samples data.
 * @frames: Number of audio data frames to process.
 *
 * This is the processing function for 16-bit signed integer PCM formats. The
 * audio samples in every frame are re-order to channels order defined in
 * component data channel_map[].
 *
 * Return: Value zero for success, otherwise an error code.
 */
int stft_process_source_s16(struct stft_comp_data *cd, struct sof_source *source, int frames)
{
	struct stft_process_state *state = &cd->state;
	struct stft_process_pre_emph *emph = &state->emph;
	struct stft_process_buffer *buf	= &state->buf;
	int32_t s;
	int16_t const *x, *x_start, *x_end;
	int16_t *w = buf->w_ptr;
	int16_t in;
	int x_size;
	int bytes = frames * cd->frame_bytes;
	int frames_left = frames;
	int ret;
	int n1;
	int n2;
	int n;
	int i;

	/* Get pointer to source data in circular buffer, get buffer start and size to
	 * check for wrap. The size in bytes is converted to number of s16 samples to
	 * control the samples process loop. If the number of bytes requested is not
	 * possible, an error is returned.
	 */
	ret = source_get_data_s16(source, bytes, &x, &x_start, &x_size);
	if (ret)
		return ret;

	/* Set helper pointers to buffer end for wrap check. Then loop until all
	 * samples are processed.
	 */
	x_end = x_start + x_size;

	while (frames_left) {
		/* Find out samples to process before first wrap or end of data. */
		n1 = (x_end - x) / cd->channels;
		n2 = stft_process_buffer_samples_without_wrap(buf, w);
		n = MIN(n1, n2);
		n = MIN(n, frames);

		/* Since the example processing is for frames of audio channels, process
		 * with step of channels count.
		 */
		for (i = 0; i < n; i++) {
			in = *(x + cd->source_channel);
			if (emph->enable) {
				/* Q1.15 x Q1.15 -> Q2.30 */
				s = (int32_t)emph->delay * emph->coef + Q_SHIFT_LEFT(in, 15, 30);
				*w = sat_int16(Q_SHIFT_RND(s, 30, 15));
				emph->delay = in;
			} else {
				*w = in;
			}
			x += cd->channels;
			w++;
		}

		/* One of the buffers needs a wrap (or end of data), so check for wrap */
		x = (x >= x_end) ? x - x_size : x;
		w = stft_process_buffer_wrap(buf, w);

		/* Update processed samples count for next loop iteration. */
		frames -= n;
	}

	/* Update the source for bytes consumed. Return success. */
	source_release_data(source, bytes);
	buf->s_avail += frames;
	buf->s_free -= frames;
	buf->w_ptr = w;
	return 0;
}
#endif /* CONFIG_FORMAT_S16LE */

void stft_process_fill_prev_samples(struct stft_process_buffer *buf, int16_t *prev_data,
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
		n = stft_process_buffer_samples_without_wrap(buf, r);
		n = MIN(n, nmax);
		memcpy(p, r, sizeof(int16_t) * n); /* Not using memcpy_s() due to speed need */
		p += n;
		r += n;
		r = stft_process_buffer_wrap(buf, r);
	}

	buf->s_avail -= copied;
	buf->s_free += copied;
	buf->r_ptr = r;
}

void stft_process_fill_fft_buffer(struct stft_process_state *state)
{
	struct stft_process_buffer *buf = &state->buf;
	struct stft_process_fft *fft = &state->fft;
	int16_t *r = buf->r_ptr;
	int copied;
	int nmax;
	int idx = fft->fft_fill_start_idx;
	int j;
	int n;

	/* Copy overlapped samples from state buffer. Imaginary part of input
	 * remains zero.
	 */
	for (j = 0; j < state->prev_data_size; j++)
		fft->fft_buf[idx + j].real = state->prev_data[j];

	/* Copy hop size of new data from circular buffer */
	idx += state->prev_data_size;
	for (copied = 0; copied < fft->fft_hop_size; copied += n) {
		nmax = fft->fft_hop_size - copied;
		n = stft_process_buffer_samples_without_wrap(buf, r);
		n = MIN(n, nmax);
		for (j = 0; j < n; j++) {
			fft->fft_buf[idx].real = *r;
			r++;
			idx++;
		}
		r = stft_process_buffer_wrap(buf, r);
	}

	buf->s_avail -= copied;
	buf->s_free += copied;
	buf->r_ptr = r;

	/* Copy for next time data back to overlap buffer */
	idx = fft->fft_fill_start_idx + fft->fft_hop_size;
	for (j = 0; j < state->prev_data_size; j++)
		state->prev_data[j] = fft->fft_buf[idx + j].real;
}

#ifdef STFT_PROCESS_NORMALIZE_FFT
int stft_process_normalize_fft_buffer(struct stft_process_state *state)
{
	struct stft_process_fft *fft = &state->fft;
	int32_t absx;
	int32_t smax = 0;
	int32_t x;
	int shift;
	int j;
	int i = fft->fft_fill_start_idx;

	for (j = 0; j < fft->fft_size; j++) {
		x = fft->fft_buf[i + j].real;
		absx = (x < 0) ? -x : x;
		if (smax < absx)
			smax = absx;
	}

	shift = norm_int32(smax << 15) - 1; /* 16 bit data */
	shift = MAX(shift, 0);
	shift = MIN(shift, STFT_PROCESS_NORMALIZE_MAX_SHIFT);
	return shift;
}
#endif

void stft_process_apply_window(struct stft_process_state *state, int input_shift)
{
	struct stft_process_fft *fft = &state->fft;
	int j;
	int i = fft->fft_fill_start_idx;

#if STFT_PROCESS_FFT_BITS == 16
	/* TODO: Use proper multiply and saturate function to make sure no overflows */
	int32_t x;
	int s = 14 - input_shift; /* Q1.15 x Q1.15 -> Q30 -> Q15, shift by 15 - 1 for round */

	for (j = 0; j < fft->fft_size; j++) {
		x = (int32_t)fft->fft_buf[i + j].real * state->window[j];
		fft->fft_buf[i + j].real = ((x >> s) + 1) >> 1;
	}
#else
	/* TODO: Use proper multiply and saturate function to make sure no overflows */
	int s = input_shift + 1; /* To convert 16 -> 32 with Q1.15 x Q1.15 -> Q30 -> Q31 */

	for (j = 0; j < fft->fft_size; j++)
		fft->fft_buf[i + j].real = (fft->fft_buf[i + j].real * state->window[j]) << s;
#endif
}
