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
#include <rtos/init.h>

LOG_MODULE_REGISTER(mfcc, CONFIG_SOF_LOG_LEVEL);

SOF_DEFINE_REG_UUID(mfcc);

__cold_rodata const struct mfcc_func_map mfcc_fm[] = {
#if CONFIG_FORMAT_S16LE
	{SOF_IPC_FRAME_S16_LE, mfcc_s16_default},
#endif /* CONFIG_FORMAT_S16LE */
#if CONFIG_FORMAT_S24LE
	{SOF_IPC_FRAME_S24_4LE, NULL},
#endif /* CONFIG_FORMAT_S24LE */
#if CONFIG_FORMAT_S32LE
	{SOF_IPC_FRAME_S32_LE, NULL},
#endif /* CONFIG_FORMAT_S32LE */
};

static mfcc_func mfcc_find_func(enum sof_ipc_frame source_format, enum sof_ipc_frame sink_format,
				const struct mfcc_func_map *map, int n)
{
	int i;

	/* Find suitable processing function from map. */
	for (i = 0; i < n; i++) {
		if (source_format == map[i].source) {
			return map[i].func;
		}
	}

	return NULL;
}

/*
 * End of MFCC setup code. Next the standard component methods.
 */

/**
 * mfcc_init() - Initialize the MFCC component.
 * @mod: Pointer to module data.
 *
 * This function is called when the instance is created. The
 * macro __cold informs that the code that is non-critical
 * is loaded to slower but large DRAM.
 *
 * Return: Zero if success, otherwise error code.
 */
