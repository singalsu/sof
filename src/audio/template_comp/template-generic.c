// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2025 Intel Corporation.

#include <sof/audio/module_adapter/module/generic.h>
#include <sof/audio/component.h>
#include <sof/audio/sink_api.h>
#include <sof/audio/sink_source_utils.h>
#include <sof/audio/source_api.h>
#include <stdint.h>
#include "template.h"

#if CONFIG_FORMAT_S16LE
/**
 * template_comp_s16() - Process S16_LE format.
 * @mod: Pointer to module data.
 * @source: Source for PCM samples data.
 * @sink: Sink for PCM samples data.
 * @frames: Number of audio data frames to process.
 *
 * This is the processing function for 16-bit signed integer PCM formats. The
 * audio samples in every frame are re-order to channels order defined in
 * component data channels_order[].
 *
 * Return: Value zero for success, otherwise an error code.
 */
static int template_comp_s16(const struct processing_module *mod,
			     struct sof_source *source,
			     struct sof_sink *sink,
			     uint32_t frames)
{
	struct template_comp_comp_data *cd = module_get_private_data(mod);
	int16_t *x, *x_start, *x_end;
	int16_t *y, *y_start, *y_end;
	size_t size;
	int x_size, y_size;
	int source_samples_without_wrap;
	int samples_without_wrap;
	int samples = frames * cd->channels;
	int bytes = frames * cd->frame_bytes;
	int ret;
	int ch;
	int i;

	ret = source_get_data(source, bytes, (void const **)&x, (void const **)&x_start, &size);
	x_size = size >> 1; /* Bytes to number of s16 samples */
	if (ret)
		return ret;

	ret = sink_get_buffer(sink, bytes, (void **)&y, (void **)&y_start, &size);
	y_size = size >> 1; /* Bytes to number of s16 samples */
	if (ret)
		return ret;

	x_end = x_start + x_size;
	y_end = y_start + y_size;
	while (samples) {
		source_samples_without_wrap = x_end - x;
		samples_without_wrap = y_end - y;
		samples_without_wrap = MIN(samples_without_wrap, source_samples_without_wrap);
		samples_without_wrap = MIN(samples_without_wrap, samples);
		for (i = 0; i < samples_without_wrap; i += cd->channels) {
			for (ch = 0; ch < cd->channels; ch++) {
				*y = *(x + cd->channels_order[ch]);
				y++;
			}
			x += cd->channels;
		}

		x = (x >= x_end) ? x - x_size : x;
		y = (y >= y_end) ? y - y_size : y;
		samples -= samples_without_wrap;
	}

	source_release_data(source, bytes);
	sink_commit_buffer(sink, bytes);
	return 0;
}
#endif /* CONFIG_FORMAT_S16LE */

#if CONFIG_FORMAT_S32LE
/**
 * template_comp_s32() - Process S32_LE or S24_4LE format.
 * @mod: Pointer to module data.
 * @source: Source for PCM samples data.
 * @sink: Sink for PCM samples data.
 * @frames: Number of audio data frames to process.
 *
 * Processing function for signed integer 32-bit PCM formats. The same
 * function works for s24 and s32 formats since the samples values are
 * not modified in computation. The audio samples in every frame are
 * re-order to channels order defined in component data channels_order[].
 *
 * Return: Value zero for success, otherwise an error code.
 */
static int template_comp_s32(const struct processing_module *mod,
			     struct sof_source *source,
			     struct sof_sink *sink,
			     uint32_t frames)
{
	struct template_comp_comp_data *cd = module_get_private_data(mod);
	int32_t *x, *x_start, *x_end;
	int32_t *y, *y_start, *y_end;
	size_t size;
	int x_size, y_size;
	int source_samples_without_wrap;
	int samples_without_wrap;
	int samples = frames * cd->channels;
	int bytes = frames * cd->frame_bytes;
	int ret;
	int ch;
	int i;

	ret = source_get_data(source, bytes, (void const **)&x, (void const **)&x_start, &size);
	x_size = size >> 2; /* Bytes to number of s32 samples */
	if (ret)
		return ret;

	ret = sink_get_buffer(sink, bytes, (void **)&y, (void **)&y_start, &size);
	y_size = size >> 2; /* Bytes to number of s32 samples */
	if (ret)
		return ret;

	x_end = x_start + x_size;
	y_end = y_start + y_size;
	while (samples) {
		source_samples_without_wrap = x_end - x;
		samples_without_wrap = y_end - y;
		samples_without_wrap = MIN(samples_without_wrap, source_samples_without_wrap);
		samples_without_wrap = MIN(samples_without_wrap, samples);
		for (i = 0; i < samples_without_wrap; i += cd->channels) {
			for (ch = 0; ch < cd->channels; ch++) {
				*y = *(x + cd->channels_order[ch]);
				y++;
			}
			x += cd->channels;
		}

		x = (x >= x_end) ? x - x_size : x;
		y = (y >= y_end) ? y - y_size : y;
		samples -= samples_without_wrap;
	}

	source_release_data(source, bytes);
	sink_commit_buffer(sink, bytes);
	return 0;
}
#endif /* CONFIG_FORMAT_S32LE */

/* This struct array defines the used processing functions for
 * the PCM formats
 */
const struct template_comp_proc_fnmap template_comp_proc_fnmap[] = {
#if CONFIG_FORMAT_S16LE
	{ SOF_IPC_FRAME_S16_LE, template_comp_s16 },
#endif
#if CONFIG_FORMAT_S24LE
	{ SOF_IPC_FRAME_S24_4LE, template_comp_s32 },
#endif
#if CONFIG_FORMAT_S32LE
	{ SOF_IPC_FRAME_S32_LE, template_comp_s32 },
#endif
};

/**
 * template_comp_find_proc_func() - Find suitable processing function.
 * @src_fmt: Enum value for PCM format.
 *
 * This function finds the suitable processing function to use for
 * the used PCM format. If not found, return NULL.
 *
 * Return: Pointer to processing function for the requested PCM format.
 */
template_comp_func template_comp_find_proc_func(enum sof_ipc_frame src_fmt)
{
	int i;

	/* Find suitable processing function from map */
	for (i = 0; i < ARRAY_SIZE(template_comp_proc_fnmap); i++)
		if (src_fmt == template_comp_proc_fnmap[i].frame_fmt)
			return template_comp_proc_fnmap[i].template_comp_proc_func;

	return NULL;
}
