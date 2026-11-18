// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2026 Intel Corporation.
//
// Real Classic Float AEC backend for the webrtc_aec module.
//
// Wraps WebRtcAec_Create/Init/BufferFarend/Process from the WebRTC AEC
// (Partitioned Block Frequency Domain MDF) floating-point library.
//
// API summary:
//   WebRtcAec_Create()       — allocate Aec handle
//   WebRtcAec_Init()         — configure sample rate (8000, 16000, 32000, 48000 Hz)
//   WebRtcAec_BufferFarend() — queue 10 ms of float echo reference (far-end)
//   WebRtcAec_Process()      — cancel echo from near-end mic frame (float)
//   WebRtcAec_set_config()   — configure NLP suppression mode
//   WebRtcAec_Free()         — release handle
//
// Single-precision floating-point operations lowered to native hardware FPU
// instructions on Panther Lake (intel_ace30_ptl). One handle per channel.

#include <sof/audio/module_adapter/module/generic.h>
#include <errno.h>
#include <math.h>
#include "webrtc_aec.h"

#include <webrtc/modules/audio_processing/aec/include/echo_cancellation.h>

LOG_MODULE_DECLARE(webrtc_aec, CONFIG_SOF_LOG_LEVEL);

#ifdef CONFIG_WEBRTC_AEC_ERLE_TRACE
/* 100 frames of 10 ms */
#define WEBRTC_AEC_ERLE_FRAMES	100
/* Far-end active when the reference frame is above -50 dBFS */
#define WEBRTC_AEC_ERLE_REF_MIN	(32768.0f * 32768.0f * 1e-5f)
#define WEBRTC_AEC_FS_ENERGY	(32768.0f * 32768.0f)
#endif

#ifdef CONFIG_WEBRTC_AEC_DELAY_DEBUG
/* 10 ms frame energy envelopes, lag search -200 .. +600 ms every 5 s */
#define WEBRTC_AEC_XC_HIST	256
#define WEBRTC_AEC_XC_WIN	170
#define WEBRTC_AEC_XC_LAG_MIN	-20
#define WEBRTC_AEC_XC_LAG_MAX	60
#define WEBRTC_AEC_XC_FRAMES	500
#endif

struct webrtc_aec_float_data {
	void   *aec[WEBRTC_AEC_CHANNELS_MAX];
	int     num_channels;
	int     suppression;
	int     stats_frames;
	int16_t ref_peak;
	int16_t mic_peak;
#ifdef CONFIG_WEBRTC_AEC_ERLE_TRACE
	int     erle_frames;
	int     erle_active;
	float   erle_mic_e;
	float   erle_out_e;
	float   erle_ref_e;
#endif
#ifdef CONFIG_WEBRTC_AEC_DELAY_DEBUG
	int     xc_pos;
	int     xc_count;
	int     xc_frames;
	float   xc_ref[WEBRTC_AEC_XC_HIST];
	float   xc_mic[WEBRTC_AEC_XC_HIST];
#endif
#if CONFIG_WEBRTC_AEC_MIC_DELAY_MS > 0
	int     mic_dly_len;
	int     mic_dly_pos;
	float   mic_dly[CONFIG_WEBRTC_AEC_MIC_DELAY_MS * 16];
#endif
	float   ref_f[WEBRTC_AEC_FRAME_SAMPLES_16K];
	float   mic_f[WEBRTC_AEC_FRAME_SAMPLES_16K];
	float   out_f[WEBRTC_AEC_FRAME_SAMPLES_16K];
};

static int webrtc_aec_float_init(struct processing_module *mod)
{
	struct webrtc_aec_comp_data *cd = module_get_private_data(mod);
	struct webrtc_aec_float_data *rd;

	rd = mod_zalloc(mod, sizeof(*rd));
	if (!rd)
		return -ENOMEM;

	cd->backend_data = rd;
	comp_info(mod->dev, "webrtc_aec: Classic Float AEC backend initialised");
	return 0;
}

