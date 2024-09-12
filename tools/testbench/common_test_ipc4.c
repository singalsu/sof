// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2018-2024 Intel Corporation. All rights reserved.

#if CONFIG_IPC_MAJOR_4

#include <sof/audio/component_ext.h>
#include <sof/lib/notifier.h>
#include <sof/schedule/edf_schedule.h>
#include <sof/schedule/ll_schedule.h>
#include <sof/schedule/ll_schedule_domain.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "testbench/common_test.h"
#include "testbench/file.h"
#include "testbench/topology_ipc4.h"

#if defined __XCC__
#include <xtensa/tie/xt_timer.h>
#endif

SOF_DEFINE_REG_UUID(testbench);
DECLARE_TR_CTX(testbench_tr, SOF_UUID(testbench_uuid), LOG_LEVEL_INFO);
LOG_MODULE_REGISTER(testbench, CONFIG_SOF_LOG_LEVEL);

/* testbench helper functions for pipeline setup and trigger */

int tb_setup(struct sof *sof, struct testbench_prm *tp)
{
	struct ll_schedule_domain domain = {0};

	domain.next_tick = tp->tick_period_us;

	/* init components */
	sys_comp_init(sof);

	/* Module adapter components */
	sys_comp_module_crossover_interface_init();
	sys_comp_module_dcblock_interface_init();
	sys_comp_module_demux_interface_init();
	sys_comp_module_drc_interface_init();
	sys_comp_module_eq_fir_interface_init();
	sys_comp_module_eq_iir_interface_init();
	sys_comp_module_file_interface_init();
	sys_comp_module_gain_interface_init();
	sys_comp_module_google_rtc_audio_processing_interface_init();
	sys_comp_module_igo_nr_interface_init();
	sys_comp_module_mfcc_interface_init();
	sys_comp_module_multiband_drc_interface_init();
	sys_comp_module_mux_interface_init();
	sys_comp_module_rtnr_interface_init();
	sys_comp_module_selector_interface_init();
	sys_comp_module_src_interface_init();
	sys_comp_module_asrc_interface_init();
	sys_comp_module_tdfb_interface_init();
	sys_comp_module_volume_interface_init();

	/* other necessary initializations, todo: follow better SOF init */
	pipeline_posn_init(sof);
	init_system_notify(sof);

	/* init IPC */
	if (ipc_init(sof) < 0) {
		fprintf(stderr, "error: IPC init\n");
		return -EINVAL;
	}

	/* Trace */
	ipc_tr.level = LOG_LEVEL_INFO;
	ipc_tr.uuid_p = SOF_UUID(testbench_uuid);

	/* init LL scheduler */
	if (scheduler_init_ll(&domain) < 0) {
		fprintf(stderr, "error: edf scheduler init\n");
		return -EINVAL;
	}

	/* init EDF scheduler */
	if (scheduler_init_edf() < 0) {
		fprintf(stderr, "error: edf scheduler init\n");
		return -EINVAL;
	}

	tb_debug_print("ipc and scheduler initialized\n");

	// TODO move somewhere else and integrate with command line
	tp->num_configs = 1;
	strcpy(tp->config[0].name, "48k2c32b");
	tp->config[0].buffer_frames = 24000;
	tp->config[0].buffer_time = 0;
	tp->config[0].period_frames = 6000;
	tp->config[0].period_time = 0;
	tp->config[0].rate = 48000;
	tp->config[0].channels = 2;
	tp->config[0].format = SOF_IPC_FRAME_S32_LE;

	tp->ipc_version = 4;
	tp->period_size = 96;	// FIXME becomes somehow obs in tb_match_audio_format()
	tp->pcm_id = 0;

	return 0;
}

static int tb_prepare_widget(struct testbench_prm *tb, struct tplg_pcm_info *pcm_info,
			     struct tplg_comp_info *comp_info, int dir)
{
	struct tplg_pipeline_list *pipeline_list;
	int ret, i;

	if (dir)
		pipeline_list = &pcm_info->capture_pipeline_list;
	else
		pipeline_list = &pcm_info->playback_pipeline_list;

	/* populate base config */
	ret = tb_set_up_widget_base_config(tb, comp_info);
	if (ret < 0)
		return ret;

	tb_pipeline_update_resource_usage(tb, comp_info);

	/* add pipeline to pcm pipeline_list if needed */
	for (i = 0; i < pipeline_list->count; i++) {
		struct tplg_pipeline_info *pipe_info = pipeline_list->pipelines[i];

		if (pipe_info == comp_info->pipe_info)
			break;
	}

