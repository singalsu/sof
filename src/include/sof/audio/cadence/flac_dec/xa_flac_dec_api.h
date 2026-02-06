/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2010-2024 Cadence Design Systems, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef __XA_FLAC_DEC_API_H__
#define __XA_FLAC_DEC_API_H__

/*****************************************************************************/
/* FLAC Decoder specific API definitions                                      */
/*****************************************************************************/

/* flac_dec-specific configuration parameters */
enum xa_config_param_flac_dec {
	//XA_FLAC_DEC_CONFIG_PARAM_TOTAL_SAMPLES = 0,   no longer used
	XA_FLAC_DEC_CONFIG_PARAM_CHANNELS = 1,
	//XA_FLAC_DEC_CONFIG_PARAM_CHANNEL_ASSIGNMENT = 2,   no longer used
	XA_FLAC_DEC_CONFIG_PARAM_BITS_PER_SAMPLE = 3,
	XA_FLAC_DEC_CONFIG_PARAM_SAMPLE_RATE = 4,
	XA_FLAC_DEC_CONFIG_PARAM_BLOCKSIZE = 5,
	XA_FLAC_DEC_CONFIG_PARAM_OGG_CONTAINER = 6,
	XA_FLAC_DEC_CONFIG_PARAM_EXTENDED_FSR = 7,
	XA_FLAC_DEC_CONFIG_PARAM_SKIP_FRAMES = 8,
	XA_FLAC_DEC_CONFIG_PARAM_SEEKTABLE_OFFSET = 9,
	XA_FLAC_DEC_CONFIG_PARAM_SEEKTABLE_LENGTH = 10,
	XA_FLAC_DEC_CONFIG_PARAM_GET_CUR_BITRATE = 11,
	XA_FLAC_DEC_CONFIG_PARAM_GET_STREAM_INFO = 12
#ifdef MD5_SUPPORT
	, XA_FLAC_DEC_CONFIG_PARAM_MD5_CHECKING = 13
#endif
	, XA_FLAC_DEC_CONFIG_PARAM_INPUT_FRAMESIZE = 14
	, XA_FLAC_DEC_CONFIG_PARAM_OUTPUT_BLOCKSIZE = 15
	, XA_FLAC_DEC_CONFIG_PARAM_OGG_MAXPAGE = 16
};

/* commands */
#include "xa_apicmd_standards.h"

/* flac_dec-specific commands */
/* (none) */

/* flac_dec-specific command types */
/* (none) */

/* error codes */
#include "xa_error_standards.h"

#define XA_CODEC_FLAC_DEC	1

/* flac_dec-specific error codes */
/*****************************************************************************/
/* Class 1: Configuration Errors                                             */
/*****************************************************************************/
/* Nonfatal Errors */
enum xa_error_nonfatal_config_flac_dec {
	XA_FLACDEC_CONFIG_NONFATAL_FLAC_ALREADY_INITIALIZED = XA_ERROR_CODE(xa_severity_nonfatal, xa_class_config, XA_CODEC_FLAC_DEC, 0),
	XA_FLACDEC_CONFIG_NONFATAL_UNSUPPORTED_CHNUM		  = XA_ERROR_CODE(xa_severity_nonfatal, xa_class_config, XA_CODEC_FLAC_DEC, 1),
	XA_FLACDEC_CONFIG_NONFATAL_SEEK_TABLE_IS_NOT_MET	  = XA_ERROR_CODE(xa_severity_nonfatal, xa_class_config, XA_CODEC_FLAC_DEC, 2),
	XA_FLACDEC_CONFIG_NONFATAL_STREAM_INFO_IS_NOT_MET	  = XA_ERROR_CODE(xa_severity_nonfatal, xa_class_config, XA_CODEC_FLAC_DEC, 3),
	XA_FLACDEC_CONFIG_NONFATAL_INVALID_PARAM		  = XA_ERROR_CODE(xa_severity_nonfatal, xa_class_config, XA_CODEC_FLAC_DEC, 4)
};

/* Fatal Errors */
enum xa_error_fatal_config_flac_dec {
	XA_FLACDEC_CONFIG_FATAL_UNSUPPORTED_CONTAINER    = XA_ERROR_CODE(xa_severity_fatal, xa_class_config, XA_CODEC_FLAC_DEC, 0)
};