static int webrtc_aec_float_configure(struct processing_module *mod, int sample_rate_hz,
				      int filter_len_ms, int suppression, int num_channels)
{
	struct webrtc_aec_comp_data *cd = module_get_private_data(mod);
	struct webrtc_aec_float_data *rd = cd->backend_data;
	AecConfig config;
	int c, ret;

	/* Free previously allocated handles. */
	for (c = 0; c < rd->num_channels; c++) {
		if (rd->aec[c]) {
			WebRtcAec_Free(rd->aec[c]);
			rd->aec[c] = NULL;
		}
	}
	rd->num_channels = 0;
	rd->suppression  = suppression;

	int aec_channels = (num_channels > 1) ? 1 : num_channels;

	for (c = 0; c < aec_channels; c++) {
		rd->aec[c] = WebRtcAec_Create();
		if (!rd->aec[c]) {
			comp_err(mod->dev, "webrtc_aec: WebRtcAec_Create() failed ch%d", c);
			goto err;
		}

		ret = WebRtcAec_Init(rd->aec[c], sample_rate_hz, sample_rate_hz);
		if (ret) {
			comp_err(mod->dev,
				 "webrtc_aec: WebRtcAec_Init(ch%d, %d) failed %d",
				 c, sample_rate_hz, ret);
			goto err;
		}

		config.nlpMode = (cd->high_suppression || suppression > 1) ? kAecNlpAggressive :
				 (suppression == 0) ? kAecNlpConservative : kAecNlpModerate;
		config.skewMode = kAecFalse;
		config.metricsMode = kAecFalse;
		config.delay_logging = IS_ENABLED(CONFIG_WEBRTC_AEC_DELAY_DEBUG) ?
				       kAecTrue : kAecFalse;

		ret = WebRtcAec_set_config(rd->aec[c], config);
		if (ret) {
			comp_err(mod->dev, "webrtc_aec: set_config ch%d failed %d", c, ret);
			goto err;
		}
	}

	rd->num_channels = aec_channels;
	rd->stats_frames = 0;
	rd->ref_peak = 0;
	rd->mic_peak = 0;
#if CONFIG_WEBRTC_AEC_MIC_DELAY_MS > 0
	rd->mic_dly_len = CONFIG_WEBRTC_AEC_MIC_DELAY_MS * sample_rate_hz / 1000;
	rd->mic_dly_pos = 0;
	memset(rd->mic_dly, 0, sizeof(rd->mic_dly));
#endif
	comp_info(mod->dev, "webrtc_aec: Float AEC rate=%d nlpMode=%d ch=%d (stream_ch=%d) delay=%d ms ref_gain=%d mic_delay=%d ms",
		  sample_rate_hz, config.nlpMode, aec_channels, num_channels,
		  CONFIG_WEBRTC_AEC_DELAY_MS, CONFIG_WEBRTC_AEC_REF_GAIN,
		  CONFIG_WEBRTC_AEC_MIC_DELAY_MS);
	return 0;

err:
	for (c = 0; c < num_channels; c++) {
		if (rd->aec[c]) {
			WebRtcAec_Free(rd->aec[c]);
			rd->aec[c] = NULL;
		}
	}
	return -ENOMEM;
}