	if (i == pipeline_list->count) {
		pipeline_list->pipelines[pipeline_list->count] = comp_info->pipe_info;
		pipeline_list->count++;
	}

	return 0;
}

static int tb_prepare_widgets(struct testbench_prm *tb, struct tplg_pcm_info *pcm_info,
			      struct tplg_comp_info *starting_comp_info,
			      struct tplg_comp_info *current_comp_info)
{
	struct list_item *item;
	int ret;

	/* for playback */
	list_for_item(item, &tb->route_list) {
		struct tplg_route_info *route_info = container_of(item, struct tplg_route_info,
								  item);

		if (route_info->source != current_comp_info)
			continue;

		/* set up source widget if it is the starting widget */
		if (starting_comp_info == current_comp_info) {
			ret = tb_prepare_widget(tb, pcm_info, current_comp_info, 0);
			if (ret < 0)
				return ret;
		}

		/* set up the sink widget */
		ret = tb_prepare_widget(tb, pcm_info, route_info->sink, 0);
		if (ret < 0)
			return ret;

		/* and then continue down the path */
		if (route_info->sink->type != SND_SOC_TPLG_DAPM_DAI_IN ||
		    route_info->sink->type != SND_SOC_TPLG_DAPM_DAI_OUT) {
			ret = tb_prepare_widgets(tb, pcm_info, starting_comp_info,
						 route_info->sink);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

static int tb_prepare_widgets_capture(struct testbench_prm *tb, struct tplg_pcm_info *pcm_info,
				      struct tplg_comp_info *starting_comp_info,
				      struct tplg_comp_info *current_comp_info)
{
	struct list_item *item;
	int ret;

	/* for capture */
	list_for_item(item, &tb->route_list) {
		struct tplg_route_info *route_info = container_of(item, struct tplg_route_info,
								  item);

		if (route_info->sink != current_comp_info)
			continue;

		/* set up sink widget if it is the starting widget */
		if (starting_comp_info == current_comp_info) {
			ret = tb_prepare_widget(tb, pcm_info, current_comp_info, 1);
			if (ret < 0)
				return ret;
		}

		/* set up the source widget */
		ret = tb_prepare_widget(tb, pcm_info, route_info->source, 1);
		if (ret < 0)
			return ret;

		/* and then continue up the path */
		if (route_info->source->type != SND_SOC_TPLG_DAPM_DAI_IN &&
		    route_info->source->type != SND_SOC_TPLG_DAPM_DAI_OUT) {
			ret = tb_prepare_widgets_capture(tb, pcm_info, starting_comp_info,
							 route_info->source);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}


static int tb_set_up_widget(struct testbench_prm *tb, struct tplg_comp_info *comp_info)
{
	struct tplg_pipeline_info *pipe_info = comp_info->pipe_info;
	int ret;

	pipe_info->usage_count++;

	/* first set up pipeline if needed, only done once for the first pipeline widget */
	if (pipe_info->usage_count == 1) {
		ret = tb_set_up_pipeline(tb, pipe_info);
		if (ret < 0) {
			pipe_info->usage_count--;
			return ret;
		}
	}

	/* now set up the widget */
	ret = tb_set_up_widget_ipc(tb, comp_info);
	if (ret < 0)
		return ret;

	return 0;
}

static int tb_set_up_widgets(struct testbench_prm *tb, struct tplg_comp_info *starting_comp_info,
			     struct tplg_comp_info *current_comp_info)
{
	struct list_item *item;
	int ret;

	/* for playback */
	list_for_item(item, &tb->route_list) {
		struct tplg_route_info *route_info = container_of(item, struct tplg_route_info,
								  item);

		if (route_info->source != current_comp_info)
			continue;

		/* set up source widget if it is the starting widget */
		if (starting_comp_info == current_comp_info) {
			ret = tb_set_up_widget(tb, current_comp_info);
			if (ret < 0)
				return ret;
		}

		/* set up the sink widget */
		ret = tb_set_up_widget(tb, route_info->sink);
		if (ret < 0)
			return ret;

		/* source and sink widgets are up, so set up route now */
		ret = tb_set_up_route(tb, route_info);
		if (ret < 0)
			return ret;

		/* and then continue down the path */
		if (route_info->sink->type != SND_SOC_TPLG_DAPM_DAI_IN ||
		    route_info->sink->type != SND_SOC_TPLG_DAPM_DAI_OUT) {
			ret = tb_set_up_widgets(tb, starting_comp_info, route_info->sink);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

static int tb_set_up_widgets_capture(struct testbench_prm *tb,
				     struct tplg_comp_info *starting_comp_info,
				     struct tplg_comp_info *current_comp_info)
{
	struct list_item *item;
	int ret;

	/* for playback */
	list_for_item(item, &tb->route_list) {
		struct tplg_route_info *route_info = container_of(item, struct tplg_route_info,
								  item);

		if (route_info->sink != current_comp_info)
			continue;

		/* set up source widget if it is the starting widget */
		if (starting_comp_info == current_comp_info) {
			ret = tb_set_up_widget(tb, current_comp_info);
			if (ret < 0)
				return ret;
		}

		/* set up the sink widget */
		ret = tb_set_up_widget(tb, route_info->source);
		if (ret < 0)
			return ret;

		/* source and sink widgets are up, so set up route now */
		ret = tb_set_up_route(tb, route_info);
		if (ret < 0)
			return ret;

		/* and then continue down the path */
		if (route_info->source->type != SND_SOC_TPLG_DAPM_DAI_IN &&
		    route_info->source->type != SND_SOC_TPLG_DAPM_DAI_OUT) {
			ret = tb_set_up_widgets_capture(tb, starting_comp_info, route_info->source);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

static int tb_set_up_pipelines(struct testbench_prm *tb, int dir)
{
	struct tplg_comp_info *host = NULL;
	struct tplg_pcm_info *pcm_info;
	struct list_item *item;
	int ret;

	// TODO tb->pcm_id is not defined?
	list_for_item(item, &tb->pcm_list) {
		pcm_info = container_of(item, struct tplg_pcm_info, item);

		if (pcm_info->id == tb->pcm_id) {
			if (dir)
				host = pcm_info->capture_host;
			else
				host = pcm_info->playback_host;
			break;
		}
	}

	if (!host) {
		fprintf(stderr, "No host component found for PCM ID: %d\n", tb->pcm_id);
		return -EINVAL;
	}

	if (!tb_is_pipeline_enabled(tb, host->pipeline_id))
		return 0;

	tb->pcm_info = pcm_info; //  TODO must be an array

	if (dir) {
		ret = tb_prepare_widgets_capture(tb, pcm_info, host, host);
		if (ret < 0)
			return ret;

		ret = tb_set_up_widgets_capture(tb, host, host);
		if (ret < 0)
			return ret;

		tb_debug_print("Setting up capture pipelines complete\n");

		return 0;
	}

	ret = tb_prepare_widgets(tb, pcm_info, host, host);
	if (ret < 0)
		return ret;

	ret = tb_set_up_widgets(tb, host, host);
	if (ret < 0)
		return ret;

	tb_debug_print("Setting up playback pipelines complete\n");

	return 0;
}

int tb_set_up_all_pipelines(struct testbench_prm *tb)
{
	int ret;

	ret = tb_set_up_pipelines(tb, SOF_IPC_STREAM_PLAYBACK);
	if (ret) {
		fprintf(stderr, "error: Failed tb_set_up_pipelines for playback\n");
		return ret;
	}

	ret = tb_set_up_pipelines(tb, SOF_IPC_STREAM_CAPTURE);
	if (ret) {
		fprintf(stderr, "error: Failed tb_set_up_pipelines for capture\n");
		return ret;
	}

	fprintf(stdout, "pipelines set up complete\n");
	return 0;
}

static int tb_free_widgets(struct testbench_prm *tb, struct tplg_comp_info *starting_comp_info,
			   struct tplg_comp_info *current_comp_info)
{
	struct tplg_route_info *route_info;
	struct list_item *item;
	int ret;

	/* for playback */
	list_for_item(item, &tb->route_list) {
		route_info = container_of(item, struct tplg_route_info, item);
		if (route_info->source != current_comp_info)
			continue;

		/* Widgets will be freed when the pipeline is deleted, so just unbind modules */
		ret = tb_free_route(tb, route_info);
		if (ret < 0)
			return ret;

		/* and then continue down the path */
		if (route_info->sink->type != SND_SOC_TPLG_DAPM_DAI_IN ||
		    route_info->sink->type != SND_SOC_TPLG_DAPM_DAI_OUT) {
			ret = tb_free_widgets(tb, starting_comp_info, route_info->sink);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

static int tb_free_widgets_capture(struct testbench_prm *tb,
				   struct tplg_comp_info *starting_comp_info,
				   struct tplg_comp_info *current_comp_info)
{
	struct tplg_route_info *route_info;
	struct list_item *item;
	int ret;

	/* for playback */
	list_for_item(item, &tb->route_list) {
		route_info = container_of(item, struct tplg_route_info, item);
		if (route_info->sink != current_comp_info)
			continue;

		/* Widgets will be freed when the pipeline is deleted, so just unbind modules */
		ret = tb_free_route(tb, route_info);
		if (ret < 0)
			return ret;

		/* and then continue down the path */
		if (route_info->sink->type != SND_SOC_TPLG_DAPM_DAI_IN &&
		    route_info->sink->type != SND_SOC_TPLG_DAPM_DAI_OUT) {
			ret = tb_free_widgets_capture(tb, starting_comp_info, route_info->source);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

static int tb_free_pipelines(struct testbench_prm *tb, int dir)
{
	struct tplg_pipeline_list *pipeline_list;
	struct tplg_pcm_info *pcm_info;
	struct list_item *item;
	struct tplg_comp_info *host = NULL;
	int ret, i;

	list_for_item(item, &tb->pcm_list) {
		pcm_info = container_of(item, struct tplg_pcm_info, item);
		if (dir)
			host = pcm_info->capture_host;
		else
			host = pcm_info->playback_host;

		if (!host || !tb_is_pipeline_enabled(tb, host->pipeline_id))
			continue;

		if (dir) {
			pipeline_list = &tb->pcm_info->capture_pipeline_list;
			ret = tb_free_widgets_capture(tb, host, host);
			if (ret < 0) {
				fprintf(stderr, "failed to free widgets for capture PCM\n");
				return ret;
			}
		} else {
			pipeline_list = &tb->pcm_info->playback_pipeline_list;
			ret = tb_free_widgets(tb, host, host);
			if (ret < 0) {
				fprintf(stderr, "failed to free widgets for playback PCM\n");
				return ret;
			}
		}
		for (i = 0; i < pipeline_list->count; i++) {
			struct tplg_pipeline_info *pipe_info = pipeline_list->pipelines[i];

			ret = tb_delete_pipeline(tb, pipe_info);
			if (ret < 0)
				return ret;
		}
	}

	tb->instance_ids[SND_SOC_TPLG_DAPM_SCHEDULER] = 0;
	return 0;
}

int tb_free_all_pipelines(struct testbench_prm *tb)
{
	tb_debug_print("freeing playback direction\n");
	tb_free_pipelines(tb, SOF_IPC_STREAM_PLAYBACK);

	tb_debug_print("freeing capture direction\n");
	tb_free_pipelines(tb, SOF_IPC_STREAM_CAPTURE);
	return 0;
}

void tb_free_topology(struct testbench_prm *tb)
{
	struct tplg_pcm_info *pcm_info;
	struct tplg_comp_info *comp_info;
	struct tplg_route_info *route_info;
	struct tplg_pipeline_info *pipe_info;
	struct tplg_context *ctx = &tb->tplg;
	struct sof_ipc4_available_audio_format *available_fmts;
	struct list_item *item, *_item;

	list_for_item_safe(item, _item, &tb->pcm_list) {
		pcm_info = container_of(item, struct tplg_pcm_info, item);
		free(pcm_info->name);
		free(pcm_info);
	}

	list_for_item_safe(item, _item, &tb->widget_list) {
		comp_info = container_of(item, struct tplg_comp_info, item);
		available_fmts = &comp_info->available_fmt;
		free(available_fmts->output_pin_fmts);
		free(available_fmts->input_pin_fmts);
		free(comp_info->name);
		free(comp_info->stream_name);
		free(comp_info->ipc_payload);
		free(comp_info);
	}

	list_for_item_safe(item, _item, &tb->route_list) {
		route_info = container_of(item, struct tplg_route_info, item);
		free(route_info);
	}

	list_for_item_safe(item, _item, &tb->pipeline_list) {
		pipe_info = container_of(item, struct tplg_pipeline_info, item);
		free(pipe_info->name);
		free(pipe_info);
	}

	// TODO: Do here or earlier?
	free(ctx->tplg_base);
	tb_debug_print("freed all pipelines, widgets, routes and pcms\n");
}

#endif /* CONFIG_IPC_MAJOR_4 */
