// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2020-2025 Intel Corporation. All rights reserved.
//
// Author: Seppo Ingalsuo <seppo.ingalsuo@linux.intel.com>

struct fft_plan *fft_plan_common_new(struct processing_module *mod, void *inb,
				     void *outb, uint32_t size, int bits);

void fft_plan_init_bit_reverse(uint16_t *bit_reverse_idx, int size, int len);