static int webrtc_aec_float_set_suppression(struct processing_module *mod, bool high_suppression)
{
	struct webrtc_aec_comp_data *cd = module_get_private_data(mod);
	struct webrtc_aec_float_data *rd = cd->backend_data;
	AecConfig config;
	int c, ret;

	if (!rd)
		return 0;

	config.nlpMode = high_suppression ? kAecNlpAggressive : kAecNlpModerate;
	config.skewMode = kAecFalse;
	config.metricsMode = kAecFalse;
	config.delay_logging = IS_ENABLED(CONFIG_WEBRTC_AEC_DELAY_DEBUG) ?
			       kAecTrue : kAecFalse;

	for (c = 0; c < rd->num_channels; c++) {
		if (!rd->aec[c])
			continue;
		ret = WebRtcAec_set_config(rd->aec[c], config);
		if (ret) {
			comp_err(mod->dev, "webrtc_aec: set_config failed %d ch%d", ret, c);
			return ret;
		}
	}

	comp_info(mod->dev, "webrtc_aec: updated float suppression high=%d nlpMode=%d",
		  high_suppression, config.nlpMode);
	return 0;
}

#ifdef CONFIG_WEBRTC_AEC_ERLE_TRACE
static int webrtc_aec_db_round(float db)
{
	return (int)(db + (db >= 0.0f ? 0.5f : -0.5f));
}

/* ERLE over far-end active frames, near-end speech in them lowers the value */
static void webrtc_aec_float_erle(struct processing_module *mod,
				  struct webrtc_aec_float_data *rd,
				  const int16_t *ref, int frame_samples)
{
	float ref_e = 0.0f, mic_e = 0.0f, out_e = 0.0f;
	float norm;
	int i;

	for (i = 0; i < frame_samples; i++) {
		float r = (float)ref[i];

		ref_e += r * r;
		mic_e += rd->mic_f[i] * rd->mic_f[i];
		out_e += rd->out_f[i] * rd->out_f[i];
	}

	if (ref_e > WEBRTC_AEC_ERLE_REF_MIN * frame_samples) {
		rd->erle_ref_e += ref_e;
		rd->erle_mic_e += mic_e;
		rd->erle_out_e += out_e;
		rd->erle_active++;
	}

	if (++rd->erle_frames < WEBRTC_AEC_ERLE_FRAMES)
		return;

	if (rd->erle_active) {
		float mic = rd->erle_mic_e > 1.0f ? rd->erle_mic_e : 1.0f;
		float out = rd->erle_out_e > 1.0f ? rd->erle_out_e : 1.0f;

		norm = WEBRTC_AEC_FS_ENERGY * rd->erle_active * frame_samples;
		comp_info(mod->dev,
			  "webrtc_aec: ERLE %d dB mic %d ref %d dBFS active %d%%",
			  webrtc_aec_db_round(10.0f * log10f(mic / out)),
			  webrtc_aec_db_round(10.0f * log10f(mic / norm)),
			  webrtc_aec_db_round(10.0f * log10f(rd->erle_ref_e / norm)),
			  rd->erle_active * 100 / WEBRTC_AEC_ERLE_FRAMES);
	}

	rd->erle_frames = 0;
	rd->erle_active = 0;
	rd->erle_mic_e = 0.0f;
	rd->erle_out_e = 0.0f;
	rd->erle_ref_e = 0.0f;
}
#endif

#ifdef CONFIG_WEBRTC_AEC_DELAY_DEBUG
static float webrtc_aec_frame_db(const int16_t *x, int frame_samples)
{
	float e = 1.0f;
	int i;

	for (i = 0; i < frame_samples; i++)
		e += (float)x[i] * (float)x[i];

	return 10.0f * log10f(e);
}

