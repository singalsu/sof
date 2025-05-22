// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2022-2025 Intel Corporation.
//
// Author: Bartosz Kokoszko <bartoszx.kokoszko@intel.com>
//         Seppo Ingalsuo <seppo.ingalsuo@linux.intel.com>

#include <sof/audio/format.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include "up_down_mixer.h"

#if SOF_USE_HIFI(NONE, UP_DOWN_MIXER)

void upmix32bit_1_to_5_1(struct up_down_mixer_data *cd, const uint8_t * const in_data,
			 const uint32_t in_size, uint8_t * const out_data)
{
	int i;

	channel_map out_channel_map = cd->out_channel_map;

	/* Only load the channel if it's present. */
	int32_t *output_left = (int32_t *)(out_data +
		(get_channel_location(out_channel_map, CHANNEL_LEFT) << 2));
	int32_t *output_center = (int32_t *)(out_data +
		(get_channel_location(out_channel_map, CHANNEL_CENTER) << 2));
	int32_t *output_right = (int32_t *)(out_data +
		(get_channel_location(out_channel_map, CHANNEL_RIGHT) << 2));
	int32_t *output_left_surround = (int32_t *)(out_data +
		(get_channel_location(out_channel_map, CHANNEL_LEFT_SURROUND) << 2));
	int32_t *output_right_surround = (int32_t *)(out_data +
		(get_channel_location(out_channel_map, CHANNEL_RIGHT_SURROUND) << 2));
	int32_t *output_lfe = (int32_t *)(out_data +
		(get_channel_location(out_channel_map, CHANNEL_LFE) << 2));

	int32_t *in_ptr = (int32_t *)in_data;

	for (i = 0; i < (in_size >> 2); ++i) {
		output_left[i * 6] = in_ptr[i];
		output_right[i * 6] = in_ptr[i];
		output_center[i * 6] = 0;
		output_left_surround[i * 6] = in_ptr[i];
		output_right_surround[i * 6] = in_ptr[i];
		output_lfe[i * 6] = 0;
	}
}

void upmix16bit_1_to_5_1(struct up_down_mixer_data *cd, const uint8_t * const in_data,
			 const uint32_t in_size, uint8_t * const out_data)
{
	int i;

	channel_map out_channel_map = cd->out_channel_map;

	/* Only load the channel if it's present. */
	int32_t *output_left = (int32_t *)(out_data +
		(get_channel_location(out_channel_map, CHANNEL_LEFT) << 2));
	int32_t *output_center = (int32_t *)(out_data +
		(get_channel_location(out_channel_map, CHANNEL_CENTER) << 2));
	int32_t *output_right = (int32_t *)(out_data +
		(get_channel_location(out_channel_map, CHANNEL_RIGHT) << 2));
	int32_t *output_left_surround = (int32_t *)(out_data +
		(get_channel_location(out_channel_map, CHANNEL_LEFT_SURROUND) << 2));
	int32_t *output_right_surround = (int32_t *)(out_data +
		(get_channel_location(out_channel_map, CHANNEL_RIGHT_SURROUND) << 2));
	int32_t *output_lfe = (int32_t *)(out_data +
		(get_channel_location(out_channel_map, CHANNEL_LFE) << 2));

	int16_t *in_ptr = (int16_t *)in_data;

	for (i = 0; i < (in_size >> 1); ++i) {
		output_left[i * 6] = (int32_t)in_ptr[i] << 16;
		output_right[i * 6] = (int32_t)in_ptr[i] << 16;
		output_center[i * 6] = 0;
		output_left_surround[i * 6] = (int32_t)in_ptr[i] << 16;
		output_right_surround[i * 6] = (int32_t)in_ptr[i] << 16;
		output_lfe[i * 6] = 0;
	}
}

void upmix32bit_2_0_to_5_1(struct up_down_mixer_data *cd, const uint8_t * const in_data,
			   const uint32_t in_size, uint8_t * const out_data)
{
	int i;

	channel_map out_channel_map = cd->out_channel_map;

	const uint8_t left_slot = get_channel_location(out_channel_map, CHANNEL_LEFT);
	const uint8_t center_slot = get_channel_location(out_channel_map, CHANNEL_CENTER);
	const uint8_t right_slot = get_channel_location(out_channel_map, CHANNEL_RIGHT);
	uint8_t left_surround_slot = get_channel_location(out_channel_map, CHANNEL_LEFT_SURROUND);
	uint8_t right_surround_slot = get_channel_location(out_channel_map, CHANNEL_RIGHT_SURROUND);
	const uint8_t lfe_slot = get_channel_location(out_channel_map, CHANNEL_LFE);

	/* Must support also 5.1 Surround */
	if (left_surround_slot == CHANNEL_INVALID && right_surround_slot == CHANNEL_INVALID) {
		left_surround_slot = get_channel_location(out_channel_map, CHANNEL_LEFT_SIDE);
		right_surround_slot = get_channel_location(out_channel_map, CHANNEL_RIGHT_SIDE);
	}

	int32_t *output_left = (int32_t *)(out_data + (left_slot << 2));
	int32_t *output_center = (int32_t *)(out_data + (center_slot << 2));
	int32_t *output_right = (int32_t *)(out_data + (right_slot << 2));
	int32_t *output_left_surround = (int32_t *)(out_data + (left_surround_slot << 2));
	int32_t *output_right_surround = (int32_t *)(out_data + (right_surround_slot << 2));
	int32_t *output_lfe = (int32_t *)(out_data + (lfe_slot << 2));

	int32_t *in_left_ptr = (int32_t *)in_data;
	int32_t *in_right_ptr = (int32_t *)(in_data + 4);

	for (i = 0; i < (in_size >> 3); ++i) {
		output_left[i * 6] = in_left_ptr[i * 2];
		output_right[i * 6] = in_right_ptr[i * 2];
		output_center[i * 6] = 0;
		output_left_surround[i * 6] = in_left_ptr[i * 2];
		output_right_surround[i * 6] = in_right_ptr[i * 2];
		output_lfe[i * 6] = 0;
	}
}

void upmix16bit_2_0_to_5_1(struct up_down_mixer_data *cd, const uint8_t * const in_data,
			   const uint32_t in_size, uint8_t * const out_data)
{
	int i;

	channel_map out_channel_map = cd->out_channel_map;

	const uint8_t left_slot = get_channel_location(out_channel_map, CHANNEL_LEFT);
	const uint8_t center_slot = get_channel_location(out_channel_map, CHANNEL_CENTER);
	const uint8_t right_slot = get_channel_location(out_channel_map, CHANNEL_RIGHT);
	uint8_t left_surround_slot = get_channel_location(out_channel_map, CHANNEL_LEFT_SURROUND);
	uint8_t right_surround_slot = get_channel_location(out_channel_map, CHANNEL_RIGHT_SURROUND);
	const uint8_t lfe_slot = get_channel_location(out_channel_map, CHANNEL_LFE);

	/* Must support also 5.1 Surround */
	if (left_surround_slot == CHANNEL_INVALID && right_surround_slot == CHANNEL_INVALID) {
		left_surround_slot = get_channel_location(out_channel_map, CHANNEL_LEFT_SIDE);
		right_surround_slot = get_channel_location(out_channel_map, CHANNEL_RIGHT_SIDE);
	}

	int32_t *output_left = (int32_t *)(out_data + (left_slot << 2));
	int32_t *output_center = (int32_t *)(out_data + (center_slot << 2));
	int32_t *output_right = (int32_t *)(out_data + (right_slot << 2));
	int32_t *output_left_surround = (int32_t *)(out_data + (left_surround_slot << 2));
	int32_t *output_right_surround = (int32_t *)(out_data + (right_surround_slot << 2));
	int32_t *output_lfe = (int32_t *)(out_data + (lfe_slot << 2));

	int16_t *in_left_ptr = (int16_t *)in_data;
	int16_t *in_right_ptr = (int16_t *)(in_data + 2);

	for (i = 0; i < (in_size >> 2); ++i) {
		output_left[i * 6] = (int32_t)in_left_ptr[i * 2] << 16;
		output_right[i * 6] = (int32_t)in_right_ptr[i * 2] << 16;
		output_center[i * 6] = 0;
		output_left_surround[i * 6] = (int32_t)in_left_ptr[i * 2] << 16;
		output_right_surround[i * 6] = (int32_t)in_right_ptr[i * 2] << 16;
		output_lfe[i * 6] = 0;
	}
}

