// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2022-2025 Intel Corporation.
//
// Author: Bartosz Kokoszko <bartoszx.kokoszko@intel.com>
//         Seppo Ingalsuo <seppo.ingalsuo@linux.intel.com>

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
	sof_panic(0);
}

void downmix32bit_3_0(struct up_down_mixer_data *cd, const uint8_t * const in_data,
		      const uint32_t in_size, uint8_t * const out_data)
{
	sof_panic(0);
}

void downmix32bit_3_1(struct up_down_mixer_data *cd, const uint8_t * const in_data,
		      const uint32_t in_size, uint8_t * const out_data)
{
	sof_panic(0);
}

void downmix32bit(struct up_down_mixer_data *cd, const uint8_t * const in_data,
		  const uint32_t in_size, uint8_t * const out_data)
{
	sof_panic(0);
}

void downmix32bit_4_0(struct up_down_mixer_data *cd, const uint8_t * const in_data,
		      const uint32_t in_size, uint8_t * const out_data)
{
	sof_panic(0);
}

void downmix32bit_5_0_mono(struct up_down_mixer_data *cd, const uint8_t * const in_data,
			   const uint32_t in_size, uint8_t * const out_data)
{
	sof_panic(0);
}

void downmix32bit_5_1(struct up_down_mixer_data *cd, const uint8_t * const in_data,
		      const uint32_t in_size, uint8_t * const out_data)
{
	sof_panic(0);
}

void downmix32bit_7_1(struct up_down_mixer_data *cd, const uint8_t * const in_data,
		      const uint32_t in_size, uint8_t * const out_data)
{
	sof_panic(0);
}

void downmix16bit_stereo(struct up_down_mixer_data *cd, const uint8_t * const in_data,
			 const uint32_t in_size, uint8_t * const out_data)
{
	sof_panic(0);
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
	sof_panic(0);
}

void downmix16bit_5_1(struct up_down_mixer_data *cd, const uint8_t * const in_data,
		      const uint32_t in_size, uint8_t * const out_data)
{
	sof_panic(0);
}

void downmix16bit_4ch_mono(struct up_down_mixer_data *cd, const uint8_t * const in_data,
			   const uint32_t in_size, uint8_t * const out_data)
{
	sof_panic(0);
}

void downmix32bit_stereo(struct up_down_mixer_data *cd, const uint8_t * const in_data,
			 const uint32_t in_size, uint8_t * const out_data)
{
	sof_panic(0);
}

void downmix32bit_3_1_mono(struct up_down_mixer_data *cd, const uint8_t * const in_data,
			   const uint32_t in_size, uint8_t * const out_data)
{
	sof_panic(0);
}

void downmix32bit_4_0_mono(struct up_down_mixer_data *cd, const uint8_t * const in_data,
			   const uint32_t in_size, uint8_t * const out_data)
{
	sof_panic(0);
}

void downmix32bit_quatro_mono(struct up_down_mixer_data *cd, const uint8_t * const in_data,
			      const uint32_t in_size, uint8_t * const out_data)
{
	sof_panic(0);
}

void downmix32bit_5_1_mono(struct up_down_mixer_data *cd, const uint8_t * const in_data,
			   const uint32_t in_size, uint8_t * const out_data)
{
	sof_panic(0);
}

void downmix32bit_7_1_mono(struct up_down_mixer_data *cd, const uint8_t * const in_data,
			   const uint32_t in_size, uint8_t * const out_data)
{
	sof_panic(0);
}

void downmix32bit_7_1_to_5_1(struct up_down_mixer_data *cd, const uint8_t * const in_data,
			     const uint32_t in_size, uint8_t * const out_data)
{
	sof_panic(0);
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
