// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2026 Intel Corporation.
//
// Author: Seppo Ingalsuo <seppo.ingalsuo@linux.intel.com>

/**
 * \file mfcc_vad.c
 * \brief Voice Activity Detection based on Mel spectrum energy.
 *
 * Implements a VAD that tracks per-bin noise floor and computes a
 * speech-frequency weighted energy above the floor. Speech is declared
 * when the weighted delta exceeds a threshold, with hangover to prevent
 * rapid toggling.
 */

#include <sof/audio/mfcc/mfcc_vad.h>

#ifdef CONFIG_COMP_MFCC_VAD

#include <sof/audio/component.h>
#include <sof/audio/format.h>
#include <sof/audio/module_adapter/module/module_interface.h>
#include <sof/math/auditory.h>
#include <sof/trace/trace.h>
#include <errno.h>
#include <stddef.h>

LOG_MODULE_DECLARE(mfcc, CONFIG_SOF_LOG_LEVEL);

/**
 * \brief A-weighting table: 1/3 octave band center frequencies in Hz (Q16.0).
 *
 * From IEC 61672-1:2013, source:
 * https://acousticalengineer.com/a-weighting-table/
 */
#define A_WEIGHT_TABLE_SIZE	36

static const int16_t a_weight_hz[A_WEIGHT_TABLE_SIZE] = {
	    6,     8,    10,    13,    16,    20,    25,    32,
	   40,    50,    63,    80,   100,   125,   160,   200,
	  250,   315,   400,   500,   630,   800,  1000,  1250,
	 1600,  2000,  2500,  3150,  4000,  5000,  6300,  8000,
	10000, 12500, 16000, 20000,
};

/**
 * \brief A-weighting linear amplitude, scaled so peak (at 2500 Hz) maps
 *        to INT16_MAX (32767).  Original dB values converted via
 *        10^(dB/20) then scaled by 32767 / max.
 */
static const int16_t a_weight_lin[A_WEIGHT_TABLE_SIZE] = {
	    2,     4,     9,    19,    43,    85,   162,   299,
	  531,   862,  1382,  2140,  3129,  4370,  6172,  8136,
	10362, 13196, 16234, 19518, 22669, 25730, 28212, 30230,
	31655, 32392, 32767, 32392, 31655, 30230, 27889, 24856,
	21156, 17196, 13045,  9670,
};

/**
 * \brief Compute A-weighted speech-frequency emphasis weights for Mel bins.
 *
 * Weights are computed by linearly interpolating the A-weighting table
 * at each Mel bin center frequency.  Output weights are in Q1.15 and
 * sum to approximately 2^15.
 *
 * \param[out] weights Output weight array.
 * \param[in] num_mel Number of Mel bins.
 * \param[in] sample_rate Sample rate in Hz.
 */
static void mfcc_vad_build_weights(int16_t *weights, int num_mel, int sample_rate)
{
	int16_t mel_end = psy_hz_to_mel((int16_t)(sample_rate / 2));
	int16_t mel_step = mel_end / (num_mel + 1);
	int32_t sum = 0;
	int i;

	for (i = 0; i < num_mel; i++) {
		int16_t f_hz = psy_mel_to_hz((int16_t)((i + 1) * mel_step));
		int16_t w;
		int j;

		/* Find the table interval containing f_hz and interpolate */
		if (f_hz <= a_weight_hz[0]) {
			w = a_weight_lin[0];
		} else if (f_hz >= a_weight_hz[A_WEIGHT_TABLE_SIZE - 1]) {
			w = a_weight_lin[A_WEIGHT_TABLE_SIZE - 1];
		} else {
			/* Find j such that a_weight_hz[j] <= f_hz < a_weight_hz[j+1] */
			for (j = 0; j < A_WEIGHT_TABLE_SIZE - 2; j++) {
				if (f_hz < a_weight_hz[j + 1])
					break;
			}

			/* Linear interpolation:
			 * w = w0 + (w1 - w0) * (f - f0) / (f1 - f0)
			 */
			int16_t f0 = a_weight_hz[j];
			int16_t f1 = a_weight_hz[j + 1];
			int16_t w0 = a_weight_lin[j];
			int16_t w1 = a_weight_lin[j + 1];
			int32_t num = (int32_t)(w1 - w0) * (f_hz - f0);
			int16_t den = f1 - f0;

			w = w0 + (int16_t)(num / den);
		}

		weights[i] = w;
		sum += w;
	}

	/* Normalize weights so they sum to ~2^15. */
	if (sum > 0) {
		for (i = 0; i < num_mel; i++) {
			int32_t scaled = ((int32_t)weights[i] << 15) / sum;

			weights[i] = (int16_t)scaled;
		}
	}
}