void upmix32bit_2_0_to_7_1(struct up_down_mixer_data *cd, const uint8_t * const in_data,
			   const uint32_t in_size, uint8_t * const out_data)
{
	int i;

	channel_map out_channel_map = cd->out_channel_map;

	/* Only load the channel if it's present. */
	int32_t *output_left = (int32_t *)(out_data +
		(get_channel_location(out_channel_map, CHANNEL_LEFT) << 2));
	int32_t *output_center = (int32_t *)(out_data +
		(get_channel_location(out_channel_map, CHANNEL_CENTER) << 2));
	int32_t *output_right = (int32_t *)(out_data +
		(get_channel_location(out_channel_map, CHANNEL_RIGHT) << 2));
	int32_t *output_left_surround = (int32_t *)(out_data +
		(get_channel_location(out_channel_map, CHANNEL_LEFT_SURROUND) << 2));
	int32_t *output_right_surround = (int32_t *)(out_data +
		(get_channel_location(out_channel_map, CHANNEL_RIGHT_SURROUND) << 2));
	int32_t *output_lfe = (int32_t *)(out_data +
		(get_channel_location(out_channel_map, CHANNEL_LFE) << 2));
	int32_t *output_left_side = (int32_t *)(out_data +
		(get_channel_location(out_channel_map, CHANNEL_LEFT_SIDE) << 2));
	int32_t *output_right_side = (int32_t *)(out_data +
		(get_channel_location(out_channel_map, CHANNEL_RIGHT_SIDE) << 2));

	int32_t *in_left_ptr = (int32_t *)in_data;
	int32_t *in_right_ptr = (int32_t *)(in_data + 4);

	for (i = 0; i < (in_size >> 3); ++i) {
		output_left[i * 8] = in_left_ptr[i * 2];
		output_right[i * 8] = in_right_ptr[i * 2];
		output_center[i * 8] = 0;
		output_left_surround[i * 8] = in_left_ptr[i * 2];
		output_right_surround[i * 8] = in_right_ptr[i * 2];
		output_lfe[i * 8] = 0;
		output_left_side[i * 8] = 0;
		output_right_side[i * 8] = 0;
	}
}

void shiftcopy32bit_mono(struct up_down_mixer_data *cd, const uint8_t * const in_data,
			 const uint32_t in_size, uint8_t * const out_data)
{
	int i;

	uint32_t *in_ptr = (uint32_t *)in_data;
	uint64_t *out_ptr = (uint64_t *)out_data;

	for (i = 0; i < (in_size >> 2); ++i)
		out_ptr[i] = ((uint64_t)in_ptr[i] << 32) | in_ptr[i];
}

void shiftcopy32bit_stereo(struct up_down_mixer_data *cd, const uint8_t * const in_data,
			   const uint32_t in_size, uint8_t * const out_data)
{
	int i;

	int64_t *in_ptr = (int64_t *)in_data;
	int64_t *out_ptr = (int64_t *)out_data;

	for (i = 0; i < (in_size >> 3); ++i)
		out_ptr[i] = in_ptr[i];
}

void downmix32bit_2_1(struct up_down_mixer_data *cd, const uint8_t * const in_data,
		      const uint32_t in_size, uint8_t * const out_data)
{
	int64_t Q_tmp_left;
	int64_t Q_tmp_right;
	int32_t P_coefficient_left = cd->downmix_coefficients[CHANNEL_LEFT];
	int32_t P_coefficient_right = cd->downmix_coefficients[CHANNEL_RIGHT];
	int32_t P_coefficient_lfe = cd->downmix_coefficients[CHANNEL_LFE];
	int32_t *in32 = (int32_t *)in_data;
	const int32_t *input_left = in32 + get_channel_location(cd->in_channel_map, CHANNEL_LEFT);
	const int32_t *input_right = in32 + get_channel_location(cd->in_channel_map, CHANNEL_RIGHT);
	const int32_t *input_lfe = in32 + get_channel_location(cd->in_channel_map, CHANNEL_LFE);
	int32_t *output_left = (int32_t *)(out_data);
	int32_t *output_right = output_left + 1;
	const int32_t *const end_input_left = input_left + (in_size / (sizeof(int32_t)));

	while (input_left < end_input_left) {
		/* update output left and right channels based on input lfe channel,
		 * accumulate as Q1.31 * Q1.31 -> Q2.62
		 */
		Q_tmp_left = (int64_t)P_coefficient_lfe * *input_lfe;
		Q_tmp_right = Q_tmp_left;
		input_lfe += 3;

		/* update output left channel based on input left channel */
		Q_tmp_left += (int64_t)P_coefficient_left * *input_left;
		input_left += 3;

		/* update output right channel based on input right channel */
		Q_tmp_right += (int64_t)P_coefficient_right * *input_right;
		input_right += 6;

		/* Shift with round to Q1.31 and saturate and store output */
		*output_left = sat_int32(Q_SHIFT_RND(Q_tmp_left, 62, 31));
		*output_right = sat_int32(Q_SHIFT_RND(Q_tmp_right, 62, 31));
		output_left += 2;
		output_right += 2;
	}
}

void downmix32bit_3_0(struct up_down_mixer_data *cd, const uint8_t * const in_data,
		      const uint32_t in_size, uint8_t * const out_data)
{
	int64_t Q_tmp_left;
	int64_t Q_tmp_right;
	int32_t P_coefficient_left = cd->downmix_coefficients[CHANNEL_LEFT];
	int32_t P_coefficient_center = cd->downmix_coefficients[CHANNEL_CENTER];
	int32_t P_coefficient_right = cd->downmix_coefficients[CHANNEL_RIGHT];
	int32_t *in32 = (int32_t *)in_data;
	const int32_t *input_left = in32 + get_channel_location(cd->in_channel_map, CHANNEL_LEFT);
	const int32_t *input_center = in32 + get_channel_location(cd->in_channel_map, CHANNEL_CENTER);
	const int32_t *input_right = in32 + get_channel_location(cd->in_channel_map, CHANNEL_RIGHT);
	int32_t *output_left = (int32_t *)(out_data);
	int32_t *output_right = output_left + 1;
	const int32_t *const end_input_left = input_left + (in_size / (sizeof(int32_t)));

	while (input_left < end_input_left) {
		/* update output left and right channels based on input center channel,
		 * accumulate as Q1.31 * Q1.31 -> Q2.62
		 */
		Q_tmp_left = (int64_t)P_coefficient_center * *input_center;
		Q_tmp_right = Q_tmp_left;
		input_center += 3;

		/* update output left channel based on input left channel */
		Q_tmp_left += (int64_t)P_coefficient_left * *input_left;
		input_left += 3;

		/* update output right channel based on input right channel */
		Q_tmp_right += (int64_t)P_coefficient_right * *input_right;
		input_right += 3;

		/* Shift with round to Q1.31 and saturate and store output */
		*output_left = sat_int32(Q_SHIFT_RND(Q_tmp_left, 62, 31));
		*output_right = sat_int32(Q_SHIFT_RND(Q_tmp_right, 62, 31));
		output_left += 2;
		output_right += 2;
	}
}

