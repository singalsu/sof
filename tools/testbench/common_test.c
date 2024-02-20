// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2018-2024 Intel Corporation. All rights reserved.

#include <platform/lib/ll_schedule.h>
#include <sof/audio/component_ext.h>
#include <sof/audio/pipeline.h>
#include <sof/ipc/driver.h>
#include <sof/ipc/topology.h>
#include <sof/lib/agent.h>
#include <sof/lib/dai.h>
#include <sof/lib/dma.h>
#include <sof/lib/notifier.h>
#include <sof/schedule/edf_schedule.h>
#include <sof/schedule/ll_schedule.h>
#include <sof/schedule/ll_schedule_domain.h>
#include <sof/schedule/schedule.h>
#include <rtos/alloc.h>
#include <rtos/sof.h>
#include <rtos/string.h>
#include <rtos/task.h>
#include <rtos/wait.h>
#include <tplg_parser/topology.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "testbench/common_test.h"
#include "testbench/file.h"
#include "testbench/trace.h"
#include "testbench/topology.h"

#if defined __XCC__
#include <xtensa/tie/xt_timer.h>
#endif

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

	debug_print("ipc and scheduler initialized\n");

	return 0;
}

struct ipc_data {
	struct ipc_data_host_buffer dh_buffer;
};

void tb_free(struct sof *sof)
{
	struct schedule_data *sch;
	struct schedulers **schedulers;
	struct list_item *slist, *_slist;
	struct notify **notify = arch_notify_get();
	struct ipc_data *iipc;

	free(*notify);

	/* free all scheduler data */
	schedule_free(0);
	schedulers = arch_schedulers_get();
	list_for_item_safe(slist, _slist, &(*schedulers)->list) {
		sch = container_of(slist, struct schedule_data, list);
		free(sch);
	}
	free(*arch_schedulers_get());

	/* free IPC data */
	iipc = sof->ipc->private;
	free(sof->ipc->comp_data);
	free(iipc->dh_buffer.page_table);
	free(iipc);
	free(sof->ipc);
}

/* Get pipeline host component */
static struct comp_dev *tb_get_pipeline_host(struct pipeline *p)
{
	struct comp_dev *cd;

	cd = p->source_comp;
	if (cd->direction == SOF_IPC_STREAM_CAPTURE)
		cd = p->sink_comp;

	return cd;
}

/* set up pcm params, prepare and trigger pipeline */
int tb_pipeline_start(struct ipc *ipc, struct pipeline *p)
{
	struct comp_dev *cd;
	int ret;

	/* Get pipeline host component */
	cd = tb_get_pipeline_host(p);

	/* Component prepare */
	ret = pipeline_prepare(p, cd);
	if (ret < 0) {
		fprintf(stderr, "error: Failed prepare pipeline command: %s\n",
			strerror(ret));
		return ret;
	}

	/* Start the pipeline */
	ret = pipeline_trigger(cd->pipeline, cd, COMP_TRIGGER_PRE_START);
	if (ret < 0) {
		fprintf(stderr, "error: Failed to start pipeline command: %s\n",
			strerror(ret));
		return ret;
	}

	return ret;
}

/* set up pcm params, prepare and trigger pipeline */
int tb_pipeline_stop(struct ipc *ipc, struct pipeline *p)
{
	struct comp_dev *cd;
	int ret;

	/* Get pipeline host component */
	cd = tb_get_pipeline_host(p);

	ret = pipeline_trigger(cd->pipeline, cd, COMP_TRIGGER_STOP);
	if (ret < 0) {
		fprintf(stderr, "error: Failed to stop pipeline command: %s\n",
			strerror(ret));
	}

	return ret;
}

/* set up pcm params, prepare and trigger pipeline */
int tb_pipeline_reset(struct ipc *ipc, struct pipeline *p)
{
	struct comp_dev *cd;
	int ret;

	/* Get pipeline host component */
	cd = tb_get_pipeline_host(p);

	ret = pipeline_reset(p, cd);
	if (ret < 0)
		fprintf(stderr, "error: pipeline reset\n");

	return ret;
}

