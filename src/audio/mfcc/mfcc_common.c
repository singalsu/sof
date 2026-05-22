// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2023-2026 Intel Corporation.
//
// Author: Andrula Song <andrula.song@intel.com>

#include <sof/audio/mfcc/mfcc_comp.h>

#include <sof/audio/component.h>
#include <sof/audio/format.h>
#include <sof/math/auditory.h>
#include <sof/math/fft.h>
#include <sof/math/matrix.h>
#include <sof/math/sqrt.h>
#include <sof/math/trig.h>
#include <sof/math/window.h>
#include <sof/trace/trace.h>
#include <user/mfcc.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <sof/audio/mfcc/mfcc_vad.h>

LOG_MODULE_REGISTER(mfcc_common, CONFIG_SOF_LOG_LEVEL);

/*
 * The main processing function for MFCC
 */

int mfcc_stft_process(struct processing_module *mod, struct mfcc_comp_data *cd)
{
	const struct comp_dev *dev = mod->dev;
	struct sof_mfcc_config *config = cd->config;
	struct mfcc_state *state = &cd->state;
	struct mfcc_buffer *buf = &state->buf;
	struct mfcc_fft *fft = &state->fft;
	int mel_scale_shift;
	int input_shift;
	int j;
	int m;
	int cc_count = 0;
	int64_t s;
	int32_t mel_value;
	int32_t peak;
	int32_t clamp_value;

	/* Phase 1, wait until whole fft_size is filled with valid data. This way
	 * first output cepstral coefficients originate from streamed data and not
	 * from buffers with zero data.
	 */
	comp_dbg(dev, "avail = %d", buf->s_avail);
	if (state->waiting_fill) {
		if (buf->s_avail < fft->fft_size)
			return 0;

		state->waiting_fill = false;
	}

	/* Phase 2, move first prev_size data to previous data buffer, remove
	 * samples from input buffer.
	 */
	if (!state->prev_samples_valid) {
		mfcc_fill_prev_samples(buf, state->prev_data, state->prev_data_size);
		state->prev_samples_valid = true;
	}

	/* Check if enough samples in buffer for one FFT hop */
	m = buf->s_avail / fft->fft_hop_size;
	if (m > 0) {
		/* Clear FFT input buffer because it has been used as scratch */
		bzero(fft->fft_buf, fft->fft_buffer_size);

		/* Copy data to FFT input buffer from overlap buffer and from new samples buffer */
		mfcc_fill_fft_buffer(state);

		/* TODO: remove_dc_offset */

		/* TODO: use_energy & raw_energy */

		input_shift = 0;

		/* Window function */
		mfcc_apply_window(state, input_shift);

		/* TODO: use_energy & !raw_energy */

		/* The FFT out buffer needs to be cleared to avoid to corrupt
		 * the output. TODO: check moving it to FFT lib.
		 */
		bzero(fft->fft_out, fft->fft_buffer_size);

		/* Compute FFT */
		fft_execute_32(fft->fft_plan, false);

		/* Initialize 16-bit Mel log spectrum buffer in Q9.7. The Mel values
		 * are converted from Q9.23 to Q9.7 for DCT matrix multiplication.
		 */
		mat_init_16b(state->mel_spectra, 1, state->dct.num_in, 7); /* Q9.7 */

		/* Compensate FFT lib scaling to Mel log values, e.g. for 512 long FFT
		 * the fft_plan->len is 9. The scaling is 1/512. Subtract from input_shift it
		 * to add the missing "gain".
		 */
		mel_scale_shift = input_shift - fft->fft_plan->len;
		psy_apply_mel_filterbank_32(&state->melfb, fft->fft_out, state->power_spectra,
					    state->mel_log_32, mel_scale_shift);

		if (state->mel_only) {
			/* In Mel-only mode output Mel log spectra directly */
			cc_count += state->dct.num_in;

			/* Find peak mel value and track state->mmax in Q9.23 */
			if (config->dynamic_mmax) {
				peak = state->mel_log_32[0];
				for (j = 1; j < state->dct.num_in; j++) {
					if (state->mel_log_32[j] > peak)
						peak = state->mel_log_32[j];
				}

				/* Jump to peak immediately if higher, decay otherwise */
				if (peak > state->mmax) {
					state->mmax = peak;
				} else {
					/* Q9.23 * Q1.15, result Q9.23. The coefficient is small
					 * so no need for saturation.
					 */
					s = (int64_t)peak - state->mmax;
					state->mmax +=
						Q_MULTSR_32X32(s, config->mmax_coef, 23, 15, 23);
				}
			}

			/* Clamp Mel values lower than mmax - top_db, add offset, and scale.
			 * Config top_db and mel_offset are Q9.7, shift to Q9.23.
			 */
			clamp_value = state->mmax - ((int32_t)config->top_db << 16);
			for (j = 0; j < state->dct.num_in; j++) {
				mel_value = state->mel_log_32[j];
				if (mel_value < clamp_value)
					mel_value = clamp_value;

				/* Q9.23 * Q4.12, result Q9.23 */
				s = (int64_t)mel_value + ((int32_t)config->mel_offset << 16);
				state->mel_log_32[j] =
					sat_int32(Q_MULTSR_32X32(s, config->mel_scale, 23, 12, 23));
			}

			/* Store Q9.7 version in mel_spectra for s16 output mode */
			for (j = 0; j < state->dct.num_in; j++)
				state->mel_spectra->data[j] =
					sat_int16(state->mel_log_32[j] >> 16);

			/* Enable this to check mmax decay */
			comp_dbg(dev, "state->mmax = %d", state->mmax);
		} else {
			/* Convert Q9.23 to Q9.7 for 16-bit DCT */
			for (j = 0; j < state->dct.num_in; j++)
				state->mel_spectra->data[j] =
					sat_int16(state->mel_log_32[j] >> 16);

			/* Multiply Mel spectra with DCT matrix to get cepstral coefficients */
			mat_init_16b(state->cepstral_coef, 1, state->dct.num_out, 7); /* Q9.7 */
			mat_multiply(state->mel_spectra, state->dct.matrix, state->cepstral_coef);

			/* Apply cepstral lifter */
			if (state->lifter.cepstral_lifter != 0) {
				mat_multiply_elementwise(state->cepstral_coef, state->lifter.matrix,
							 state->cepstral_coef);
			}

			cc_count += state->dct.num_out;
		}

		/* Use hop counter for frame numbering (independent of VAD enable) */
		state->header.frame_number = state->hop_count;

		/* Run VAD on the mel log spectrum (available in both modes) */
		if (config->enable_vad) {
			mfcc_vad_update(&cd->vad, state->mel_log_32);

			/* Populate data header for this output frame */
			state->header.energy = cd->vad.energy;
			state->header.noise_energy = cd->vad.noise_energy;
			state->header.vad_flag = cd->vad.is_speech ? 1 : 0;
		}

		/* Increment hop counter at end of hop processing */
		state->hop_count++;

		/* Send notification when VAD state changes */
		if (config->enable_vad && config->update_controls) {
			bool vad_now = cd->vad.is_speech;

			if (vad_now != cd->vad_prev) {
				mfcc_send_vad_notification(mod, vad_now ? 1 : 0);
				cd->vad_prev = vad_now;
			}
		}
	}

	return cc_count;
}