void downmix32bit_3_1(struct up_down_mixer_data *cd, const uint8_t * const in_data,
		      const uint32_t in_size, uint8_t * const out_data)
{
	int64_t Q_tmp_left;
	int64_t Q_tmp_right;
	int32_t P_coefficient_left = cd->downmix_coefficients[CHANNEL_LEFT];
	int32_t P_coefficient_center = cd->downmix_coefficients[CHANNEL_CENTER];
	int32_t P_coefficient_right = cd->downmix_coefficients[CHANNEL_RIGHT];
	int32_t P_coefficient_lfe = cd->downmix_coefficients[CHANNEL_LFE];
	int32_t *in32 = (int32_t *)in_data;
	const int32_t *input_left = in32 + get_channel_location(cd->in_channel_map, CHANNEL_LEFT);
	const int32_t *input_center = in32 + get_channel_location(cd->in_channel_map, CHANNEL_CENTER);
	const int32_t *input_right = in32 + get_channel_location(cd->in_channel_map, CHANNEL_RIGHT);
	const int32_t *input_lfe = in32 + get_channel_location(cd->in_channel_map, CHANNEL_LFE);
	int32_t *output_left = (int32_t *)(out_data);
	int32_t *output_right = output_left + 1;
	const int32_t *const end_input_left = input_left + (in_size / (sizeof(int32_t)));

	while (input_left < end_input_left) {
		/* update output left and right channels based on input lfe channel,
		 * accumulate as Q1.31 * Q1.31 -> Q2.62
		 */
		Q_tmp_left = (int64_t)P_coefficient_center * *input_center;
		Q_tmp_left = (int64_t)P_coefficient_lfe * *input_lfe;
		Q_tmp_right = Q_tmp_left;
		input_center += 4;
		input_lfe += 4;

		/* update output left channel based on input left channel */
		Q_tmp_left += (int64_t)P_coefficient_left * *input_left;
		input_left += 4;

		/* update output right channel based on input right channel */
		Q_tmp_right += (int64_t)P_coefficient_right * *input_right;
		input_right += 4;

		/* Shift with round to Q1.31 and saturate and store output */
		*output_left = sat_int32(Q_SHIFT_RND(Q_tmp_left, 62, 31));
		*output_right = sat_int32(Q_SHIFT_RND(Q_tmp_right, 62, 31));
		output_left += 2;
		output_right += 2;
	}
}

void downmix32bit(struct up_down_mixer_data *cd, const uint8_t * const in_data,
		  const uint32_t in_size, uint8_t * const out_data)
{
	int64_t Q_tmp_left = 0;
	int64_t Q_tmp_right = 0;
	int64_t p;
	int32_t P_coefficient_left = cd->downmix_coefficients[CHANNEL_LEFT];
	int32_t P_coefficient_center = cd->downmix_coefficients[CHANNEL_CENTER];
	int32_t P_coefficient_right = cd->downmix_coefficients[CHANNEL_RIGHT];
	int32_t P_coefficient_left_surround = cd->downmix_coefficients[CHANNEL_LEFT_SURROUND];
	int32_t P_coefficient_right_surround = cd->downmix_coefficients[CHANNEL_RIGHT_SURROUND];
	int32_t P_coefficient_lfe = cd->downmix_coefficients[CHANNEL_LFE];
	int32_t *input_left;
	int32_t *input_center;
	int32_t *input_right;
	int32_t *input_left_surround;
	int32_t *input_right_surround;
	int32_t *input_lfe;
	int32_t *in32 = (int32_t *)in_data;
	int32_t *output_left = (int32_t *)out_data;
	int32_t *output_right = output_left + 1;

	/* See what channels are available. */
	bool left = (get_channel_location(cd->in_channel_map, CHANNEL_LEFT) != 0xF);
	bool center = (get_channel_location(cd->in_channel_map, CHANNEL_CENTER) != 0xF);
	bool right = (get_channel_location(cd->in_channel_map, CHANNEL_RIGHT) != 0xF);
	bool left_surround =
		(get_channel_location(cd->in_channel_map, CHANNEL_LEFT_SURROUND) != 0xF);
	bool right_surround =
		(get_channel_location(cd->in_channel_map, CHANNEL_RIGHT_SURROUND) != 0xF);
	bool lfe = (get_channel_location(cd->in_channel_map, CHANNEL_LFE) != 0xF);

	/** Calculate number of samples in a single channel. */
	int number_of_samples_in_one_channel = in_size / cd->in_channel_no >> 2;

	int sample_offset = cd->in_channel_no;
	int i;

	/* No check for bool above. If false address becomes in32 + 0xf, but it is not
	 * used in the processing loop.
	 */
	input_left = in32 + get_channel_location(cd->in_channel_map, CHANNEL_LEFT);
	input_center = in32 + get_channel_location(cd->in_channel_map, CHANNEL_CENTER);
	input_right = in32 + get_channel_location(cd->in_channel_map, CHANNEL_RIGHT);
	input_left_surround = in32 + get_channel_location(cd->in_channel_map,
							  CHANNEL_LEFT_SURROUND);
	input_right_surround = in32 + get_channel_location(cd->in_channel_map,
							   CHANNEL_RIGHT_SURROUND);
	input_lfe = in32 + get_channel_location(cd->in_channel_map, CHANNEL_LFE);

	for (i = 0; i < number_of_samples_in_one_channel; i++) {
		if (left) {
			Q_tmp_left += (int64_t)P_coefficient_left * *input_left;
			input_left += sample_offset;
		}
		if (center) {
			p = (int64_t)P_coefficient_center * *input_center;
			Q_tmp_left += p;
			Q_tmp_right += p;
			input_center += sample_offset;
		}
		if (right) {
			Q_tmp_right += (int64_t)P_coefficient_right * *input_right;
			input_right += sample_offset;
		}
		if (left_surround) {
			p = (int64_t)P_coefficient_left_surround * *input_left_surround;
			Q_tmp_left += p;
			input_left_surround += sample_offset;
			if (cd->in_channel_config == IPC4_CHANNEL_CONFIG_4_POINT_0)
				Q_tmp_right += p;
		}
		if (right_surround) {
			Q_tmp_right += (int64_t)P_coefficient_right_surround *
				*input_right_surround;
			input_right_surround += sample_offset;
		}
		if (lfe) {
			p = (int64_t)P_coefficient_lfe * *input_lfe;
			Q_tmp_left += p;
			Q_tmp_right += p;
			input_lfe += sample_offset;
		}

		/* Shift with round to Q1.31 and saturate and store output */
		*output_left = sat_int32(Q_SHIFT_RND(Q_tmp_left, 62, 31));
		*output_right = sat_int32(Q_SHIFT_RND(Q_tmp_right, 62, 31));
		output_left += 2;
		output_right += 2;
	}
}

void downmix32bit_4_0(struct up_down_mixer_data *cd, const uint8_t * const in_data,
		      const uint32_t in_size, uint8_t * const out_data)
{
	int64_t Q_tmp_left;
	int64_t Q_tmp_right;
	int64_t p;
	int32_t P_coefficient_left = cd->downmix_coefficients[CHANNEL_LEFT];
	int32_t P_coefficient_center = cd->downmix_coefficients[CHANNEL_CENTER];
	int32_t P_coefficient_right = cd->downmix_coefficients[CHANNEL_RIGHT];
	int32_t P_coefficient_left_surround = cd->downmix_coefficients[CHANNEL_LEFT_SURROUND];
	//int32_t P_coefficient_right_surround = cd->downmix_coefficients[CHANNEL_RIGHT_SURROUND];
	//int32_t P_coefficient_lfe = cd->downmix_coefficients[CHANNEL_LFE];

	int32_t *in32 = (int32_t *)in_data;

	const int32_t *input_left = in32 + get_channel_location(cd->in_channel_map, CHANNEL_LEFT);
	const int32_t *input_center = in32 +
		get_channel_location(cd->in_channel_map, CHANNEL_CENTER);
	const int32_t *input_right = in32 + get_channel_location(cd->in_channel_map, CHANNEL_RIGHT);
	const int32_t *input_left_surround = in32 +
		get_channel_location(cd->in_channel_map, CHANNEL_LEFT_SURROUND);

	int32_t *output_left = (int32_t *)(out_data);
	int32_t *output_right = output_left + 1;

	const int32_t *const end_input_left = input_left + (in_size / (sizeof(int32_t)));

	while (input_left < end_input_left) {
		/* Accumulate as Q1.31 * Q1.31 -> Q2.62 */
		Q_tmp_left = (int64_t)P_coefficient_left * *input_left;
		input_left += 4;

		p = (int64_t)P_coefficient_center * *input_center;
		Q_tmp_left += p;
		Q_tmp_right = p;
		input_center += 4;

		Q_tmp_right += (int64_t)P_coefficient_right * *input_right;
		input_right += 4;

		/* for 4.0 left surround if propagated to both left and right output channels */
		p += (int64_t)P_coefficient_left_surround * *input_left_surround;
		Q_tmp_left += p;
		Q_tmp_right = p;
		input_left_surround += 4;

		/* Shift with round to Q1.31 and saturate and store output */
		*output_left = sat_int32(Q_SHIFT_RND(Q_tmp_left, 62, 31));
		*output_right = sat_int32(Q_SHIFT_RND(Q_tmp_right, 62, 31));
		output_left += 2;
		output_right += 2;
	}
}

