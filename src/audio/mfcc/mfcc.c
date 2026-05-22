// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2022 Intel Corporation. All rights reserved.
//
// Author: Seppo Ingalsuo <seppo.ingalsuo@linux.intel.com>

#include <sof/audio/mfcc/mfcc_comp.h>
#include <sof/audio/module_adapter/module/generic.h>
#include <sof/audio/component.h>
#include <sof/audio/data_blob.h>
#include <sof/audio/buffer.h>
#include <sof/audio/format.h>
#include <sof/audio/pipeline.h>
#include <sof/audio/ipc-config.h>
#include <module/audio/source_api.h>
#include <module/audio/sink_api.h>
#include <sof/common.h>
#include <rtos/panic.h>
#include <sof/ipc/msg.h>
#include <sof/lib/uuid.h>
#include <sof/list.h>
#include <sof/platform.h>
#include <sof/ut.h>
#include <sof/trace/trace.h>
#include <ipc/control.h>
#include <ipc/stream.h>
#include <ipc/topology.h>
#include <user/mfcc.h>
#include <user/trace.h>
#include <rtos/init.h>
#include <rtos/string.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

LOG_MODULE_REGISTER(mfcc, CONFIG_SOF_LOG_LEVEL);

SOF_DEFINE_REG_UUID(mfcc);

/** \brief Source/sink API based source copy function map. */
struct mfcc_source_func_map {
	uint8_t source;
	mfcc_source_func func;
};

__cold_rodata static const struct mfcc_source_func_map mfcc_sfm[] = {
#if CONFIG_FORMAT_S16LE
	{SOF_IPC_FRAME_S16_LE, mfcc_source_copy_s16},
#endif
#if CONFIG_FORMAT_S24LE
	{SOF_IPC_FRAME_S24_4LE, mfcc_source_copy_s24},
#endif
#if CONFIG_FORMAT_S32LE
	{SOF_IPC_FRAME_S32_LE, mfcc_source_copy_s32},
#endif
};

static mfcc_source_func mfcc_find_source_func(enum sof_ipc_frame source_format)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mfcc_sfm); i++) {
		if (source_format == mfcc_sfm[i].source)
			return mfcc_sfm[i].func;
	}

	return NULL;
}

/*
 * End of MFCC setup code. Next the standard component methods.
 */

static int mfcc_init(struct processing_module *mod)
{
	struct module_data *md = &mod->priv;
	struct comp_dev *dev = mod->dev;
	struct mfcc_comp_data *cd = NULL;

	comp_info(dev, "entry");

	cd = mod_zalloc(mod, sizeof(*cd));
	if (!cd)
		return -ENOMEM;

	/* Handler for configuration data */
	md->private = cd;
	cd->model_handler = mod_data_blob_handler_new(mod);
	if (!cd->model_handler) {
		comp_err(dev, "comp_data_blob_handler_new() failed.");
		mod_free(mod, cd);
		return -ENOMEM;
	}

	return 0;
}

static int mfcc_free(struct processing_module *mod)
{
	struct mfcc_comp_data *cd = module_get_private_data(mod);

	comp_info(mod->dev, "entry");
	ipc_msg_free(cd->msg);
	cd->msg = NULL;
	mod_data_blob_handler_free(mod, cd->model_handler);
	mfcc_free_buffers(mod);
	mod_free(mod, cd);
	return 0;
}


/**
 * \brief Write bytes to a circular sink buffer with wrap handling.
 *
 * Copies up to \a max_bytes from \a src into the sink buffer at \a *dst,
 * advancing *dst and wrapping within [buf_start, buf_start + buf_size).
 *
 * \return Number of bytes actually written (limited by max_bytes).
 */