/* Coarse echo delay from energy envelopes, independent of the AEC estimator */
static void webrtc_aec_float_xcorr(struct processing_module *mod,
				   struct webrtc_aec_float_data *rd,
				   const int16_t *ref, const int16_t *mic,
				   int frame_samples)
{
	const int h = WEBRTC_AEC_XC_HIST;
	float mean_m = 0.0f, mean_r, smm = 0.0f, smr, srr;
	float best_corr = -2.0f;
	int best_lag = 0;
	int newest, start, lag, n, im, ir;

	rd->xc_ref[rd->xc_pos] = webrtc_aec_frame_db(ref, frame_samples);
	rd->xc_mic[rd->xc_pos] = webrtc_aec_frame_db(mic, frame_samples);
	newest = rd->xc_pos;
	rd->xc_pos = (rd->xc_pos + 1) % h;
	if (rd->xc_count < h)
		rd->xc_count++;

	if (++rd->xc_frames < WEBRTC_AEC_XC_FRAMES || rd->xc_count < h)
		return;

	rd->xc_frames = 0;

	/* Mic window ends -LAG_MIN frames back so negative lags fit in history */
	start = newest + WEBRTC_AEC_XC_LAG_MIN - WEBRTC_AEC_XC_WIN + 1 + h;
	for (n = 0; n < WEBRTC_AEC_XC_WIN; n++)
		mean_m += rd->xc_mic[(start + n) % h];
	mean_m /= WEBRTC_AEC_XC_WIN;
	for (n = 0; n < WEBRTC_AEC_XC_WIN; n++) {
		float d = rd->xc_mic[(start + n) % h] - mean_m;

		smm += d * d;
	}

	for (lag = WEBRTC_AEC_XC_LAG_MIN; lag <= WEBRTC_AEC_XC_LAG_MAX; lag++) {
		mean_r = 0.0f;
		for (n = 0; n < WEBRTC_AEC_XC_WIN; n++)
			mean_r += rd->xc_ref[(start + n - lag + h) % h];
		mean_r /= WEBRTC_AEC_XC_WIN;

		smr = 0.0f;
		srr = 0.0f;
		for (n = 0; n < WEBRTC_AEC_XC_WIN; n++) {
			im = (start + n) % h;
			ir = (start + n - lag + h) % h;
			smr += (rd->xc_mic[im] - mean_m) * (rd->xc_ref[ir] - mean_r);
			srr += (rd->xc_ref[ir] - mean_r) * (rd->xc_ref[ir] - mean_r);
		}

		if (smm > 0.0f && srr > 0.0f) {
			float c = smr / sqrtf(smm * srr);

			if (c > best_corr) {
				best_corr = c;
				best_lag = lag;
			}
		}
	}

	comp_info(mod->dev, "webrtc_aec: xcorr lag %d ms corr %d%%",
		  best_lag * 10, (int)(best_corr * 100.0f));
}
#endif