void downmix32bit_5_0_mono(struct up_down_mixer_data *cd, const uint8_t * const in_data,
			   const uint32_t in_size, uint8_t * const out_data)
{
	size_t i;

	int64_t Q_tmp;
	int32_t P_coefficient_left = cd->downmix_coefficients[CHANNEL_LEFT];
	int32_t P_coefficient_center = cd->downmix_coefficients[CHANNEL_CENTER];
	int32_t P_coefficient_right = cd->downmix_coefficients[CHANNEL_RIGHT];
	int32_t P_coefficient_cs = cd->downmix_coefficients[CHANNEL_CENTER_SURROUND];

	int32_t *in32 = (int32_t *)in_data;

	const int32_t *input_left = in32 + get_channel_location(cd->in_channel_map, CHANNEL_LEFT);
	const int32_t *input_center = in32 + get_channel_location(cd->in_channel_map, CHANNEL_CENTER);
	const int32_t *input_right = in32 + get_channel_location(cd->in_channel_map, CHANNEL_RIGHT);
	const int32_t *input_cs = in32 + get_channel_location(cd->in_channel_map, CHANNEL_CENTER_SURROUND);

	int32_t *output = (int32_t *)(out_data);

	const uint32_t channel_no = 5;

	for (i = 0; i < in_size / (sizeof(int32_t)); i += channel_no) {
		Q_tmp = (int64_t)P_coefficient_left * *input_left;
		input_left += 5;

		Q_tmp += (int64_t)P_coefficient_center * *input_center;
		input_center += 5;

		Q_tmp += (int64_t)P_coefficient_right * *input_right;
		input_right += 5;

		Q_tmp += (int64_t)P_coefficient_cs * *input_cs;
		input_right += 5;

		/* Shift with round to Q1.31 and saturate and store output */
		*output = sat_int32(Q_SHIFT_RND(Q_tmp, 62, 31));
		output++;
	}
}

void downmix32bit_5_1(struct up_down_mixer_data *cd, const uint8_t * const in_data,
		      const uint32_t in_size, uint8_t * const out_data)
{
	int64_t Q_tmp_left;
	int64_t Q_tmp_right;
	int32_t *in32 = (int32_t *)in_data;
	int32_t *input_left_surround;
	int32_t *input_right_surround;
	int32_t *output_left = (int32_t *)(out_data);
	int32_t *output_right = output_left + 1;
	int left_surround_slot =  get_channel_location(cd->in_channel_map, CHANNEL_LEFT_SURROUND);
	int right_surround_slot = get_channel_location(cd->in_channel_map, CHANNEL_RIGHT_SURROUND);

	/* Must support also 5.1 Surround */
	const bool surround_5_1_channel_map = (left_surround_slot == CHANNEL_INVALID) &&
					      (right_surround_slot == CHANNEL_INVALID);

	const int32_t *input_left = in32 + get_channel_location(cd->in_channel_map, CHANNEL_LEFT);
	const int32_t *input_center = in32 + get_channel_location(cd->in_channel_map, CHANNEL_CENTER);
	const int32_t *input_right = in32 + get_channel_location(cd->in_channel_map, CHANNEL_RIGHT);
	const int32_t *input_lfe = in32 +  get_channel_location(cd->in_channel_map, CHANNEL_LFE);

	if (surround_5_1_channel_map) {
		input_left_surround = in32 + get_channel_location(cd->in_channel_map, CHANNEL_LEFT_SIDE);
		input_right_surround = in32 + get_channel_location(cd->in_channel_map, CHANNEL_RIGHT_SIDE);
	} else {
		input_left_surround = in32 + left_surround_slot;
		input_right_surround = in32 + right_surround_slot;
	}

	/* Load the downmix coefficients. */
	int32_t P_coefficient_left = cd->downmix_coefficients[CHANNEL_LEFT];
	int32_t P_coefficient_center = cd->downmix_coefficients[CHANNEL_CENTER];
	int32_t P_coefficient_right = cd->downmix_coefficients[CHANNEL_RIGHT];
	int32_t P_coefficient_lfe = cd->downmix_coefficients[CHANNEL_LFE];
	int32_t P_coefficient_left_surround;
	int32_t P_coefficient_right_surround;

	if (surround_5_1_channel_map) {
		P_coefficient_left_surround  = cd->downmix_coefficients[CHANNEL_LEFT_SIDE];
		P_coefficient_right_surround = cd->downmix_coefficients[CHANNEL_RIGHT_SIDE];
	} else {
		P_coefficient_left_surround = cd->downmix_coefficients[CHANNEL_LEFT_SURROUND];
		P_coefficient_right_surround = cd->downmix_coefficients[CHANNEL_RIGHT_SURROUND];
	}


	const int32_t *const end_input_left = input_left + (in_size / (sizeof(int32_t)));

	while (input_left < end_input_left) {
		/* Accumulate as Q1.31 * Q1.31 -> Q2.62 */
		Q_tmp_left = (int64_t)P_coefficient_center * *input_center;
		input_center += 6;

		Q_tmp_left += (int64_t)P_coefficient_lfe * *input_lfe;
		input_lfe += 6;
		Q_tmp_right = Q_tmp_left;

		Q_tmp_left += (int64_t)P_coefficient_left * *input_left;
		input_left += 6;

		Q_tmp_right += (int64_t)P_coefficient_right * *input_right;
		input_right += 6;

		Q_tmp_left += (int64_t)P_coefficient_left_surround * *input_left_surround;
		input_left_surround += 6;

		Q_tmp_right += (int64_t)P_coefficient_right_surround * *input_right_surround;
		input_right_surround += 6;

		/* Shift with round to Q1.31 and saturate and store output */
		*output_left = sat_int32(Q_SHIFT_RND(Q_tmp_left, 62, 31));
		*output_right = sat_int32(Q_SHIFT_RND(Q_tmp_right, 62, 31));
		output_left += 2;
		output_right += 2;
	}
}

