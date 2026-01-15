// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2020-2026 Intel Corporation.
//
// Author: Amery Song <chao.song@intel.com>
//	   Keyon Jie <yang.jie@linux.intel.com>

#include <sof/audio/format.h>
#include <sof/math/exp_fcn.h>
#include <sof/math/log.h>
#include <sof/math/trig.h>
#include <sof/common.h>
#include <stdint.h>

#ifndef __SOF_ICOMPLEX32_H__
#define __SOF_ICOMPLEX32_H__

/**
 * struct icomplex32 - Storage for a normal complex number.
 * @real: The real part in Q1.31 fractional format.
 * @imag: The imaginary part in Q1.31 fractional format.
 */
struct icomplex32 {
	int32_t real;
	int32_t imag;
};

/**
 * struct ipolar32 - Storage for complex number in polar format.
 * @magnitude: The length of vector in Q2.30 format.
 * @angle: The phase angle of the vector -pi to +pi in Q3.29 format.
 */
struct ipolar32 {
	int32_t magnitude;
	int32_t angle;
};

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

static inline void icomplex32_adds(const struct icomplex32 *in1, const struct icomplex32 *in2,
				   struct icomplex32 *out)
{
	out->real = sat_int32((int64_t)in1->real + in2->real);
	out->imag = sat_int32((int64_t)in1->imag + in2->imag);
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

/* Lookup table for square root, created with Octave commands:
 * arg=((1:32) * 2^26) / 2^30; lut = int32(sqrt(arg) * 2^30);
 * fmt=['static int32_t sqrt_int32_lut[] = {' repmat(' %d,',1, numel(lut)-1) ' %d};\n'];
 * fprintf(fmt, lut)
 */
static int32_t sqrt_int32_lut[] = {
	268435456, 379625062, 464943848, 536870912, 600239927, 657529896, 710213460,
	759250125, 805306368, 848867446, 890299688, 929887697, 967857801, 1004393507,
	1039646051, 1073741824, 1106787739, 1138875187, 1170083026, 1200479854,
	1230125796, 1259073893, 1287371222, 1315059792, 1342177280, 1368757628,
	1394831545, 1420426919, 1445569171, 1470281545, 1494585366, 1518500250
};

/**
 * sqrt_int32() - Calculate 32-bit fractional square root function.
 * @param n: Input value in Q2.30 format, from 0 to 2.0.
 * @return : Calculated square root of n in Q2.30 format.
 */
static int32_t sqrt_int32(int32_t n)
{
	uint64_t n_shifted;
	uint32_t x;
	int mul_shift;
	int div_shift;

	if (n < 1)
		return 0;

	/* Scale input argument with 2^n, where n is even.
	 * Scale calculated sqrt() with 2^(-n/2).
	 */
	div_shift = (__builtin_clz(n) - 1) >> 1;
	mul_shift = div_shift << 1;
	n = n << mul_shift;

	/* For Q2.30 divide */
	n_shifted = (uint64_t)n << 30;

	/* idx = 0 .. 31 */
	x = sqrt_int32_lut[n >> 26];

	/* Iterate x(n+1) = 1/2 * (x(n) + N / x(n))
	 * N is argument for square root
	 * x(n) is initial guess
	 */
	x = (uint32_t)(((n_shifted / x + x) + 1) >> 1);
	x = (uint32_t)(((n_shifted / x + x) + 1) >> 1);
	x = (uint32_t)(((n_shifted / x + x) + 1) >> 1);

	x = x >> div_shift;
	return (int32_t)x;
}

/**
 * icomplex32_to_polar() - Convert (re, im) complex number to polar.
 * @param complex: Pointer to input complex number in Q1.31 format.
 * @param polar: Pointer to output complex number in Q2.30 format for
 *		 magnitude and Q3.29 for phase angle.
 *
 * The function can be used to convert data in-place with same address for
 * input and output. It can be useful to save scratch memory.
 */
static inline void icomplex32_to_polar(struct icomplex32 *complex, struct ipolar32 *polar)
{
	struct icomplex32 c = *complex;
	int64_t squares_sum;
	int32_t sqrt_arg;
	int32_t acos_arg;
	int32_t acos_val;

	/* Calculate square of magnitudes Q1.31, result is Q2.62 */
	squares_sum = (int64_t)c.real * c.real +  (int64_t)c.imag * c.imag;

	/* Square root */
	sqrt_arg = Q_SHIFT_RND(squares_sum, 62, 30);
	polar->magnitude = sqrt_int32(sqrt_arg); /* Q2.30 */

	if (polar->magnitude == 0) {
		polar->angle = 0;
		return;
	}

	/* Calculate phase angle with acos( complex->reaL / polar->magnitude) */
	acos_arg = sat_int32((((int64_t)c.real) << 29) / polar->magnitude); /* Q2.30 */
	acos_val = acos_fixed_32b(acos_arg); /* Q3.29 */
	polar->angle = (c.imag < 0) ? -acos_val : acos_val;
}

/**
 * ipolar32_to_complex() - Convert complex number from polar to normal (re, im) format.
 * @param polar: Pointer to input complex number in polar format.
 * @param complex: Pointer to output complex number in normal format in Q1.31.
 *
 * This function can be used to convert data in-place with same address for input
 * and ouutput. It can be useful to save scratch memory.
 */
static inline void ipolar32_to_complex(struct ipolar32 *polar, struct icomplex32 *complex)
{
	struct cordic_cmpx cexp;
	int32_t phase;
	int32_t magnitude;

	/* The conversion can happen in-place, so load copies of the values first */
	magnitude = polar->magnitude;
	phase = Q_SHIFT_RND(polar->angle, 29, 28); /* Q3.29 to Q2.28 */
	cmpx_exp_32b(phase, &cexp); /* Q2.30 */
	complex->real = sat_int32(Q_MULTSR_32X32((int64_t)magnitude, cexp.re, 30, 30, 31));
	complex->imag = sat_int32(Q_MULTSR_32X32((int64_t)magnitude, cexp.im, 30, 30, 31));
}

#endif /* __SOF_ICOMPLEX32_H__ */