/*****************************************************************************/
/* Class 2: Execution Errors                                                 */
/*****************************************************************************/
/* Nonfatal Errors */
enum xa_error_nonfatal_execute_flac_dec {
	XA_FLACDEC_EXECUTE_NONFATAL_INSUFFICIENT_INPUT  = XA_ERROR_CODE(xa_severity_nonfatal, xa_class_execute, XA_CODEC_FLAC_DEC, 0),
	XA_FLACDEC_EXECUTE_NONFATAL_LOST_SYNC			= XA_ERROR_CODE(xa_severity_nonfatal, xa_class_execute, XA_CODEC_FLAC_DEC, 1),
	XA_FLACDEC_EXECUTE_NONFATAL_BAD_HEADER			= XA_ERROR_CODE(xa_severity_nonfatal, xa_class_execute, XA_CODEC_FLAC_DEC, 2),
	XA_FLACDEC_EXECUTE_NONFATAL_FRAME_CRC_MISMATCH	= XA_ERROR_CODE(xa_severity_nonfatal, xa_class_execute, XA_CODEC_FLAC_DEC, 3),
	XA_FLACDEC_EXECUTE_NONFATAL_UNPARSEABLE_STREAM	= XA_ERROR_CODE(xa_severity_nonfatal, xa_class_execute, XA_CODEC_FLAC_DEC, 4),
	XA_FLACDEC_EXECUTE_NONFATAL_ANOTHER_CHNUM_SET	= XA_ERROR_CODE(xa_severity_nonfatal, xa_class_execute, XA_CODEC_FLAC_DEC, 5),
	XA_FLACDEC_EXECUTE_NONFATAL_INVALID_STRM_POS    = XA_ERROR_CODE(xa_severity_nonfatal, xa_class_execute, XA_CODEC_FLAC_DEC, 6),
	XA_FLACDEC_EXECUTE_NONFATAL_NEW_STREAM_DETECTED = XA_ERROR_CODE(xa_severity_nonfatal, xa_class_execute, XA_CODEC_FLAC_DEC, 7),
	XA_FLACDEC_EXECUTE_NONFATAL_NEW_STREAM_MAY_START = XA_ERROR_CODE(xa_severity_nonfatal, xa_class_execute, XA_CODEC_FLAC_DEC, 8),
	XA_FLACDEC_EXECUTE_NONFATAL_NEW_STREAM_PARAMS = XA_ERROR_CODE(xa_severity_nonfatal, xa_class_execute, XA_CODEC_FLAC_DEC, 9),
	XA_FLACDEC_EXECUTE_NONFATAL_PARTIALLY_DECODABLE_INPUT = XA_ERROR_CODE(xa_severity_nonfatal, xa_class_execute, XA_CODEC_FLAC_DEC, 10)
#ifdef FLAC_SUBSET
	, XA_FLACDEC_EXECUTE_NONFATAL_SUBSET_LIM = XA_ERROR_CODE(xa_severity_nonfatal, xa_class_execute, XA_CODEC_FLAC_DEC, 11)
#endif
};

/* Fatal Errors */
enum xa_error_fatal_execute_flac_dec {
	XA_FLACDEC_EXECUTE_FATAL_OGG_ERROR            = XA_ERROR_CODE(xa_severity_fatal, xa_class_execute, XA_CODEC_FLAC_DEC, 0),	/* apparently unused */
	XA_FLACDEC_EXECUTE_FATAL_SEEK_ERROR           = XA_ERROR_CODE(xa_severity_fatal, xa_class_execute, XA_CODEC_FLAC_DEC, 1),
	XA_FLACDEC_EXECUTE_FATAL_ABORTED              = XA_ERROR_CODE(xa_severity_fatal, xa_class_execute, XA_CODEC_FLAC_DEC, 2),
	XA_FLACDEC_EXECUTE_FATAL_UNINITIALIZED        = XA_ERROR_CODE(xa_severity_fatal, xa_class_execute, XA_CODEC_FLAC_DEC, 3)
#ifdef MD5_SUPPORT
	, XA_FLACDEC_EXECUTE_FATAL_MD5_MISMATCH = XA_ERROR_CODE(xa_severity_fatal, xa_class_execute, XA_CODEC_FLAC_DEC, 4)
#endif
	, XA_FLACDEC_EXECUTE_FATAL_INP_BUF_TOO_SMALL = XA_ERROR_CODE(xa_severity_fatal, xa_class_execute, XA_CODEC_FLAC_DEC, 5)
	, XA_FLACDEC_EXECUTE_FATAL_MAX_BLOCKSIZE_CONFIG_MISMATCH = XA_ERROR_CODE(xa_severity_fatal, xa_class_execute, XA_CODEC_FLAC_DEC, 6)
};

#include "xa_type_def.h"

typedef struct {
	unsigned min_blocksize, max_blocksize;
	unsigned min_framesize, max_framesize;
	unsigned sample_rate;
	unsigned channels;
	unsigned bits_per_sample;
	unsigned long long total_samples;
	unsigned char md5sum[16];
} xa_flac_dec_streaminfo_t;

#if defined(__cplusplus)
extern "C" {
#endif	/* __cplusplus */
xa_codec_func_t xa_flac_dec;
#if defined(__cplusplus)
}
#endif	/* __cplusplus */
#endif /* __XA_FLAC_DEC_API_H__ */