/* pipeline pcm params */
int tb_pipeline_params(struct testbench_prm *tp, struct ipc *ipc, struct pipeline *p)
{
	struct comp_dev *cd;
	struct sof_ipc_pcm_params params = {{0}};
	char message[DEBUG_MSG_LEN];
	int fs_period;
	int period;
	int ret = 0;

	if (!p) {
		fprintf(stderr, "error: pipeline is NULL\n");
		return -EINVAL;
	}

	period = p->period;

	/* Compute period from sample rates */
	fs_period = (int)(0.9999 + tp->fs_in * period / 1e6);
	sprintf(message, "period sample count %d\n", fs_period);
	debug_print(message);

	/* set pcm params */
	params.comp_id = p->comp_id;
	params.params.buffer_fmt = SOF_IPC_BUFFER_INTERLEAVED;
	params.params.frame_fmt = tp->frame_fmt;
	params.params.rate = tp->fs_in;
	params.params.channels = tp->channels_in;

	switch (params.params.frame_fmt) {
	case SOF_IPC_FRAME_S16_LE:
		params.params.sample_container_bytes = 2;
		params.params.sample_valid_bytes = 2;
		break;
	case SOF_IPC_FRAME_S24_4LE:
		params.params.sample_container_bytes = 4;
		params.params.sample_valid_bytes = 3;
		break;
	case SOF_IPC_FRAME_S32_LE:
		params.params.sample_container_bytes = 4;
		params.params.sample_valid_bytes = 4;
		break;
	default:
		fprintf(stderr, "error: invalid frame format\n");
		return -EINVAL;
	}

	params.params.host_period_bytes = fs_period * params.params.channels *
		params.params.sample_container_bytes;

	/* Get pipeline host component */
	cd = tb_get_pipeline_host(p);

	/* Set pipeline params direction from scheduling component */
	params.params.direction = cd->direction;

	printf("test params: rate %d channels %d format %d\n",
	       params.params.rate, params.params.channels,
	       params.params.frame_fmt);

	/* pipeline params */
	ret = pipeline_params(p, cd, &params);
	if (ret < 0)
		fprintf(stderr, "error: pipeline_params\n");

	return ret;
}

/* print debug messages */
void debug_print(char *message)
{
	if (host_trace_level >= LOG_LEVEL_DEBUG)
		printf("debug: %s", message);
}

/* enable trace in testbench */
void tb_enable_trace(unsigned int log_level)
{
	host_trace_level = log_level;
	if (host_trace_level)
		debug_print("trace print enabled\n");
	else
		debug_print("trace print disabled\n");
}

void tb_gettime(struct timespec *td)
{
#if !defined __XCC__
	clock_gettime(CLOCK_MONOTONIC, td);
#else
	td->tv_nsec = 0;
	td->tv_sec = 0;
#endif
}

void tb_getcycles(uint64_t *cycles)
{
#if defined __XCC__
	*cycles = XT_RSR_CCOUNT();
#else
	*cycles = 0;
#endif
}

static int tb_get_instance_id(struct testbench_prm *tp, int id)
{
	struct tplg_pipeline_info *pipe_info;
	struct tplg_pipeline_list *pipeline_list;
	int i;

	pipeline_list = &tp->pcm_info->playback_pipeline_list;
	for (i = 0; i < pipeline_list->count; i++) {
		pipe_info = pipeline_list->pipelines[i];
		if (pipe_info->id == id)
			return pipe_info->instance_id;
	}

	pipeline_list = &tp->pcm_info->capture_pipeline_list;
	for (i = 0; i < pipeline_list->count; i++) {
		pipe_info = pipeline_list->pipelines[i];
		if (pipe_info->id == id)
			return pipe_info->instance_id;
	}

	return -EINVAL;
}

static struct pipeline *tb_get_pipeline_by_id(struct testbench_prm *tb, int pipeline_id)
{
	struct ipc_comp_dev *pipe_dev;
	struct ipc *ipc = sof_get()->ipc;
	int id = tb_get_instance_id(tb, pipeline_id);

	pipe_dev = ipc_get_comp_by_ppl_id(ipc, COMP_TYPE_PIPELINE, id, IPC_COMP_IGNORE_REMOTE);
	return pipe_dev->pipeline;
}