__cold static int mfcc_init(struct processing_module *mod)
{
	struct module_data *md = &mod->priv;
	struct comp_dev *dev = mod->dev;
	struct mfcc_comp_data *cd = NULL;

	assert_can_be_cold();

	comp_info(dev, "entry");

	cd = mod_zalloc(mod, sizeof(*cd));
	if (!cd) {
		return -ENOMEM;
	}

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

/**
 * mfcc_free() - Free dynamic allocations.
 * @mod: Pointer to module data.
 *
 * Component free is called when the pipelines are deleted. All
 * dynamic allocations need to be freed here. The macro __cold
 * instructs the build to locate this performance wise non-critical
 * function to large and slower DRAM.
 *
 * Return: Value zero, always success.
 */
__cold static int mfcc_free(struct processing_module *mod)
{
	struct mfcc_comp_data *cd = module_get_private_data(mod);

	assert_can_be_cold();

	comp_dbg(mod->dev, "free");
	mfcc_free_buffers(mod);
	mod_data_blob_handler_free(mod, cd->model_handler);
	mod_free(mod, cd);
	return 0;
}

/**
 * mfcc_get_config() - Handle controls get.
 * @mod: Pointer to module data.
 * @config_id: Configuration ID.
 * @data_offset_size: Size of the whole configuration.
 * @fragment: Message payload data.
 * @fragment_size: Size of this fragment.
 *
 * Return: Zero if success, otherwise error code.
 */
static int mfcc_get_config(struct processing_module *mod, uint32_t config_id,
			   uint32_t *data_offset_size, uint8_t *fragment, size_t fragment_size)
{
	struct sof_ipc_ctrl_data *cdata = (struct sof_ipc_ctrl_data *)fragment;
	struct mfcc_comp_data *cd = module_get_private_data(mod);

	comp_dbg(mod->dev, "get_config");

	return comp_data_blob_get_cmd(cd->model_handler, cdata, fragment_size);
}

/**
 * mfcc_set_config() - Handle controls set.
 * @mod: Pointer to module data.
 * @config_id: Configuration ID.
 * @pos: Position of the fragment in the large message.
 * @data_offset_size: Size of the whole configuration or offset of fragment.
 * @fragment: Message payload data.
 * @fragment_size: Size of this fragment.
 * @response: Response data.
 * @response_size: Size of response.
 *
 * Return: Zero if success, otherwise error code.
 */
static int mfcc_set_config(struct processing_module *mod, uint32_t config_id,
			   enum module_cfg_fragment_position pos, uint32_t data_offset_size,
			   const uint8_t *fragment, size_t fragment_size, uint8_t *response,
			   size_t response_size)
{
	struct mfcc_comp_data *cd = module_get_private_data(mod);

	comp_dbg(mod->dev, "set_config");

	return comp_data_blob_set(cd->model_handler, pos, data_offset_size, fragment,
				  fragment_size);
}

/**
 * mfcc_process() - The audio data processing function.
 * @mod: Pointer to module data.
 * @input_buffers: Pointer to audio input stream buffers.
 * @num_input_buffers: Number of input buffers.
 * @output_buffers: Pointer to audio output stream buffers.
 * @num_output_buffers: Number of output buffers.
 *
 * This is the processing function that is called for scheduled
 * pipelines.
 *
 * Return: Zero if success, otherwise error code.
 */
static int mfcc_process(struct processing_module *mod, struct input_stream_buffer *input_buffers,
			int num_input_buffers, struct output_stream_buffer *output_buffers,
			int num_output_buffers)
{
	struct mfcc_comp_data *cd = module_get_private_data(mod);
	struct audio_stream *source = input_buffers->data;
	struct audio_stream *sink = output_buffers->data;
	int frames = input_buffers->size;

	frames = MIN(frames, cd->max_frames);
	cd->mfcc_func(mod, input_buffers, output_buffers, frames);

	input_buffers->consumed += audio_stream_frame_bytes(source) * frames;
	output_buffers->size += audio_stream_frame_bytes(sink) * frames;
	return 0;
}

/**
 * mfcc_prepare() - Prepare the component for processing.
 * @mod: Pointer to module data.
 * @sources: Pointer to audio samples data sources array.
 * @num_of_sources: Number of sources in the array.
 * @sinks: Pointer to audio samples data sinks array.
 * @num_of_sinks: Number of sinks in the array.
 *
 * Function prepare is called just before the pipeline is started.
 * The audio format parameters are retrieved and the processing
 * function pointer is set.
 *
 * Return: Value zero if success, otherwise error code.
 */
static int mfcc_prepare(struct processing_module *mod, struct sof_source **sources,
			int num_of_sources, struct sof_sink **sinks, int num_of_sinks)
{
	struct mfcc_comp_data *cd = module_get_private_data(mod);
	struct comp_buffer *sourceb;
	struct comp_buffer *sinkb;
	struct comp_dev *dev = mod->dev;
	enum sof_ipc_frame source_format;
	enum sof_ipc_frame sink_format;
	size_t data_size;
	uint32_t sink_period_bytes;
	int ret;

	comp_dbg(dev, "prepare");

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
	sink_period_bytes = audio_stream_period_bytes(&sinkb->stream, dev->frames);
	comp_info(dev, "source_format = %d, sink_format = %d", source_format, sink_format);
	if (audio_stream_get_size(&sinkb->stream) < sink_period_bytes) {
		comp_err(dev, "sink buffer size %d is insufficient < %d",
			 audio_stream_get_size(&sinkb->stream), sink_period_bytes);
		ret = -ENOMEM;
		goto err;
	}

	cd->config = comp_get_data_blob(cd->model_handler, &data_size, NULL);

	/* Initialize MFCC, max_frames is set to dev->frames + 4 */
	if (cd->config && data_size > 0) {
		ret = mfcc_setup(mod, dev->frames + 4, audio_stream_get_rate(&sourceb->stream),
				 audio_stream_get_channels(&sourceb->stream));
		if (ret < 0) {
			comp_err(dev, "setup failed.");
			goto err;
		}
	}

	cd->mfcc_func = mfcc_find_func(source_format, sink_format, mfcc_fm, ARRAY_SIZE(mfcc_fm));
	if (!cd->mfcc_func) {
		comp_err(dev, "No proc func");
		ret = -EINVAL;
		goto err;
	}

	return 0;

err:
	comp_set_state(dev, COMP_TRIGGER_RESET);
	return ret;
}

/**
 * mfcc_reset() - Reset the component.
 * @mod: Pointer to module data.
 *
 * The component reset is called when pipeline is stopped. The reset
 * should return the component to same state as init.
 *
 * Return: Value zero, always success.
 */
static int mfcc_reset(struct processing_module *mod)
{
	struct mfcc_comp_data *cd = module_get_private_data(mod);

	comp_dbg(mod->dev, "reset");

	mfcc_free_buffers(mod);
	cd->mfcc_func = NULL;
	return 0;
}

static const struct module_interface mfcc_interface = {
	.init = mfcc_init,
	.free = mfcc_free,
	.set_configuration = mfcc_set_config,
	.get_configuration = mfcc_get_config,
	.process_audio_stream = mfcc_process,
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