static size_t mfcc_sink_write_bytes(uint8_t **dst, uint8_t *buf_start,
				    size_t buf_size, const uint8_t *src,
				    size_t max_bytes)
{
	uint8_t *buf_end = buf_start + buf_size;
	size_t chunk;

	if (max_bytes == 0)
		return 0;

	chunk = MIN(max_bytes, (size_t)(buf_end - *dst));
	memcpy(*dst, src, chunk);
	if (chunk < max_bytes) {
		memcpy(buf_start, src + chunk, max_bytes - chunk);
		*dst = buf_start + (max_bytes - chunk);
	} else {
		*dst += chunk;
		if (*dst >= buf_end)
			*dst = buf_start;
	}

	return max_bytes;
}

/**
 * \brief Source/sink API based process function for MFCC.
 *
 * Reads input audio from sof_source, runs STFT/Mel/DCT processing,
 * and writes feature data to the sink. In compress output mode, only
 * the actual feature data (header + coefficients) is committed without
 * zero padding. When VAD is enabled and detects silence in compress
 * mode, the output frame is suppressed entirely to avoid sending
 * redundant data to user space.
 *
 * In non-compress (legacy) mode, a full period of data is committed
 * with zero-fill padding. Output data that exceeds one period is
 * spanned across multiple periods using state->header_pending,
 * state->out_data_ptr, and state->out_remain.
 */
static int mfcc_process(struct processing_module *mod,
				 struct sof_source **sources, int num_of_sources,
				 struct sof_sink **sinks, int num_of_sinks)
{
	struct mfcc_comp_data *cd = module_get_private_data(mod);
	struct comp_dev *dev = mod->dev;
	struct mfcc_state *state = &cd->state;
	size_t source_avail;
	int frames;
	int num_ceps;
	size_t commit_bytes;
	void *sink_ptr;
	void *sink_start;
	size_t sink_buf_size;
	int ret;

	comp_dbg(dev, "start");

	source_avail = source_get_data_frames_available(sources[0]);
	frames = MIN(source_avail, cd->max_frames);
	if (frames == 0)
		return -ENODATA;

	/* Copy input audio from source to MFCC internal circular buffer */
	cd->source_func(sources[0], &state->buf, &state->emph, frames, state->source_channel);

	/* Run STFT and Mel/DCT processing */
	num_ceps = mfcc_stft_process(mod, cd);

	/* If new output produced, set up output pointers for multi-period draining.
	 * All output is int32 Q9.23: mel-only uses Q9.23 from mel_log_32,
	 * cepstral uses int16 Q9.7 left-shifted to Q9.23.
	 */
	if (num_ceps > 0) {
		if (state->mel_only) {
			state->out_data_ptr = state->mel_log_32;
		} else {
			/* Widen int16 Q9.7 cepstral coefficients to int32 Q9.23.
			 * Safe to copy forward: cepstral_coef is in fft_out while
			 * mel_log_32 is in fft_buf (separate scratch buffers).
			 */
			int k;

			for (k = 0; k < num_ceps; k++)
				state->mel_log_32[k] = (int32_t)state->cepstral_coef->data[k] << 16;

			state->out_data_ptr = state->mel_log_32;
		}

		state->out_remain = num_ceps;
		state->header_pending = true;
	}