void tb_show_file_stats(int pipeline_id)
{
	struct list_item *clist;
	struct list_item *temp;
	struct ipc_comp_dev *icd;
	struct comp_dev *cd;
	struct dai_data *dd;
	struct file_comp_data *fcd;
	unsigned long time;

	/* get the file IO status for each file in pipeline */
	list_for_item_safe(clist, temp, &sof_get()->ipc->comp_list) {
		icd = container_of(clist, struct ipc_comp_dev, list);

		switch (icd->type) {
		case COMP_TYPE_COMPONENT:
			cd = icd->cd;
			if (cd->pipeline->pipeline_id != pipeline_id)
				break;
			switch (cd->drv->type) {
			case SOF_COMP_HOST:
			case SOF_COMP_DAI:
			case SOF_COMP_FILEREAD:
			case SOF_COMP_FILEWRITE:
				dd = comp_get_drvdata(cd);
				fcd = comp_get_drvdata(dd->dai);

				time = cd->pipeline->pipe_task->start;
				if (fcd->fs.copy_count == 0)
					fcd->fs.copy_count = 1;
				printf("file %s: id %d: type %d: samples %d copies %d total time %lu uS avg time %lu uS\n",
				       fcd->fs.fn, cd->ipc_config.id, cd->drv->type, fcd->fs.n,
				       fcd->fs.copy_count, time, time / fcd->fs.copy_count);
				break;
			default:
				break;
			}
			break;
		case COMP_TYPE_BUFFER:
		default:
			break;
		}
	}
}

#if DISABLED_CODE

/* free components */
static void test_pipeline_free_comps(int pipeline_id)
{
	struct list_item *clist;
	struct list_item *temp;
	struct ipc_comp_dev *icd = NULL;
	int err;

	/* remove the components for this pipeline */
	list_for_item_safe(clist, temp, &sof_get()->ipc->comp_list) {
		icd = container_of(clist, struct ipc_comp_dev, list);

		switch (icd->type) {
		case COMP_TYPE_COMPONENT:
			if (icd->cd->pipeline->pipeline_id != pipeline_id)
				break;
			err = ipc_comp_free(sof_get()->ipc, icd->id);
			if (err)
				fprintf(stderr, "failed to free comp %d\n",
					icd->id);
			break;
		case COMP_TYPE_BUFFER:
			if (icd->cb->pipeline_id != pipeline_id)
				break;
			err = ipc_buffer_free(sof_get()->ipc, icd->id);
			if (err)
				fprintf(stderr, "failed to free buffer %d\n", icd->id);
			break;
		default:
			if (icd->pipeline->pipeline_id != pipeline_id)
				break;
			err = ipc_pipeline_free(sof_get()->ipc, icd->id);
			if (err)
				fprintf(stderr, "failed to free pipeline %d\n",
					icd->id);
			break;
		}
	}
}

static void test_pipeline_set_test_limits(int pipeline_id, int max_copies,
					  int max_samples)
{
	struct list_item *clist;
	struct list_item *temp;
	struct ipc_comp_dev *icd = NULL;
	struct comp_dev *cd;
	struct dai_data *dd;
	struct file_comp_data *fcd;

	/* set the test limits for this pipeline */
	list_for_item_safe(clist, temp, &sof_get()->ipc->comp_list) {
		icd = container_of(clist, struct ipc_comp_dev, list);

		switch (icd->type) {
		case COMP_TYPE_COMPONENT:
			cd = icd->cd;
			if (cd->pipeline->pipeline_id != pipeline_id)
				break;

			switch (cd->drv->type) {
			case SOF_COMP_HOST:
			case SOF_COMP_DAI:
			case SOF_COMP_FILEREAD:
			case SOF_COMP_FILEWRITE:
				/* only file limits supported today. TODO: add others */
				dd = comp_get_drvdata(cd);
				fcd = comp_get_drvdata(dd->dai);
				fcd->max_samples = max_samples;
				fcd->max_copies = max_copies;
				break;
			default:
				break;
			}
			break;
		case COMP_TYPE_BUFFER:
		default:
			break;
		}
	}
}
#endif


#if DISABLED_CODE

static int test_pipeline_stop(struct testbench_prm *tp)
{
	struct pipeline *p;
	struct ipc *ipc = sof_get()->ipc;
	int ret = 0;
	int i;

	for (i = 0; i < tp->pipeline_num; i++) {
		p = get_pipeline_by_id(tp->pipelines[i]);
		ret = tb_pipeline_stop(ipc, p);
		if (ret < 0)
			break;
	}

	return ret;
}