void downmix32bit_7_1(struct up_down_mixer_data *cd, const uint8_t * const in_data,
		      const uint32_t in_size, uint8_t * const out_data)
{
	/* Load the downmix coefficients. */
	int64_t Q_tmp_left;
	int64_t Q_tmp_right;
	int32_t P_coefficient_left = cd->downmix_coefficients[CHANNEL_LEFT];
	int32_t P_coefficient_center = cd->downmix_coefficients[CHANNEL_CENTER];
	int32_t P_coefficient_right = cd->downmix_coefficients[CHANNEL_RIGHT];
	int32_t P_coefficient_left_surround = cd->downmix_coefficients[CHANNEL_LEFT_SURROUND];
	int32_t P_coefficient_right_surround = cd->downmix_coefficients[CHANNEL_RIGHT_SURROUND];
	int32_t P_coefficient_lfe = cd->downmix_coefficients[CHANNEL_LFE];
	int32_t P_coefficient_left_side = cd->downmix_coefficients[CHANNEL_LEFT_SIDE];
	int32_t P_coefficient_right_side = cd->downmix_coefficients[CHANNEL_RIGHT_SIDE];

	/* Only load the channel if it's present. */
	int32_t *in32 = (int32_t *)in_data;

	const int32_t *input_left = in32 + get_channel_location(cd->in_channel_map, CHANNEL_LEFT);
	const int32_t *input_center = in32 +
		get_channel_location(cd->in_channel_map, CHANNEL_CENTER);
	const int32_t *input_right = in32 +
		get_channel_location(cd->in_channel_map, CHANNEL_RIGHT);
	const int32_t *input_left_surround = in32 +
		get_channel_location(cd->in_channel_map, CHANNEL_LEFT_SURROUND);
	const int32_t *input_right_surround = in32 +
		get_channel_location(cd->in_channel_map, CHANNEL_RIGHT_SURROUND);
	const int32_t *input_lfe = in32 + get_channel_location(cd->in_channel_map, CHANNEL_LFE);
	const int32_t *input_left_side = in32 +
		get_channel_location(cd->in_channel_map, CHANNEL_LEFT_SIDE);
	const int32_t *input_right_side = in32 +
		get_channel_location(cd->in_channel_map, CHANNEL_RIGHT_SIDE);

	/* Downmixer single store. */
	int32_t *output_left = (int32_t *)(out_data);
	int32_t *output_right = output_left + 1;

	const int32_t *const end_input_left = input_left + (in_size / (sizeof(int32_t)));

	while (input_left < end_input_left) {
		/* Accumulate as Q1.31 * Q1.31 -> Q2.62 */
		Q_tmp_left = (int64_t)P_coefficient_center * *input_center;
		input_center += 8;

		Q_tmp_left += (int64_t)P_coefficient_lfe * *input_lfe;
		input_lfe += 8;
		Q_tmp_right = Q_tmp_left;

		Q_tmp_left += (int64_t)P_coefficient_left * *input_left;
		input_left += 8;

		Q_tmp_right += (int64_t)P_coefficient_right * *input_right;
		input_right += 8;

		Q_tmp_left += (int64_t)P_coefficient_left_surround * *input_left_surround;
		input_left_surround += 8;

		Q_tmp_right += (int64_t)P_coefficient_right_surround * *input_right_surround;
		input_right_surround += 8;

		Q_tmp_left += (int64_t)P_coefficient_left_side * *input_left_side;
		input_left_surround += 8;

		Q_tmp_right += (int64_t)P_coefficient_right_side * *input_right_side;
		input_right_surround += 8;

		/* Shift with round to Q1.31 and saturate and store output */
		*output_left = sat_int32(Q_SHIFT_RND(Q_tmp_left, 62, 31));
		*output_right = sat_int32(Q_SHIFT_RND(Q_tmp_right, 62, 31));
		output_left += 2;
		output_right += 2;
	}
}

void downmix16bit_stereo(struct up_down_mixer_data *cd, const uint8_t * const in_data,
			 const uint32_t in_size, uint8_t * const out_data)
{
	int32_t sum;
	const int16_t *in_data16 = (int16_t *)in_data;
	int16_t *out_data16 = (int16_t *)out_data;
	int idx;

	for (idx = 0; idx < (in_size >> 2); ++idx) {
		sum = (int32_t)in_data16[2 * idx] + in_data16[2 * idx + 1];
		out_data16[idx] = sat_int16((sum + 1) >> 1);
	}
}

void shiftcopy16bit_mono(struct up_down_mixer_data *cd, const uint8_t * const in_data,
			 const uint32_t in_size, uint8_t * const out_data)
{
	int i;

	uint16_t *in_ptrs = (uint16_t *)in_data;
	uint64_t *out_ptrs = (uint64_t *)out_data;

	for (i = 0; i < (in_size >> 1); ++i)
		out_ptrs[i] = (uint64_t)in_ptrs[i] << 48 | ((uint64_t)in_ptrs[i] << 16);
}

void shiftcopy16bit_stereo(struct up_down_mixer_data *cd, const uint8_t * const in_data,
			   const uint32_t in_size, uint8_t * const out_data)
{
	uint32_t i;

	uint32_t *in_ptrs = (uint32_t *)in_data;
	uint64_t *out_ptrs = (uint64_t *)out_data;
	uint32_t in_regs;

	for (i = 0; i < (in_size >> 2); ++i) {
		in_regs = in_ptrs[i];
		out_ptrs[i] = (((uint64_t)in_regs & 0xffff) << 16) |
			(((uint64_t)in_regs & 0xffff0000) << 32);
	}
}

void downmix16bit(struct up_down_mixer_data *cd, const uint8_t * const in_data,
		  const uint32_t in_size, uint8_t * const out_data)
{
	int64_t Q_tmp_left = 0;
	int64_t Q_tmp_right = 0;
	int64_t p;
	int32_t P_coefficient_left = cd->downmix_coefficients[CHANNEL_LEFT];
	int32_t P_coefficient_center = cd->downmix_coefficients[CHANNEL_CENTER];
	int32_t P_coefficient_right = cd->downmix_coefficients[CHANNEL_RIGHT];
	int32_t P_coefficient_left_surround = cd->downmix_coefficients[CHANNEL_LEFT_SURROUND];
	int32_t P_coefficient_right_surround = cd->downmix_coefficients[CHANNEL_RIGHT_SURROUND];
	int32_t P_coefficient_lfe = cd->downmix_coefficients[CHANNEL_LFE];
	int32_t *output_left = (int32_t *)out_data;
	int32_t *output_right = output_left + 1;
	int16_t *input_left;
	int16_t *input_center;
	int16_t *input_right;
	int16_t *input_left_surround;
	int16_t *input_right_surround;
	int16_t *input_lfe;
	int16_t *in16 = (int16_t *)in_data;

	/* See what channels are available. */
	bool left = (get_channel_location(cd->in_channel_map, CHANNEL_LEFT) != 0xF);
	bool center = (get_channel_location(cd->in_channel_map, CHANNEL_CENTER) != 0xF);
	bool right = (get_channel_location(cd->in_channel_map, CHANNEL_RIGHT) != 0xF);
	bool left_surround =
		(get_channel_location(cd->in_channel_map, CHANNEL_LEFT_SURROUND) != 0xF);
	bool right_surround =
		(get_channel_location(cd->in_channel_map, CHANNEL_RIGHT_SURROUND) != 0xF);
	bool lfe = (get_channel_location(cd->in_channel_map, CHANNEL_LFE) != 0xF);

	/** Calculate number of samples in a single channel. */
	int number_of_samples_in_one_channel = in_size / cd->in_channel_no >> 1;

	int sample_offset = cd->in_channel_no;
	int i;

	/* No check for bool above. If false address becomes in32 + 0xf, but it is not
	 * used in the processing loop.
	 */
	input_left = in16 + get_channel_location(cd->in_channel_map, CHANNEL_LEFT);
	input_center = in16 + get_channel_location(cd->in_channel_map, CHANNEL_CENTER);
	input_right = in16 + get_channel_location(cd->in_channel_map, CHANNEL_RIGHT);
	input_left_surround = in16 + get_channel_location(cd->in_channel_map,
							  CHANNEL_LEFT_SURROUND);
	input_right_surround = in16 + get_channel_location(cd->in_channel_map,
							   CHANNEL_RIGHT_SURROUND);
	input_lfe = in16 + get_channel_location(cd->in_channel_map, CHANNEL_LFE);

	for (i = 0; i < number_of_samples_in_one_channel; i++) {
		if (left) {
			Q_tmp_left += (int64_t)P_coefficient_left * *input_left;
			input_left += sample_offset;
		}
		if (center) {
			p = (int64_t)P_coefficient_center * *input_center;
			Q_tmp_left += p;
			Q_tmp_right += p;
			input_center += sample_offset;
		}
		if (right) {
			Q_tmp_right += (int64_t)P_coefficient_right * *input_right;
			input_right += sample_offset;
		}
		if (left_surround) {
			p = (int64_t)P_coefficient_left_surround * *input_left_surround;
			Q_tmp_left += p;
			input_left_surround += sample_offset;
			if (cd->in_channel_config == IPC4_CHANNEL_CONFIG_4_POINT_0)
				Q_tmp_right += p;
		}
		if (right_surround) {
			Q_tmp_right += (int64_t)P_coefficient_right_surround *
				*input_right_surround;
			input_right_surround += sample_offset;
		}
		if (lfe) {
			p = (int64_t)P_coefficient_lfe * *input_lfe;
			Q_tmp_left += p;
			Q_tmp_right += p;
			input_lfe += sample_offset;
		}

		/* Shift with round to Q1.31 and saturate and store output */
		*output_left = sat_int32(Q_SHIFT_RND(Q_tmp_left, 46, 31));
		*output_right = sat_int32(Q_SHIFT_RND(Q_tmp_right, 46, 31));
		output_left += 2;
		output_right += 2;
	}
}

