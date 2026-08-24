// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2026 Intel Corporation. All rights reserved.

#ifndef __MWW_MODEL_DATA_H__
#define __MWW_MODEL_DATA_H__

#if !CONFIG_COMP_MWW_MODEL_FROM_CONTROL
#include <cstdint>

constexpr unsigned int g_mww_model_data_size = 52272;
extern const unsigned char g_mww_model_data[];
#endif

#endif