void mfcc_fill_fft_buffer(struct mfcc_state *state)
{
	struct mfcc_buffer *buf = &state->buf;
	struct mfcc_fft *fft = &state->fft;
	int32_t *d = &fft->fft_buf[fft->fft_fill_start_idx].real;
	const int fft_elem_inc = sizeof(fft->fft_buf[0]) / sizeof(int32_t);
	int16_t *prev = state->prev_data;
	int16_t *prev_end = prev + state->prev_data_size;
	int16_t *r = buf->r_ptr;
	int copied;
	int nmax;
	int n;
	int j;

	/* Copy overlapped samples from state buffer. The fft_buf has been
	 * cleared by caller so imaginary part remains zero.
	 */
	while (prev < prev_end) {
		*d = *prev++;
		d += fft_elem_inc;
	}

	/* Copy hop size of new data from circular buffer */
	for (copied = 0; copied < fft->fft_hop_size; copied += n) {
		nmax = fft->fft_hop_size - copied;
		n = mfcc_buffer_samples_without_wrap(buf, r);
		n = MIN(n, nmax);
		for (j = 0; j < n; j++) {
			*d = *r++;
			d += fft_elem_inc;
		}
		r = mfcc_buffer_wrap(buf, r);
	}

	buf->s_avail -= copied;
	buf->s_free += copied;
	buf->r_ptr = r;

	/* Copy for next time data back to overlap buffer */
	d = (int32_t *)&fft->fft_buf[fft->fft_fill_start_idx + fft->fft_hop_size].real;
	prev = state->prev_data;
	while (prev < prev_end) {
		*prev++ = *d;
		d += fft_elem_inc;
	}
}