static int webrtc_aec_float_process_ch(struct processing_module *mod,
				       const int16_t *mic, const int16_t *ref, int16_t *out,
				       int frame_samples, int ch)
{
	struct webrtc_aec_comp_data *cd = module_get_private_data(mod);
	struct webrtc_aec_float_data *rd = cd->backend_data;
	const float *const nearend[1] = { rd->mic_f };
	float *const out_ptrs[1] = { rd->out_f };
	int i, ret;

	if (frame_samples > WEBRTC_AEC_FRAME_SAMPLES_16K)
		return -EINVAL;

	if (ch >= rd->num_channels || !rd->aec[ch]) {
		memcpy(out, mic, (size_t)frame_samples * sizeof(int16_t));
		return 0;
	}

	/* Convert ref to float and buffer far-end */
	for (i = 0; i < frame_samples; i++) {
		rd->ref_f[i] = (float)ref[i] * (float)CONFIG_WEBRTC_AEC_REF_GAIN;
#ifdef CONFIG_WEBRTC_AEC_DELAY_DEBUG
		if (ref[i] > rd->ref_peak)
			rd->ref_peak = ref[i];
		if (mic[i] > rd->mic_peak)
			rd->mic_peak = mic[i];
#endif
	}

	ret = WebRtcAec_BufferFarend(rd->aec[ch], rd->ref_f, (size_t)frame_samples);
	if (ret) {
		comp_err(mod->dev, "webrtc_aec: BufferFarend ch%d failed %d", ch, ret);
		return ret;
	}

	/* Convert mic to float */
	for (i = 0; i < frame_samples; i++)
		rd->mic_f[i] = (float)mic[i];

#if CONFIG_WEBRTC_AEC_MIC_DELAY_MS > 0
	for (i = 0; i < frame_samples; i++) {
		float x = rd->mic_f[i];

		rd->mic_f[i] = rd->mic_dly[rd->mic_dly_pos];
		rd->mic_dly[rd->mic_dly_pos] = x;
		if (++rd->mic_dly_pos >= rd->mic_dly_len)
			rd->mic_dly_pos = 0;
	}
#endif

	ret = WebRtcAec_Process(rd->aec[ch], nearend, 1, out_ptrs,
				(size_t)frame_samples, CONFIG_WEBRTC_AEC_DELAY_MS, 0);
	if (ret) {
		comp_err(mod->dev, "webrtc_aec: Process ch%d failed %d", ch, ret);
		return ret;
	}

#ifdef CONFIG_WEBRTC_AEC_DELAY_DEBUG
	/* Median < 0 means echo arrives before the aligned reference (non-causal) */
	if (ch == 0 && ++rd->stats_frames >= 500) {
		int median, std;
		float poor;

		if (!WebRtcAec_GetDelayMetrics(rd->aec[0], &median, &std, &poor))
			comp_info(mod->dev,
				  "webrtc_aec: delay median %d std %d ms poor %d%% ref_pk %d mic_pk %d",
				  median, std, (int)(poor * 100.0f), rd->ref_peak, rd->mic_peak);
		rd->stats_frames = 0;
		rd->ref_peak = 0;
		rd->mic_peak = 0;
	}
#endif

#ifdef CONFIG_WEBRTC_AEC_ERLE_TRACE
	if (ch == 0)
		webrtc_aec_float_erle(mod, rd, ref, frame_samples);
#endif
#ifdef CONFIG_WEBRTC_AEC_DELAY_DEBUG
	if (ch == 0)
		webrtc_aec_float_xcorr(mod, rd, ref, mic, frame_samples);
#endif

	/* Convert float output back to int16_t with saturation and rounding */
	for (i = 0; i < frame_samples; i++) {
		float val = rd->out_f[i];
		if (val > 32767.0f)
			out[i] = 32767;
		else if (val < -32768.0f)
			out[i] = -32768;
		else
			out[i] = (int16_t)(val + (val >= 0.0f ? 0.5f : -0.5f));
	}

	return 0;
}

static int webrtc_aec_float_reset(struct processing_module *mod)
{
	struct webrtc_aec_comp_data *cd = module_get_private_data(mod);
	struct webrtc_aec_float_data *rd = cd->backend_data;
	int c, ret;

	for (c = 0; c < rd->num_channels; c++) {
		if (!rd->aec[c])
			continue;
		ret = WebRtcAec_Init(rd->aec[c], cd->proc_rate, cd->proc_rate);
		if (ret)
			comp_warn(mod->dev, "webrtc_aec: reset ch%d failed %d", c, ret);
	}
	return 0;
}

static int webrtc_aec_float_free(struct processing_module *mod)
{
	struct webrtc_aec_comp_data *cd = module_get_private_data(mod);
	struct webrtc_aec_float_data *rd = cd->backend_data;
	int c;

	if (!rd)
		return 0;

	for (c = 0; c < rd->num_channels; c++) {
		if (rd->aec[c]) {
			WebRtcAec_Free(rd->aec[c]);
			rd->aec[c] = NULL;
		}
	}
	mod_free(mod, rd);
	cd->backend_data = NULL;
	return 0;
}

const struct webrtc_aec_backend webrtc_aec_backend = {
	.name            = "aec_float",
	.init            = webrtc_aec_float_init,
	.configure       = webrtc_aec_float_configure,
	.set_suppression = webrtc_aec_float_set_suppression,
	.process_ch      = webrtc_aec_float_process_ch,
	.reset           = webrtc_aec_float_reset,
	.free            = webrtc_aec_float_free,
};