void downmix16bit_5_1(struct up_down_mixer_data *cd, const uint8_t * const in_data,
		      const uint32_t in_size, uint8_t * const out_data)
{
	int64_t Q_tmp_left;
	int64_t Q_tmp_right;
	int32_t P_coefficient_left = cd->downmix_coefficients[CHANNEL_LEFT];
	int32_t P_coefficient_center = cd->downmix_coefficients[CHANNEL_CENTER];
	int32_t P_coefficient_right = cd->downmix_coefficients[CHANNEL_RIGHT];
	int32_t P_coefficient_left_surround = cd->downmix_coefficients[CHANNEL_LEFT_SURROUND];
	int32_t P_coefficient_right_surround = cd->downmix_coefficients[CHANNEL_RIGHT_SURROUND];
	int32_t P_coefficient_lfe = cd->downmix_coefficients[CHANNEL_LFE];
	int32_t *output_left = (int32_t *)out_data;
	int32_t *output_right = output_left + 1;
	int16_t *input_left;
	int16_t *input_center;
	int16_t *input_right;
	int16_t *input_left_surround;
	int16_t *input_right_surround;
	int16_t *input_lfe;
	int16_t *in16 = (int16_t *)in_data;
	int i;
	int number_of_samples_in_one_channel = (in_size / cd->in_channel_no) >> 1;

	/* Only load the channel if it's present. */
	input_left = in16 + get_channel_location(cd->in_channel_map, CHANNEL_LEFT);
	input_center = in16 + get_channel_location(cd->in_channel_map, CHANNEL_CENTER);
	input_right = in16 + get_channel_location(cd->in_channel_map, CHANNEL_RIGHT);
	input_left_surround = in16 + get_channel_location(cd->in_channel_map, CHANNEL_LEFT_SURROUND);
	input_right_surround = in16 + get_channel_location(cd->in_channel_map, CHANNEL_RIGHT_SURROUND);
	input_lfe = in16 + get_channel_location(cd->in_channel_map, CHANNEL_LFE);

	for (i = 0; i < number_of_samples_in_one_channel; i++) {
		/* Accumulate as Q1.31 * Q1.15 -> Q2.46 */
		Q_tmp_left = (int64_t)P_coefficient_center * *input_center;
		input_center += 6;

		Q_tmp_left += (int64_t)P_coefficient_lfe * *input_lfe;
		input_lfe += 6;
		Q_tmp_right = Q_tmp_left;

		Q_tmp_left += (int64_t)P_coefficient_left * *input_left;
		input_left += 6;

		Q_tmp_right += (int64_t)P_coefficient_right * *input_right;
		input_right += 6;

		Q_tmp_left += (int64_t)P_coefficient_left_surround * *input_left_surround;
		input_left_surround += 6;

		Q_tmp_right += (int64_t)P_coefficient_right_surround * *input_right_surround;
		input_right_surround += 6;

		/* Shift with round to Q1.31 and saturate and store output */
		*output_left = sat_int32(Q_SHIFT_RND(Q_tmp_left, 46, 31));
		*output_right = sat_int32(Q_SHIFT_RND(Q_tmp_right, 46, 31));
		output_left += 2;
		output_right += 2;
	}
}

void downmix16bit_4ch_mono(struct up_down_mixer_data *cd, const uint8_t * const in_data,
			   const uint32_t in_size, uint8_t * const out_data)
{
	int32_t Q_tmp;
	uint16_t coeffs[4] = {cd->downmix_coefficients[get_channel_index(cd->in_channel_map, 0)],
			      cd->downmix_coefficients[get_channel_index(cd->in_channel_map, 1)],
			      cd->downmix_coefficients[get_channel_index(cd->in_channel_map, 2)],
			      cd->downmix_coefficients[get_channel_index(cd->in_channel_map, 3)]
			};

	const int16_t *input_data = (const int16_t *)in_data;
	int16_t *output_data = (int16_t *)out_data;
	int i;

	for (i = 0; i < in_size / (sizeof(int16_t)); i += 4) {
		/* Q1.15 * Q.15 -> Q2.30 */
		Q_tmp = (int32_t)coeffs[0] * input_data[0] +
			(int32_t)coeffs[1] * input_data[1] +
			(int32_t)coeffs[2] * input_data[2] +
			(int32_t)coeffs[3] * input_data[3];
		*output_data = sat_int16(Q_SHIFT_RND(Q_tmp, 30, 15));
		input_data += 4;
		output_data++;
	}
}

void downmix32bit_stereo(struct up_down_mixer_data *cd, const uint8_t * const in_data,
			 const uint32_t in_size, uint8_t * const out_data)
{
	int64_t sum;
	int32_t *in_data32 = (int32_t *)in_data;
	int32_t *out_data32 = (int32_t *)out_data;
	int idx;

	for (idx = 0; idx < (in_size >> 3); idx++) {
		sum = (int64_t)in_data32[2 * idx] + in_data32[2 * idx + 1];
		out_data32[idx] = sat_int32((sum + 1) >> 1);
	}
}

void downmix32bit_3_1_mono(struct up_down_mixer_data *cd, const uint8_t * const in_data,
			   const uint32_t in_size, uint8_t * const out_data)
{
	int64_t Q_tmp;
	int32_t P_coefficient_left = cd->downmix_coefficients[CHANNEL_LEFT];
	int32_t P_coefficient_center = cd->downmix_coefficients[CHANNEL_CENTER];
	int32_t P_coefficient_right = cd->downmix_coefficients[CHANNEL_RIGHT];
	int32_t P_coefficient_lfe = cd->downmix_coefficients[CHANNEL_LFE];
	const int32_t *in32 = (const int32_t *)in_data;
	const int32_t *input_left = in32 + get_channel_location(cd->in_channel_map, CHANNEL_LEFT);
	const int32_t *input_center = in32 + get_channel_location(cd->in_channel_map, CHANNEL_CENTER);
	const int32_t *input_right = in32 + get_channel_location(cd->in_channel_map, CHANNEL_RIGHT);
	const int32_t *input_lfe = in32 + get_channel_location(cd->in_channel_map, CHANNEL_LFE);
	int32_t *output = (int32_t *)(out_data);
	const int channel_no = 4;
	int i;

	for (i = 0; i < in_size / (sizeof(int32_t)); i += channel_no) {
		Q_tmp = (int64_t)P_coefficient_left * *input_left;
		input_left += 4;

		Q_tmp += (int64_t)P_coefficient_center * *input_center;
		input_center += 4;

		Q_tmp += (int64_t)P_coefficient_right * *input_right;
		input_right += 4;

		Q_tmp += (int64_t)P_coefficient_lfe * *input_lfe;
		input_right += 4;

		/* Shift with round to Q1.31 and saturate and store output */
		*output = sat_int32(Q_SHIFT_RND(Q_tmp, 62, 31));
		output++;
	}
}

