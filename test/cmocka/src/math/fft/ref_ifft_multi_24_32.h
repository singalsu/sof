/* SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright(c) 2025 Intel Corporation.
 */

/* Created 19-Nov-2025 19:49:04 with script ref_fft_multi.m v1.9-rc1-6867-gbc168aba3-dirty */

#define REF_SOFM_IFFT_MULTI_24_NUM_TESTS  1

static const int32_t ifft_in_real_24_q31[24] = {
	  536870912,           0,           0,           0,           0,           0,
	          0,           0,           0,           0,           0,           0,
	          0,           0,           0,           0,           0,           0,
	          0,           0,           0,           0,           0,           0,
};

static const int32_t ifft_in_imag_24_q31[24] = {
	          0,           0,           0,           0,           0,           0,
	          0,           0,           0,           0,           0,           0,
	          0,           0,           0,           0,           0,           0,
	          0,           0,           0,           0,           0,           0,
};

static const int32_t ifft_ref_real_24_q31[24] = {
	  536870912,   536870912,   536870912,   536870912,   536870912,   536870912,
	  536870912,   536870912,   536870912,   536870912,   536870912,   536870912,
	  536870912,   536870912,   536870912,   536870912,   536870912,   536870912,
	  536870912,   536870912,   536870912,   536870912,   536870912,   536870912,
};

static const int32_t ifft_ref_imag_24_q31[24] = {
	          0,           0,           0,           0,           0,           0,
	          0,           0,           0,           0,           0,           0,
	          0,           0,           0,           0,           0,           0,
	          0,           0,           0,           0,           0,           0,
};