static int test_pipeline_reset(struct testbench_prm *tp)
{
	struct pipeline *p;
	struct ipc *ipc = sof_get()->ipc;
	int ret = 0;
	int i;

	for (i = 0; i < tp->pipeline_num; i++) {
		p = get_pipeline_by_id(tp->pipelines[i]);
		ret = tb_pipeline_reset(ipc, p);
		if (ret < 0)
			break;
	}

	return ret;
}

static void test_pipeline_free(struct testbench_prm *tp)
{
	int i;

	for (i = 0; i < tp->pipeline_num; i++)
		test_pipeline_free_comps(tp->pipelines[i]);
}

static int test_pipeline_params(struct testbench_prm *tp)
{
	struct ipc_comp_dev *pcm_dev;
	struct pipeline *p;
	struct ipc *ipc = sof_get()->ipc;
	int ret = 0;
	int i;

	/* Run pipeline until EOF from fileread */

	for (i = 0; i < tp->pipeline_num; i++) {
		pcm_dev = ipc_get_ppl_src_comp(ipc, tp->pipelines[i]);
		if (!pcm_dev) {
			fprintf(stderr, "error: pipeline %d has no source component\n",
				tp->pipelines[i]);
			return -EINVAL;
		}

		/* set up pipeline params */
		p = pcm_dev->cd->pipeline;

		/* input and output sample rate */
		if (!tp->fs_in)
			tp->fs_in = p->period * p->frames_per_sched;

		if (!tp->fs_out)
			tp->fs_out = p->period * p->frames_per_sched;

		ret = tb_pipeline_params(tp, ipc, p);
		if (ret < 0) {
			fprintf(stderr, "error: pipeline params failed: %s\n",
				strerror(ret));
			return ret;
		}
	}


	return 0;
}

static int test_pipeline_start(struct testbench_prm *tp)
{
	struct pipeline *p;
	struct ipc *ipc = sof_get()->ipc;
	int i;

	/* Run pipeline until EOF from fileread */
	for (i = 0; i < tp->pipeline_num; i++) {
		p = get_pipeline_by_id(tp->pipelines[i]);

		/* do we need to apply copy count limit ? */
		if (tp->copy_check)
			test_pipeline_set_test_limits(tp->pipelines[i], tp->copy_iterations, 0);

		/* set pipeline params and trigger start */
		if (tb_pipeline_start(ipc, p) < 0) {
			fprintf(stderr, "error: pipeline params\n");
			return -EINVAL;
		}
	}

	return 0;
}
#endif

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

int tb_load_topology(struct testbench_prm *tb)
{
	struct tplg_context *ctx = &tb->tplg;
	int ret;

	/* setup the thread virtual core config */
	memset(ctx, 0, sizeof(*ctx));
	ctx->comp_id = 1;
	ctx->core_id = 0;
	ctx->sof = sof_get();
	ctx->tplg_file = tb->tplg_file;
	if (tb->ipc_version < 3 || tb->ipc_version > 4) {
		fprintf(stderr, "error: illegal ipc version\n");
		return -EINVAL;
	}

	ctx->ipc_major = tb->ipc_version;

	/* parse topology file and create pipeline */
	ret = tb_parse_topology(tb);
	if (ret < 0)
		fprintf(stderr, "error: parsing topology\n");

	fprintf(stdout, "topology parsing complete\n");
	return 0;
}


bool tb_schedule_pipeline_check_state(struct testbench_prm *tp, int state)
{
	struct pipeline *p;
	uint64_t cycles0, cycles1;
	int i;
	bool equal_state = true;

	tb_getcycles(&cycles0);

	schedule_ll_run_tasks();

	tb_getcycles(&cycles1);
	tp->total_cycles += cycles1 - cycles0;

	/* Run pipeline until EOF from fileread */
	for (i = 0; i < tp->pipeline_num; i++) {
		p = tb_get_pipeline_by_id(tp, tp->pipelines[i]);
		if (p->pipe_task->state != state) {
			fprintf(stderr, "pipeline %d state is %d expexted %d\n",
				tp->pipelines[i], p->pipe_task->state, state);
			equal_state = false;
		}
	}

	return equal_state;
}


