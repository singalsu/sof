// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2020 Intel Corporation. All rights reserved.
//
// Author: Amery Song <chao.song@intel.com>
//	   Keyon Jie <yang.jie@linux.intel.com>

#include <sof/audio/format.h>
#include <sof/common.h>
#include <rtos/alloc.h>
#include <sof/math/fft.h>

#ifdef FFT_GENERIC
#include <sof/audio/coefficients/fft/twiddle_1024_1536_32.h>
#include <sof/audio/coefficients/fft/twiddle_32.h>

#define DFT3_COEFR -1073741824	/* int32(-0.5 * 2^31) */
#define DFT3_COEFI  1859775393	/* int32(sqrt(3) / 2 * 2^31) */

/*
 * These helpers are optimized for FFT calculation only.
 * e.g. _add/sub() assume the output won't be saturate so no check needed,
 * and _mul() assumes Q1.31 * Q1.31 so the output will be shifted to be Q1.31.
 */

static inline void icomplex32_add(const struct icomplex32 *in1, const struct icomplex32 *in2,
				  struct icomplex32 *out)
{
	out->real = in1->real + in2->real;
	out->imag = in1->imag + in2->imag;
}

static inline void icomplex32_sub(const struct icomplex32 *in1, const struct icomplex32 *in2,
				  struct icomplex32 *out)
{
	out->real = in1->real - in2->real;
	out->imag = in1->imag - in2->imag;
}

static inline void icomplex32_mul(const struct icomplex32 *in1, const struct icomplex32 *in2,
				  struct icomplex32 *out)
{
	out->real = ((int64_t)in1->real * in2->real - (int64_t)in1->imag * in2->imag) >> 31;
	out->imag = ((int64_t)in1->real * in2->imag + (int64_t)in1->imag * in2->real) >> 31;
}

/* complex conjugate */
static inline void icomplex32_conj(struct icomplex32 *comp)
{
	comp->imag = SATP_INT32((int64_t)-1 * comp->imag);
}

/* shift a complex n bits, n > 0: left shift, n < 0: right shift */
static inline void icomplex32_shift(const struct icomplex32 *input, int32_t n,
				    struct icomplex32 *output)
{
	if (n > 0) {
		/* need saturation handling */
		output->real = SATP_INT32(SATM_INT32((int64_t)input->real << n));
		output->imag = SATP_INT32(SATM_INT32((int64_t)input->imag << n));
	} else {
		output->real = input->real >> -n;
		output->imag = input->imag >> -n;
	}
}

/**
 * \brief Execute the 32-bits Fast Fourier Transform (FFT) or Inverse FFT (IFFT)
 *	  For the configured fft_pan.
 * \param[in] plan - pointer to fft_plan which will be executed.
 * \param[in] ifft - set to 1 for IFFT and 0 for FFT.
 */
void fft_execute_32(struct fft_plan *plan, bool ifft)
{
	struct icomplex32 tmp1;
	struct icomplex32 tmp2;
	struct icomplex32 *inb;
	struct icomplex32 *outb;
	int depth;
	int top;
	int bottom;
	int index;
	int i;
	int j;
	int k;
	int m;
	int n;

	if (!plan || !plan->bit_reverse_idx)
		return;

	inb = plan->inb32;
	outb = plan->outb32;
	if (!inb || !outb)
		return;

	/* convert to complex conjugate for ifft */
	if (ifft) {
		for (i = 0; i < plan->size; i++)
			icomplex32_conj(&inb[i]);
	}

	/* step 1: re-arrange input in bit reverse order, and shrink the level to avoid overflow */
	for (i = 1; i < plan->size; ++i)
		icomplex32_shift(&inb[i], -(plan->len), &outb[plan->bit_reverse_idx[i]]);

	/* step 2: loop to do FFT transform in smaller size */
	for (depth = 1; depth <= plan->len; ++depth) {
		m = 1 << depth;
		n = m >> 1;
		i = FFT_SIZE_MAX >> depth;

		/* doing FFT transforms in size m */
		for (k = 0; k < plan->size; k += m) {
			/* doing one FFT transform for size m */
			for (j = 0; j < n; ++j) {
				index = i * j;
				top = k + j;
				bottom = top + n;
				tmp1.real = twiddle_real_32[index];
				tmp1.imag = twiddle_imag_32[index];
				/* calculate the accumulator: twiddle * bottom */
				icomplex32_mul(&tmp1, &outb[bottom], &tmp2);
				tmp1 = outb[top];
				/* calculate the top output: top = top + accumulate */
				icomplex32_add(&tmp1, &tmp2, &outb[top]);
				/* calculate the bottom output: bottom = top - accumulate */
				icomplex32_sub(&tmp1, &tmp2, &outb[bottom]);
			}
		}
	}

	/* shift back for ifft */
	if (ifft) {
		/*
		 * no need to divide N as it is already done in the input side
		 * for Q1.31 format. Instead, we need to multiply N to compensate
		 * the shrink we did in the FFT transform.
		 */
		for (i = 0; i < plan->size; i++)
			icomplex32_shift(&outb[i], plan->len, &outb[i]);
	}
}