int mfcc_vad_init(struct mfcc_vad_state *vad, int num_mel_bins, int sample_rate,
		  struct processing_module *mod)
{
	int i;

	if (!vad)
		return -EINVAL;

	if (num_mel_bins <= 0)
		return -EINVAL;

	vad->num_mel_bins = num_mel_bins;
	vad->energy_threshold = MFCC_VAD_ENERGY_THRESHOLD;
	vad->noise_rise_alpha_slow = MFCC_VAD_NOISE_RISE_ALPHA;
	vad->noise_rise_alpha_fast = MFCC_VAD_NOISE_RISE_ALPHA_FAST;
	vad->hangover_max = MFCC_VAD_HANGOVER_FRAMES;
	vad->hangover_counter = 0;
	vad->init_frames = MFCC_VAD_NOISE_INIT_FRAMES;
	vad->frame_count = 0;
	vad->is_speech = false;
	vad->initialized = false;

	/* Allocate per-bin noise floor */
	vad->noise_floor = mod_zalloc(mod, num_mel_bins * sizeof(int32_t));
	if (!vad->noise_floor)
		return -ENOMEM;

	/* Allocate and compute per-bin weights */
	vad->weights = mod_zalloc(mod, num_mel_bins * sizeof(int16_t));
	if (!vad->weights)
		return -ENOMEM;

	for (i = 0; i < num_mel_bins; i++)
		vad->noise_floor[i] = 0;

	mfcc_vad_build_weights(vad->weights, num_mel_bins, sample_rate);
	return 0;
}

int mfcc_vad_update(struct mfcc_vad_state *vad, const int32_t *mel_log)
{
	int64_t energy_delta = 0;
	int32_t delta;
	int16_t alpha;
	bool raw_speech;
	int i;

	if (!vad || !mel_log)
		return 0;

	vad->frame_count++;

	/* Initialize noise floor to first frame */
	if (!vad->initialized) {
		for (i = 0; i < vad->num_mel_bins; i++)
			vad->noise_floor[i] = mel_log[i];

		vad->initialized = true;
	}

	/* Select rise alpha based on convergence phase */
	if (vad->frame_count <= vad->init_frames)
		alpha = vad->noise_rise_alpha_fast;
	else
		alpha = vad->noise_rise_alpha_slow;

	/* Update noise floor: follow down instantly, rise slowly */
	for (i = 0; i < vad->num_mel_bins; i++) {
		if (mel_log[i] < vad->noise_floor[i]) {
			/* Instant follow-down */
			vad->noise_floor[i] = mel_log[i];
		} else {
			/* Slow rise: floor += alpha * (mel - floor)
			 * Q9.23 + Q1.15 * Q9.23 => need Q9.23 result
			 * alpha is Q1.15, delta is Q9.23
			 * product is Q10.38, shift right by 15 to get Q9.23
			 */
			delta = mel_log[i] - vad->noise_floor[i];
			vad->noise_floor[i] += (int32_t)(((int64_t)alpha * delta) >> 15);
		}
	}

	/* Compute weighted energy delta above noise floor.
	 * energy_delta = sum(weights[i] * (mel[i] - noise_floor[i]))
	 * weights are Q1.15, mel delta is Q9.23
	 * Product is Q10.38, accumulate in int64_t then shift to Q9.23
	 */
	for (i = 0; i < vad->num_mel_bins; i++) {
		delta = mel_log[i] - vad->noise_floor[i];
		if (delta > 0)
			energy_delta += (int64_t)vad->weights[i] * delta;
	}

	/* Shift accumulated energy from Q10.38 to Q9.23: shift right by 15 */
	energy_delta >>= 15;

	/* VAD decision with hangover */
	raw_speech = (int32_t)energy_delta > vad->energy_threshold;

	if (raw_speech) {
		vad->hangover_counter = vad->hangover_max;
		vad->is_speech = true;
	} else {
		if (vad->hangover_counter > 0) {
			vad->hangover_counter--;
			vad->is_speech = true;
		} else {
			vad->is_speech = false;
		}
	}

	return vad->is_speech ? 1 : 0;
}

void mfcc_vad_reset(struct mfcc_vad_state *vad)
{
	int i;

	if (!vad)
		return;

	vad->frame_count = 0;
	vad->hangover_counter = 0;
	vad->is_speech = false;
	vad->initialized = false;

	for (i = 0; i < vad->num_mel_bins; i++)
		vad->noise_floor[i] = 0;
}

#endif /* CONFIG_COMP_MFCC_VAD */