	if (cd->config->compress_output) {
		/* Compress mode: commit only actual data bytes, no zero padding.
		 * When VAD is enabled and DTX is on, send a configurable
		 * number of trailing silence frames after speech ends, then
		 * suppress the rest until speech resumes.
		 */
		size_t out_bytes;

		if (num_ceps <= 0)
			return 0;

		out_bytes = sizeof(state->header) + num_ceps * sizeof(int32_t);

		if (cd->config->enable_vad && !cd->vad.is_speech) {
			state->vad_silence_count++;
			/* With DTX enabled, send trailing silence frames
			 * (configurable count) then suppress. After trailing
			 * frames, optionally send periodic silence updates
			 * at the configured interval. This gives the host
			 * enough silence to detect end-of-speech while
			 * keeping alive updates during long silence.
			 * Without DTX, output every frame regardless of VAD.
			 */
			if (cd->config->enable_dtx) {
				if (state->vad_silence_count > state->dtx_trailing_silence) {
					/* Check periodic silence frame send */
					if (state->dtx_silence_interval > 0) {
						state->dtx_silence_counter++;
						if (state->dtx_silence_counter >= state->dtx_silence_interval) {
							state->dtx_silence_counter = 0;
							goto send_frame;
						}
					}
					state->header_pending = false;
					state->out_remain = 0;
					return 0;
				}
			}
		} else {
			state->vad_silence_count = 0;
			state->dtx_silence_counter = 0;
		}

send_frame:
		commit_bytes = out_bytes;

		if (sink_get_free_size(sinks[0]) < commit_bytes)
			return -ENOSPC;

		ret = sink_get_buffer(sinks[0], commit_bytes, &sink_ptr,
				      &sink_start, &sink_buf_size);
		if (ret)
			return ret;

		uint8_t *dst = sink_ptr;

		mfcc_sink_write_bytes(&dst, sink_start, sink_buf_size,
				      (uint8_t *)&state->header, sizeof(state->header));
		mfcc_sink_write_bytes(&dst, sink_start, sink_buf_size,
				      (uint8_t *)state->out_data_ptr,
				      num_ceps * sizeof(int32_t));

		state->header_pending = false;
		state->out_remain = 0;
	} else {
		/* Legacy mode: commit full period with zero-fill padding.
		 * Output data may span multiple periods when the feature
		 * vector is larger than one period.
		 */
		commit_bytes = frames * source_get_frame_bytes(sources[0]);

		if (sink_get_free_size(sinks[0]) < commit_bytes)
			return -ENOSPC;

		ret = sink_get_buffer(sinks[0], commit_bytes, &sink_ptr,
				      &sink_start, &sink_buf_size);
		if (ret)
			return ret;

		/* Zero-fill entire period first */
		size_t bytes_to_end = (size_t)((uint8_t *)sink_start + sink_buf_size -
					       (uint8_t *)sink_ptr);
		if (bytes_to_end >= commit_bytes)
			memset(sink_ptr, 0, commit_bytes);
		else {
			memset(sink_ptr, 0, bytes_to_end);
			memset(sink_start, 0, commit_bytes - bytes_to_end);
		}

		uint8_t *dst = sink_ptr;
		size_t avail = commit_bytes;

		/* Write pending header */
		if (state->header_pending && avail > 0) {
			size_t hdr_size = sizeof(state->header);

			if (avail >= hdr_size) {
				mfcc_sink_write_bytes(&dst, sink_start, sink_buf_size,
						      (uint8_t *)&state->header, hdr_size);
				avail -= hdr_size;
				state->header_pending = false;
			}
		}

		/* Write pending feature data (always int32) */
		if (state->out_remain > 0 && avail > 0) {
			size_t data_bytes;
			size_t to_write;

			data_bytes = state->out_remain * sizeof(int32_t);
			to_write = MIN(data_bytes, avail) & ~(size_t)3;
			if (to_write > 0) {
				int n32;

				mfcc_sink_write_bytes(&dst, sink_start,
						      sink_buf_size,
						      (uint8_t *)state->out_data_ptr,
						      to_write);
				n32 = to_write / sizeof(int32_t);
				state->out_data_ptr += n32;
				state->out_remain -= n32;
			}
		}
	}

	sink_commit_buffer(sinks[0], commit_bytes);
	comp_dbg(dev, "done, produced %zu bytes", commit_bytes);
	return 0;
}

static int mfcc_prepare(struct processing_module *mod,
			struct sof_source **sources, int num_of_sources,
			struct sof_sink **sinks, int num_of_sinks)
{
	struct mfcc_comp_data *cd = module_get_private_data(mod);
	struct comp_buffer *sourceb;
	struct comp_buffer *sinkb;
	struct comp_dev *dev = mod->dev;
	enum sof_ipc_frame source_format;
	enum sof_ipc_frame sink_format;
	size_t data_size;
	int ret;