void sofm_dft3_32(struct icomplex32 *x, struct icomplex32 *y)
{
	const struct icomplex32 c0 = {DFT3_COEFR, -DFT3_COEFI};
	const struct icomplex32 c1 = {DFT3_COEFR,  DFT3_COEFI};
	struct icomplex32 tmp1;
	struct icomplex32 tmp2;
	int i;

	for (i = 0; i < 3; i++) {
		x[i].real = Q_SHIFT_RND(x[i].real, 31, 29);
		x[i].imag = Q_SHIFT_RND(x[i].imag, 31, 29);
	}

	/*
	 *      | 1   1   1 |
	 * c =  | 1  c0  c1 | , x = [ x0 x1 x2 ]
	 *      | 1  c1  c0 |
	 *
	 * y(0) = c(0,0) * x(0) + c(1,0) * x(1) + c(2,0) * x(0)
	 * y(1) = c(0,1) * x(0) + c(1,1) * x(1) + c(2,1) * x(0)
	 * y(2) = c(0,2) * x(0) + c(1,2) * x(1) + c(2,2) * x(0)
	 */

	/* y(0) = 1 * x(0) + 1 * x(1) + 1 * x(2) */
	icomplex32_add(&x[0], &x[1], &tmp1);
	icomplex32_add(&x[2], &tmp1, &y[0]);

	/* y(1) = 1 * x(0) + c0 * x(1) + c1 * x(2) */
	icomplex32_mul(&c0, &x[1], &tmp1);
	icomplex32_mul(&c1, &x[2], &tmp2);
	icomplex32_add(&tmp1, &tmp2, &y[1]);
	icomplex32_add(&x[0], &y[1], &y[1]);

	/* y(2) = 1 * x(0) + c1 * x(1) + c0 * x(2) */
	icomplex32_mul(&c1, &x[1], &tmp1);
	icomplex32_mul(&c0, &x[2], &tmp2);
	icomplex32_add(&tmp1, &tmp2, &y[2]);
	icomplex32_add(&x[0], &y[2], &y[2]);
}

void fft_multi_execute_32(struct fft_multi_plan *plan, bool ifft)
{
	struct icomplex32 x[FFT_MULTI_COUNT_MAX];
	struct icomplex32 y[FFT_MULTI_COUNT_MAX];
	struct icomplex32 t;
	int i, j, k;

	/* Handle 2^N FFT */
	if (plan->num_ffts == 1) {
		fft_execute_32(plan->fft_plan[0], ifft);
		return;
	}

	/* convert to complex conjugate for IFFT */
	if (ifft) {
		for (i = 0; i < plan->total_size; i++)
			icomplex32_conj(&plan->inb32[i]);
	}

	/* Copy input buffers */
	k = 0;
	for (i = 0; i < plan->fft_size; i++)
		for (j = 0; j < plan->num_ffts; j++)
			*(plan->tmp_i32[j] + i) = plan->inb32[k++];

	/* Clear output buffers and call individual FFTs*/
	for (j = 0; j < plan->num_ffts; j++) {
		bzero(plan->tmp_o32[j], plan->fft_size * sizeof(struct icomplex32));
		fft_execute_32(plan->fft_plan[j], 0);
	}

	/* Multiply with twiddle factors */
	for (j = 1; j < plan->num_ffts; j++) {
		for (i = 0; i < plan->fft_size; i++) {
			k = j * i;
			t.real = multi_twiddle_real_32[k];
			t.imag = multi_twiddle_imag_32[k];
			icomplex32_mul(&t, plan->tmp_o32[j] + i, plan->tmp_o32[j] + i);
		}
	}

	/* DFT of size 3 */
	k = 0;
	for (i = 0; i < plan->fft_size; i++) {
		x[0].real = (plan->tmp_o32[0] + i)->real;
		x[0].imag = (plan->tmp_o32[0] + i)->imag;
		x[1].real = (plan->tmp_o32[1] + i)->real;
		x[1].imag = (plan->tmp_o32[1] + i)->imag;
		x[2].real = (plan->tmp_o32[2] + i)->real;
		x[2].imag = (plan->tmp_o32[2] + i)->imag;
		sofm_dft3_32(&x[0], &y[0]);
		plan->outb32[k].real = y[0].real;
		plan->outb32[k++].imag = y[0].imag;
		plan->outb32[k].real = y[1].real;
		plan->outb32[k++].imag = y[1].imag;
		plan->outb32[k].real = y[2].real;
		plan->outb32[k++].imag = y[2].imag;
	}

	/* shift back for IFFT */
	if (ifft) {
		/*
		 * no need to divide N as it is already done in the input side
		 * for Q1.31 format. Instead, we need to multiply N to compensate
		 * the shrink we did in the FFT transform.
		 */
		for (i = 0; i < plan->total_size; i++)
			icomplex32_shift(&plan->outb32[i], plan->fft_plan[0]->len,
					 &plan->outb32[i]);
	}
}

#endif
