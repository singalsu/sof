// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2025 Intel Corporation.

#include <sof/audio/component.h>
#include <sof/audio/audio_stream.h>
#include <sof/math/auditory.h>
#include <sof/math/matrix.h>
#include <sof/math/sqrt.h>
#include <sof/math/trig.h>
#include <sof/math/window.h>
#include <sof/trace/trace.h>

#include "stft_process.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

LOG_MODULE_REGISTER(stft_process_common, CONFIG_SOF_LOG_LEVEL);

/*
 * The main processing function for STFT_PROCESS
 */

static int stft_prepare_fft(struct stft_process_state *state)
{
	struct stft_process_buffer *ibuf = &state->ibuf;
	struct stft_process_fft *fft = &state->fft;
	int num_fft;

	/* Phase 1, wait until whole fft_size is filled with valid data. This way
	 * first output cepstral coefficients originate from streamed data and not
	 * from buffers with zero data.
	 */
	if (state->waiting_fill) {
		if (ibuf->s_avail < fft->fft_size)
			return 0;

		state->waiting_fill = false;
	}

	/* Phase 2, move first prev_size data to previous data buffer, remove
	 * samples from input buffer.
	 */
	if (!state->prev_samples_valid) {
		stft_process_fill_prev_samples(ibuf, state->prev_data, state->prev_data_size);
		state->prev_samples_valid = true;
	}

	/* Check if enough samples in buffer for FFT hop */
	num_fft = ibuf->s_avail / fft->fft_hop_size;
	return num_fft;
}

static void stft_do_fft(struct stft_process_state *state)
{
	struct stft_process_fft *fft = &state->fft;
	int input_shift = 0;

	/* Clear FFT input buffer because it has been used as scratch */
	bzero(fft->fft_buf, fft->fft_buffer_size);

	/* Copy data to FFT input buffer from overlap buffer and from new samples buffer */
	stft_process_fill_fft_buffer(state);

	/* TODO: remove_dc_offset */

	/* TODO: use_energy & raw_energy */

	/* Window function */
	stft_process_apply_window(state, input_shift);

	/* TODO: use_energy & !raw_energy */

	/* The FFT out buffer needs to be cleared to avoid to corrupt
	 * the output. TODO: check moving it to FFT lib.
	 */
	bzero(fft->fft_out, fft->fft_buffer_size);

	/* Compute FFT */
	fft_execute_32(fft->fft_plan, false);
}

static void stft_do_ifft(struct stft_process_state *state)
{
	struct stft_process_fft *fft = &state->fft;
	int input_shift = 0;
	int bin = 50;

	/* The FFT out buffer needs to be cleared to avoid to corrupt
	 * the output. TODO: check moving it to FFT lib.
	 */
	bzero(fft->fft_buf, fft->fft_buffer_size);
	bzero(fft->fft_out, fft->fft_buffer_size);
	fft->fft_out[bin - 1].real = 10000;
	fft->fft_out[fft->fft_size + 2 - bin - 1].real = 10000;

	/* Compute IFFT */
	fft_execute_32(fft->ifft_plan, true);

	/* Window function */
	stft_process_apply_window(state, input_shift);

	/* Copy to output buffer */
	stft_process_overlap_add_ifft_buffer(state);
}

#if CONFIG_FORMAT_S16LE

static int stft_process_output_zeros_s16(struct stft_comp_data *cd, struct sof_sink *sink,
					 int frames)
{
	int16_t *y, *y_start, *y_end;
	int samples_without_wrap;
	int bytes_without_wrap;
	int y_size;
	int ret;
	int samples = frames * cd->channels;
	size_t bytes = samples * sizeof(int16_t);

	/* Get pointer to sink data in circular buffer, buffer start and size. */
	ret = sink_get_buffer_s16(sink, bytes, &y, &y_start, &y_size);
	if (ret)
		return ret;

	/* Set helper pointers to buffer end for wrap check. Then loop until all
	 * samples are processed.
	 */
	y_end = y_start + y_size;
	while (samples) {
		/* Find out samples to process before first wrap or end of data. */
		samples_without_wrap = y_end - y;
		samples_without_wrap = MIN(samples_without_wrap, samples);
		bytes_without_wrap = samples_without_wrap * sizeof(int16_t);
		bzero(y, bytes_without_wrap);
		y += bytes_without_wrap;

		/* Check for wrap */
		y = (y >= y_end) ? y - y_size : y;

		/* Update processed samples count for next loop iteration. */
		samples -= samples_without_wrap;
	}

	/* Update the source and sink for bytes consumed and produced. Return success. */
	sink_commit_buffer(sink, bytes);
	return 0;
}

static int stft_process_s16(const struct processing_module *mod, struct sof_source *source,
			    struct sof_sink *sink, uint32_t frames)
{
	struct stft_comp_data *cd = module_get_private_data(mod);
	struct stft_process_state *state = &cd->state;
	int num_fft;
	int i;

	/* Get samples from source buffer */
	stft_process_source_s16(cd, source, frames);

	/* Run STFT and processing after FFT: Mel auditory filter and DCT. The sink
	 * buffer is updated during STDF processing.
	 */
	num_fft = stft_prepare_fft(state);
	comp_info(mod->dev, "num_fft = %d", num_fft);

	for (i = 0; i < num_fft; i++) {
		stft_do_fft(state);

		/* stft_process(state) */

		stft_do_ifft(state);
		cd->fft_done = true;
	}

	/* Get samples from source buffer */
	if (cd->fft_done)
		stft_process_sink_s16(cd, sink, frames);
	else
		stft_process_output_zeros_s16(cd, sink, frames);

	return 0;
}
#endif /* CONFIG_FORMAT_S16LE */

#if CONFIG_FORMAT_S24LE
#endif /* CONFIG_FORMAT_S24LE */

#if CONFIG_FORMAT_S32LE
#endif /* CONFIG_FORMAT_S32LE */

/* This struct array defines the used processing functions for
 * the PCM formats
 */
const struct stft_process_proc_fnmap stft_process_functions[] = {
#if CONFIG_FORMAT_S16LE
	{ SOF_IPC_FRAME_S16_LE, stft_process_s16 },
#endif
};

/**
 * stft_process_find_proc_func() - Find suitable processing function.
 * @src_fmt: Enum value for PCM format.
 *
 * This function finds the suitable processing function to use for
 * the used PCM format. If not found, return NULL.
 *
 * Return: Pointer to processing function for the requested PCM format.
 */
stft_process_func stft_process_find_proc_func(enum sof_ipc_frame src_fmt)
{
	int i;

	/* Find suitable processing function from map */
	for (i = 0; i < ARRAY_SIZE(stft_process_functions); i++)
		if (src_fmt == stft_process_functions[i].frame_fmt)
			return stft_process_functions[i].stft_process_function;

	return NULL;
}
