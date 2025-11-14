// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2025 Intel Corporation. All rights reserved.
//
// Author: Seppo Ingalsuo <seppo.ingalsuo@linux.intel.com>

#include <sof/audio/format.h>
#include <sof/math/fft.h>
#include "ref_sofm_fft_multi_32.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>
#include <math.h>

#define SOFM_DFT3_MAX_ERROR_ABS		3.1
#define SOFM_DFT3_MAX_ERROR_RMS		1.1

struct processing_module dummy;

static void fft_multi_32_test(const int32_t *in_real, const int32_t *in_imag,
			      const int32_t *ref_real, const int32_t *ref_imag,
			      int num_bins, int num_tests)
{
	struct icomplex32 *x;
	struct icomplex32 *y;
	struct fft_multi_plan *plan;
	double delta;
	double error_rms;
	double delta_max = 0;
	double sum_squares = 0;
	const int32_t *p_in_real = in_real;
	const int32_t *p_in_imag = in_imag;
	const int32_t *p_ref_real = ref_real;
	const int32_t *p_ref_imag = ref_imag;
	int i, j;
	FILE *fh1, *fh2;

	x = malloc(num_bins * sizeof(struct icomplex32));
	if (!x) {
		fprintf(stderr, "Failed to allocate input data buffer.\n");
		assert_true(false);
	}

	y = malloc(num_bins * sizeof(struct icomplex32));
	if (!y) {
		fprintf(stderr, "Failed to allocate output data buffer.\n");
		assert_true(false);
	}

	plan = mod_fft_multi_plan_new(&dummy, x, y, num_bins, 32);
	if (!plan) {
		fprintf(stderr, "Failed to allocate FFT plan.\n");
		assert_true(false);
	}

	fh1 = fopen("debug_fft_multi_in.txt", "w");
	fh2 = fopen("debug_fft_multi_out.txt", "w");

	for (i = 0; i < num_tests; i++) {
		for (j = 0; j < num_bins; j++) {
			x[j].real = *p_in_real++;
			x[j].imag = *p_in_imag++;
			fprintf(fh1, "%d %d\n", x[j].real, x[j].imag);
		}

		fft_multi_execute_32(plan, 0);

		for (j = 0; j < num_bins; j++) {
			fprintf(fh2, "%d %d %d %d\n", y[j].real, y[j].imag, *p_ref_real, *p_ref_imag);
			delta = (double)*p_ref_real - (double)y[j].real;
			sum_squares += delta * delta;
			if (delta > delta_max)
				delta_max = delta;
			else if (-delta > delta_max)
				delta_max = -delta;

			delta = (double)*p_ref_imag - (double)y[j].imag;
			sum_squares += delta * delta;
			if (delta > delta_max)
				delta_max = delta;
			else if (-delta > delta_max)
				delta_max = -delta;

			p_ref_real++;
			p_ref_imag++;
		}

	}

	mod_fft_multi_plan_free(&dummy, plan);
	free(y);
	free(x);

	fclose(fh1);
	fclose(fh2);

	error_rms = sqrt(sum_squares / (double)(2 * num_bins * num_tests));
	printf("Max absolute error = %5.2f (max %5.2f), error RMS = %5.2f (max %5.2f)\n",
	       delta_max, SOFM_DFT3_MAX_ERROR_ABS, error_rms, SOFM_DFT3_MAX_ERROR_RMS);

	assert_true(error_rms < SOFM_DFT3_MAX_ERROR_RMS);
	assert_true(delta_max < SOFM_DFT3_MAX_ERROR_ABS);
}

static void fft_multi_32_test_1(void **state)
{
	(void)state;

	fft_multi_32_test(input_data_real_q31, input_data_imag_q31,
			  ref_data_real_q31, ref_data_imag_q31,
			  REF_SOFM_FFT_MULTI_N, REF_SOFM_FFT_MULTI_NUM_TESTS);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(fft_multi_32_test_1),
	};

	cmocka_set_message_output(CM_OUTPUT_TAP);

	return cmocka_run_group_tests(tests, NULL, NULL);
}