	comp_info(dev, "entry");

	/* MFCC component will only ever have 1 source and 1 sink buffer */
	sourceb = comp_dev_get_first_data_producer(dev);
	sinkb = comp_dev_get_first_data_consumer(dev);
	if (!sourceb || !sinkb) {
		comp_err(dev, "no source or sink");
		return -ENOTCONN;
	}

	/* get source data format */
	source_format = audio_stream_get_frm_fmt(&sourceb->stream);

	/* get sink data format and period bytes */
	sink_format = audio_stream_get_frm_fmt(&sinkb->stream);
	comp_info(dev, "source_format = %d, sink_format = %d", source_format, sink_format);

	cd->config = comp_get_data_blob(cd->model_handler, &data_size, NULL);

	/* Initialize MFCC, max_frames is set to dev->frames + 4 */
	if (cd->config && data_size > 0) {
		ret = mfcc_setup(mod, dev->frames + 4, audio_stream_get_rate(&sourceb->stream),
				 audio_stream_get_channels(&sourceb->stream));
		if (ret < 0) {
			comp_err(dev, "setup failed.");
			return ret;
		}
	} else {
		comp_err(dev, "configuration is missing.");
		return -EINVAL;
	}

	cd->source_func = mfcc_find_source_func(source_format);
	if (!cd->source_func) {
		comp_err(dev, "No source func");
		mfcc_free_buffers(mod);
		return -EINVAL;
	}

	cd->source_format = source_format;

	if (cd->config->compress_output)
		comp_info(dev, "compress PCM output mode enabled");

	if (cd->config->enable_dtx && !cd->config->compress_output)
		comp_warn(dev, "enable_dtx ignored in normal PCM mode, only applies to compress");

	/* Initialize VAD switch control notification if enabled */
	if (cd->config->enable_vad && cd->config->update_controls) {
		if (!cd->msg) {
			ret = mfcc_ipc_notification_init(mod);
			if (ret < 0) {
				mfcc_free_buffers(mod);
				return ret;
			}
		}
	}

	cd->vad_prev = false;
	return 0;
}

static int mfcc_reset(struct processing_module *mod)
{
	struct mfcc_comp_data *cd = module_get_private_data(mod);

	comp_info(mod->dev, "entry");

	/* Free MFCC buffers to prevent leaks on reset->prepare cycles.
	 * mfcc_free_buffers() NULLs the pointers after free.
	 */
	mfcc_free_buffers(mod);

	/* Reset to similar state as init() */
	cd->source_func = NULL;
	return 0;
}

static const struct module_interface mfcc_interface = {
	.init = mfcc_init,
	.free = mfcc_free,
	.set_configuration = mfcc_set_config,
	.get_configuration = mfcc_get_config,
	.process = mfcc_process,
	.prepare = mfcc_prepare,
	.reset = mfcc_reset,
};

#if CONFIG_COMP_MFCC_MODULE
/* modular: llext dynamic link */

#include <module/module/api_ver.h>
#include <module/module/llext.h>
#include <rimage/sof/user/manifest.h>

static const struct sof_man_module_manifest mod_manifest __section(".module") __used =
	SOF_LLEXT_MODULE_MANIFEST("MFCC", &mfcc_interface, 1, SOF_REG_UUID(mfcc), 40);

SOF_LLEXT_BUILDINFO;

#else

DECLARE_TR_CTX(mfcc_tr, SOF_UUID(mfcc_uuid), LOG_LEVEL_INFO);
DECLARE_MODULE_ADAPTER(mfcc_interface, mfcc_uuid, mfcc_tr);
SOF_MODULE_INIT(mfcc, sys_comp_module_mfcc_interface_init);

#endif
