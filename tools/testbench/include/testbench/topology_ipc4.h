/* SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright(c) 2024 Intel Corporation. All rights reserved.
 */

#ifndef _TESTBENCH_TOPOLOGY_IPC4_H
#define _TESTBENCH_TOPOLOGY_IPC4_H

#include <module/ipc4/base-config.h>
#include "testbench/common_test.h"

#define TB_FAKE_IPC	0

struct sof_ipc4_process {
	struct ipc4_base_module_cfg base_config;
	struct ipc4_base_module_cfg_ext *base_config_ext;
	struct ipc4_audio_format output_format;
	struct sof_ipc4_available_audio_format available_fmt;
	void *ipc_config_data;
	uint32_t ipc_config_size;
	struct ipc4_module_init_instance msg;
	uint32_t base_config_ext_size;
	uint32_t init_config;
};

int tb_parse_ipc4_comp_tokens(struct testbench_prm *tp, struct ipc4_base_module_cfg *base_cfg);

void tb_setup_widget_ipc_msg(struct tplg_comp_info *comp_info);

int tb_set_up_widget_ipc(struct testbench_prm *tb, struct tplg_comp_info *comp_info);

int tb_set_up_route(struct testbench_prm *tb, struct tplg_route_info *route_info);

int tb_set_up_pipeline(struct testbench_prm *tb, struct tplg_pipeline_info *pipe_info);

void tb_pipeline_update_resource_usage(struct testbench_prm *tb,
					      struct tplg_comp_info *comp_info);

int tb_is_single_format(struct sof_ipc4_pin_format *fmts, int num_formats);

int tb_match_audio_format(struct testbench_prm *tb, struct tplg_comp_info *comp_info,
				 struct tb_config *config);

int tb_set_up_widget_base_config(struct testbench_prm *tb, struct tplg_comp_info *comp_info);

#endif /* _TESTBENCH_TOPOLOGY_IPC4_H */