void downmix32bit_4_0_mono(struct up_down_mixer_data *cd, const uint8_t * const in_data,
			   const uint32_t in_size, uint8_t * const out_data)
{
	int64_t Q_tmp;
	int32_t P_coefficient_left = cd->downmix_coefficients[CHANNEL_LEFT];
	int32_t P_coefficient_center = cd->downmix_coefficients[CHANNEL_CENTER];
	int32_t P_coefficient_right = cd->downmix_coefficients[CHANNEL_RIGHT];
	int32_t P_coefficient_cs = cd->downmix_coefficients[CHANNEL_CENTER_SURROUND];
	const int32_t *in32 = (const int32_t *)in_data;
	const int32_t *input_left = in32 + get_channel_location(cd->in_channel_map, CHANNEL_LEFT);
	const int32_t *input_center = in32 + get_channel_location(cd->in_channel_map, CHANNEL_CENTER);
	const int32_t *input_right = in32 + get_channel_location(cd->in_channel_map, CHANNEL_RIGHT);
	const int32_t *input_cs = in32 + get_channel_location(cd->in_channel_map, CHANNEL_CENTER_SURROUND);
	int32_t *output = (int32_t *)(out_data);
	const int channel_no = 4;
	int i;

	for (i = 0; i < in_size / (sizeof(int32_t)); i += channel_no) {
		Q_tmp = (int64_t)P_coefficient_left * *input_left;
		input_left += 4;

		Q_tmp += (int64_t)P_coefficient_center * *input_center;
		input_center += 4;

		Q_tmp += (int64_t)P_coefficient_right * *input_right;
		input_right += 4;

		Q_tmp += (int64_t)P_coefficient_cs * *input_cs;
		input_cs += 4;

		/* Shift with round to Q1.31 and saturate and store output */
		*output = sat_int32(Q_SHIFT_RND(Q_tmp, 62, 31));
		output++;
	}
}

void downmix32bit_quatro_mono(struct up_down_mixer_data *cd, const uint8_t * const in_data,
			      const uint32_t in_size, uint8_t * const out_data)
{
	int64_t Q_tmp;
	int32_t P_coefficient_left = cd->downmix_coefficients[CHANNEL_LEFT];
	int32_t P_coefficient_ls = cd->downmix_coefficients[CHANNEL_LEFT_SURROUND];
	int32_t P_coefficient_right = cd->downmix_coefficients[CHANNEL_RIGHT];
	int32_t P_coefficient_rs = cd->downmix_coefficients[CHANNEL_RIGHT_SURROUND];
	const int32_t *in32 = (const int32_t *)in_data;
	const int32_t *input_left = in32 + get_channel_location(cd->in_channel_map, CHANNEL_LEFT);
	const int32_t *input_ls = in32 + get_channel_location(cd->in_channel_map, CHANNEL_LEFT_SURROUND);
	const int32_t *input_right = in32 + get_channel_location(cd->in_channel_map, CHANNEL_RIGHT);
	const int32_t *input_rs = in32 + get_channel_location(cd->in_channel_map, CHANNEL_RIGHT_SURROUND);
	int32_t *output = (int32_t *)(out_data);
	const int channel_no = 4;
	int i;

	for (i = 0; i < in_size / (sizeof(int32_t)); i += channel_no) {
		Q_tmp = (int64_t)P_coefficient_left * *input_left;
		input_left += 4;

		Q_tmp += (int64_t)P_coefficient_ls * *input_ls;
		input_ls += 4;

		Q_tmp += (int64_t)P_coefficient_right * *input_right;
		input_right += 4;

		Q_tmp += (int64_t)P_coefficient_rs * *input_rs;
		input_rs += 4;

		/* Shift with round to Q1.31 and saturate and store output */
		*output = sat_int32(Q_SHIFT_RND(Q_tmp, 62, 31));
		output++;
	}
}

void downmix32bit_5_1_mono(struct up_down_mixer_data *cd, const uint8_t * const in_data,
			   const uint32_t in_size, uint8_t * const out_data)
{
	int64_t Q_tmp;
	int32_t P_coefficient_left = cd->downmix_coefficients[CHANNEL_LEFT];
	int32_t P_coefficient_center = cd->downmix_coefficients[CHANNEL_CENTER];
	int32_t P_coefficient_right = cd->downmix_coefficients[CHANNEL_RIGHT];
	int32_t P_coefficient_cs = cd->downmix_coefficients[CHANNEL_CENTER_SURROUND];
	const int32_t *in32 = (const int32_t *)in_data;
	const int32_t *input_left = in32 + get_channel_location(cd->in_channel_map, CHANNEL_LEFT);
	const int32_t *input_center = in32 + get_channel_location(cd->in_channel_map, CHANNEL_CENTER);
	const int32_t *input_right = in32 + get_channel_location(cd->in_channel_map, CHANNEL_RIGHT);
	const int32_t *input_cs = in32 + get_channel_location(cd->in_channel_map, CHANNEL_CENTER_SURROUND);
	int32_t *output = (int32_t *)(out_data);
	const int channel_no = 4;
	int i;

	for (i = 0; i < in_size / (sizeof(int32_t)); i += channel_no) {
		Q_tmp = (int64_t)P_coefficient_left * *input_left;
		input_left += 6;

		Q_tmp += (int64_t)P_coefficient_center * *input_center;
		input_center += 6;

		Q_tmp += (int64_t)P_coefficient_right * *input_right;
		input_right += 6;

		Q_tmp += (int64_t)P_coefficient_cs * *input_cs;
		input_cs += 6;

		/* Shift with round to Q1.31 and saturate and store output */
		*output = sat_int32(Q_SHIFT_RND(Q_tmp, 62, 31));
		output++;
	}
}

void downmix32bit_7_1_mono(struct up_down_mixer_data *cd, const uint8_t * const in_data,
			   const uint32_t in_size, uint8_t * const out_data)
{
	int64_t Q_tmp;
	int32_t P_coefficient_left = cd->downmix_coefficients[CHANNEL_LEFT];
	int32_t P_coefficient_center = cd->downmix_coefficients[CHANNEL_CENTER];
	int32_t P_coefficient_right = cd->downmix_coefficients[CHANNEL_RIGHT];
	int32_t P_coefficient_cs = cd->downmix_coefficients[CHANNEL_CENTER_SURROUND];
	const int32_t *in32 = (const int32_t *)in_data;
	const int32_t *input_left = in32 + get_channel_location(cd->in_channel_map, CHANNEL_LEFT);
	const int32_t *input_center = in32 + get_channel_location(cd->in_channel_map, CHANNEL_CENTER);
	const int32_t *input_right = in32 + get_channel_location(cd->in_channel_map, CHANNEL_RIGHT);
	const int32_t *input_cs = in32 + get_channel_location(cd->in_channel_map, CHANNEL_CENTER_SURROUND);
	int32_t *output = (int32_t *)(out_data);
	const int channel_no = 8;
	int i;

	for (i = 0; i < in_size / (sizeof(int32_t)); i += channel_no) {
		Q_tmp = (int64_t)P_coefficient_left * *input_left;
		input_left += 8;

		Q_tmp += (int64_t)P_coefficient_center * *input_center;
		input_center += 8;

		Q_tmp += (int64_t)P_coefficient_right * *input_right;
		input_right += 8;

		Q_tmp += (int64_t)P_coefficient_cs * *input_cs;
		input_cs += 8;

		/* Shift with round to Q1.31 and saturate and store output */
		*output = sat_int32(Q_SHIFT_RND(Q_tmp, 62, 31));
		output++;
	}
}

void downmix32bit_7_1_to_5_1(struct up_down_mixer_data *cd, const uint8_t * const in_data,
			     const uint32_t in_size, uint8_t * const out_data)
{
	int64_t Q_tmp_right_side;
	int64_t Q_tmp_left_side;
	uint32_t i;

	channel_map out_channel_map = cd->out_channel_map;
	const uint8_t left_slot = get_channel_location(out_channel_map, CHANNEL_LEFT);
	const uint8_t center_slot = get_channel_location(out_channel_map, CHANNEL_CENTER);
	const uint8_t right_slot = get_channel_location(out_channel_map, CHANNEL_RIGHT);
	uint8_t right_surround_slot = get_channel_location(out_channel_map, CHANNEL_RIGHT_SURROUND);
	uint8_t left_surround_slot = get_channel_location(out_channel_map, CHANNEL_LEFT_SURROUND);
	const uint8_t lfe_slot = get_channel_location(out_channel_map, CHANNEL_LFE);

	/* Must support also 5.1 Surround */
	const bool surround_5_1_channel_map = (left_surround_slot == CHANNEL_INVALID) &&
					      (right_surround_slot == CHANNEL_INVALID);

	if (surround_5_1_channel_map) {
		left_surround_slot = get_channel_location(out_channel_map, CHANNEL_LEFT_SIDE);
		right_surround_slot = get_channel_location(out_channel_map, CHANNEL_RIGHT_SIDE);
	}

	int32_t *output_left_ptr = (int32_t *)(out_data + (left_slot << 2));
	int32_t *output_center_ptr = (int32_t *)(out_data + (center_slot << 2));
	int32_t *output_right_ptr = (int32_t *)(out_data + (right_slot << 2));
	int32_t *output_side_left_ptr = (int32_t *)(out_data + (left_surround_slot << 2));
	int32_t *output_side_right_ptr = (int32_t *)(out_data + (right_surround_slot << 2));
	int32_t *output_lfe_ptr = (int32_t *)(out_data + (lfe_slot << 2));

	int32_t *in_left_ptr = (int32_t *)in_data;
	int32_t *in_center_ptr = (int32_t *)(in_data + 4);
	int32_t *in_right_ptr = (int32_t *)(in_data + 8);
	int32_t *in_lfe_ptr = (int32_t *)(in_data + 20);

	for (i = 0; i < (in_size >> 5); ++i) {
		output_left_ptr[i * 6] = in_left_ptr[i * 8];
		output_right_ptr[i * 6] = in_right_ptr[i * 8];
		output_center_ptr[i * 6] = in_center_ptr[i * 8];
		output_lfe_ptr[i * 6] = in_lfe_ptr[i * 8];
	}

	/* Load the downmix coefficients. */
	int32_t P_coefficient_left = cd->downmix_coefficients[CHANNEL_LEFT];
	int32_t P_coefficient_right = cd->downmix_coefficients[CHANNEL_RIGHT];
	int32_t P_coefficient_left_surround = cd->downmix_coefficients[CHANNEL_LEFT_SURROUND];
	int32_t P_coefficient_right_surround = cd->downmix_coefficients[CHANNEL_RIGHT_SURROUND];

	const int32_t *in32 = (const int32_t *)in_data;
	const int32_t *input_left_surround = in32 + get_channel_location(cd->in_channel_map, CHANNEL_LEFT_SURROUND);
	const int32_t *input_right_surround = in32 + get_channel_location(cd->in_channel_map, CHANNEL_RIGHT_SURROUND);
	const int32_t *input_left_side = in32 + get_channel_location(cd->in_channel_map, CHANNEL_LEFT_SIDE);
	const int32_t *input_right_side = in32 + get_channel_location(cd->in_channel_map, CHANNEL_RIGHT_SIDE);


	const int32_t *const end_input_left = input_left_surround + (in_size / (sizeof(int32_t)));
	// const char cs = 8 * sizeof(int32_t);

	while (input_left_surround < end_input_left) {
		Q_tmp_left_side = (int64_t)P_coefficient_left * *input_left_surround;
		Q_tmp_right_side = (int64_t)P_coefficient_right_surround * *input_left_surround;
		input_left_surround += 8;

		Q_tmp_left_side += (int64_t)P_coefficient_left_surround * *input_right_surround;
		Q_tmp_right_side += (int64_t)P_coefficient_right * *input_right_surround;
		input_right_surround += 8;

		Q_tmp_left_side += (int64_t)P_coefficient_left * *input_left_side;
		input_left_side += 8;

		Q_tmp_right_side += (int64_t)P_coefficient_right * *input_right_side;
		input_right_side += 8;

		/* Shift with round to Q1.31 and saturate and store output */
		*output_side_left_ptr = sat_int32(Q_SHIFT_RND(Q_tmp_left_side, 46, 31));
		*output_side_right_ptr = sat_int32(Q_SHIFT_RND(Q_tmp_right_side, 46, 31));
		output_side_left_ptr += 6;
		output_side_right_ptr += 6;
	}
}

void upmix32bit_4_0_to_5_1(struct up_down_mixer_data *cd, const uint8_t * const in_data,
			   const uint32_t in_size, uint8_t * const out_data)
{
	sof_panic(0);
}

void upmix32bit_quatro_to_5_1(struct up_down_mixer_data *cd, const uint8_t * const in_data,
			      const uint32_t in_size, uint8_t * const out_data)
{
	int i;

	channel_map out_channel_map = cd->out_channel_map;

	const uint8_t left_slot = get_channel_location(out_channel_map, CHANNEL_LEFT);
	const uint8_t center_slot = get_channel_location(out_channel_map, CHANNEL_CENTER);
	const uint8_t right_slot = get_channel_location(out_channel_map, CHANNEL_RIGHT);
	uint8_t right_surround_slot = get_channel_location(out_channel_map, CHANNEL_RIGHT_SURROUND);
	uint8_t left_surround_slot = get_channel_location(out_channel_map, CHANNEL_LEFT_SURROUND);
	const uint8_t lfe_slot = get_channel_location(out_channel_map, CHANNEL_LFE);

	/* Must support also 5.1 Surround */
	const bool surround_5_1_channel_map = (left_surround_slot == CHANNEL_INVALID) &&
		(right_surround_slot == CHANNEL_INVALID);

	if (surround_5_1_channel_map) {
		left_surround_slot = get_channel_location(cd->in_channel_map, CHANNEL_LEFT_SIDE);
		right_surround_slot = get_channel_location(cd->in_channel_map, CHANNEL_RIGHT_SIDE);
	}

	int32_t *output_left = (int32_t *)(out_data + (left_slot << 2));
	int32_t *output_center = (int32_t *)(out_data + (center_slot << 2));
	int32_t *output_right = (int32_t *)(out_data + (right_slot << 2));
	int32_t *output_side_left = (int32_t *)(out_data + (left_surround_slot << 2));
	int32_t *output_side_right = (int32_t *)(out_data + (right_surround_slot << 2));
	int32_t *output_lfe = (int32_t *)(out_data + (lfe_slot << 2));

	int32_t *in_left_ptr = (int32_t *)in_data;
	int32_t *in_right_ptr = (int32_t *)(in_data + 4);
	int32_t *in_left_sorround_ptr = (int32_t *)(in_data + 8);
	int32_t *in_right_sorround_ptr = (int32_t *)(in_data + 12);

	for (i = 0; i < (in_size >> 4); ++i) {
		output_left[i * 6] = in_left_ptr[i * 4];
		output_right[i * 6] = in_right_ptr[i * 4];
		output_center[i * 6] = 0;
		output_side_left[i * 6] = in_left_sorround_ptr[i * 4];
		output_side_right[i * 6] = in_right_sorround_ptr[i * 4];
		output_lfe[i * 6] = 0;
	}
}

#endif /* #if SOF_USE_HIFI(NONE, UP_DOWN_MIXER) */
