#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2026 Intel Corporation. All rights reserved.
#
# Batch-emit SOF mel40 MFCC feature files for a labelled WAV dataset, for
# retraining the TFLM micro_speech model against real SOF features.
#
# Expected input layout:
#   <wav_root>/<label>/<file>.wav      # any sample rate/channels; auto-converted
#
# Output layout:
#   <feat_root>/<label>/<file>.raw     # SOF mel40 hop records, one after another
#
# Each hop record is 184 bytes (matches the on-device wire format):
#   24 bytes  struct mfcc_data_header  (magic, frame_number, reserved,
#                                       energy, noise_energy, vad_flag)
#   40 x int32_t   Q9.23 mel-log values
#
# The consumer (Python training loader) parses N=hop_count records, strips the
# 24-byte headers, and reshapes to (N, 40) int32 Q9.23. See decode_mel.m for a
# reference decoder.
#
# Per-example gain augmentation (S32 path only, on by default):
#   Each speech WAV is peak-normalized to --gain-peak-dbfs (default -10 dBFS)
#   and then shifted by a Gaussian offset with sigma --gain-sigma-db (default
#   5 dB), drawn independently per WAV. This gives the model a wider input
#   level distribution than the (already peak-normalized) piper output would
#   otherwise cover. The silence/ class is exempt so background noise stays
#   quiet. Disable with --no-gain-aug (or GAIN_AUG=0) for bit-exact reruns.
#
# Requirements:
#   - $SOF_WORKSPACE points at the parent of the sof tree
#   - sof-testbench4 built at tools/testbench/build_testbench/install/bin/
#   - sof-hda-benchmark-mfccmel40{16,24,32}.tplg built (development target)
#   - sox on PATH

set -e

usage() {
	cat >&2 <<EOF
Usage: $0 <wav_root> <feat_root> [--format S16|S24|S32]
                    [--no-gain-aug] [--gain-peak-dbfs DB] [--gain-sigma-db DB]

  wav_root            Directory containing <label>/*.wav (recursed one level).
  feat_root           Output root; <label>/ subdirs are created as needed.
  --format            Testbench sample container. Default S32 (on-device).
  --no-gain-aug       Disable per-WAV gain augmentation (S32 path only).
  --gain-peak-dbfs    Peak-normalization target in dBFS (default -10).
  --gain-sigma-db     Gaussian jitter sigma in dB around the target (default 5).
EOF
	exit 1
}

[ $# -ge 2 ] || usage

WAV_ROOT="$1"
FEAT_ROOT="$2"
FORMAT="S32"
: "${GAIN_AUG:=1}"
: "${GAIN_PEAK_DBFS:=-10}"
: "${GAIN_SIGMA_DB:=5}"

shift 2
while [ $# -gt 0 ]; do
	case "$1" in
	--format)
		FORMAT="$2"
		shift 2
		;;
	--no-gain-aug)
		GAIN_AUG=0
		shift
		;;
	--gain-peak-dbfs)
		GAIN_PEAK_DBFS="$2"
		shift 2
		;;
	--gain-sigma-db)
		GAIN_SIGMA_DB="$2"
		shift 2
		;;
	*)
		usage
		;;
	esac
done

case "$FORMAT" in
S16) SF="16" ;;
S24) SF="24" ;;
S32) SF="32" ;;
*) usage ;;
esac

: "${SOF_WORKSPACE:?SOF_WORKSPACE must point at the parent of the sof tree}"

TESTBENCH="${SOF_WORKSPACE}/sof/tools/testbench/build_testbench/install/bin/sof-testbench4"
TPLG="${SOF_WORKSPACE}/sof/tools/build_tools/topology/topology2/development/sof-hda-benchmark-mfccmel40${SF}.tplg"

[ -x "$TESTBENCH" ] || { echo "Missing testbench: $TESTBENCH" >&2; exit 2; }
[ -f "$TPLG" ]     || { echo "Missing topology: $TPLG"       >&2; exit 2; }
[ -d "$WAV_ROOT" ] || { echo "Missing wav_root: $WAV_ROOT"   >&2; exit 2; }
command -v sox >/dev/null || { echo "sox not on PATH" >&2; exit 2; }

TMPDIR_LOCAL=$(mktemp -d -t sof_mfcc_train.XXXXXX)
trap 'rm -rf "$TMPDIR_LOCAL"' EXIT

# Draw one N(0, sigma) sample in dB via awk Box-Muller, clipped so the
# combined peak (GAIN_PEAK_DBFS + offset) never exceeds 0 dBFS. The seed
# mixes $RANDOM with nanosecond time so consecutive calls in the same
# second do not collapse to the same value; keeping it under ~14 digits
# avoids awk losing entropy when parsing the seed as a double.
gauss_offset_db() {
	local sigma="$1"
	local peak="$2"
	awk -v s="$sigma" -v cap="$(awk -v p="$2" 'BEGIN{print -p}')" \
	    -v seed="$RANDOM$(date +%N)" 'BEGIN {
		srand(seed)
		u1 = rand(); if (u1 < 1e-12) u1 = 1e-12
		u2 = rand()
		v = s * sqrt(-2 * log(u1)) * cos(6.283185307 * u2)
		if (v > cap) v = cap
		printf "%.3f", v
	}'
}

# sox parameters: 16 kHz stereo (bench topology expects -c 2 -p 3,4).
# MFCC config picks channel 0 (see get_mel_spectrogram_config in setup_mfcc.m),
# so a mono WAV upmixed to stereo is fine.
#
# LABEL_GAIN is set per label directory by the outer loop: 1 => append gain
# effects for this WAV, 0 => write the plain conversion.
convert_wav() {
	local src="$1"
	local dst="$2"

	case "$FORMAT" in
	S16) sox -R --encoding signed-integer "$src" \
		-L -r 16000 -c 2 -b 16 "$dst" ;;
	S24) sox -R --no-dither --encoding signed-integer "$src" \
		-L -r 16000 -c 2 -b 32 "$dst" vol 0.003906250000 ;;
	S32)
		if [ "${LABEL_GAIN:-0}" = "1" ]; then
			local off
			off=$(gauss_offset_db "$GAIN_SIGMA_DB" "$GAIN_PEAK_DBFS")
			sox -R --no-dither --encoding signed-integer "$src" \
				-L -r 16000 -c 2 -b 32 "$dst" \
				gain -n "$GAIN_PEAK_DBFS" gain "$off"
		else
			sox -R --no-dither --encoding signed-integer "$src" \
				-L -r 16000 -c 2 -b 32 "$dst"
		fi
		;;
	esac
}

process_wav() {
	local wav="$1"
	local out_dir="$2"
	local base
	base=$(basename "$wav" .wav)

	local raw_in="${TMPDIR_LOCAL}/in.raw"
	local raw_out="${out_dir}/${base}.raw"

	mkdir -p "$out_dir"
	convert_wav "$wav" "$raw_in"

	# -d 0 silences testbench traces (there is no -q flag).
	"$TESTBENCH" -d 0 \
		-r 16000 -c 2 -b "${FORMAT}_LE" -p 3,4 \
		-t "$TPLG" -i "$raw_in" -o "$raw_out"
}

if [ "$GAIN_AUG" = "1" ] && [ "$FORMAT" = "S32" ]; then
	echo "Gain aug: peak=${GAIN_PEAK_DBFS} dBFS, sigma=${GAIN_SIGMA_DB} dB (silence exempt)"
else
	echo "Gain aug: disabled"
fi

total=0
for label_dir in "$WAV_ROOT"/*/; do
	[ -d "$label_dir" ] || continue
	label=$(basename "$label_dir")
	# Skip underscore-prefixed staging dirs (e.g. _raw from the Piper generator).
	case "$label" in _*) continue ;; esac
	out_dir="${FEAT_ROOT}/${label}"

	if [ "$GAIN_AUG" = "1" ] && [ "$FORMAT" = "S32" ] && [ "$label" != "silence" ]; then
		LABEL_GAIN=1
	else
		LABEL_GAIN=0
	fi

	echo "[label=${label} gain_aug=${LABEL_GAIN}]"
	count=0
	for wav in "$label_dir"*.wav; do
		[ -f "$wav" ] || continue
		process_wav "$wav" "$out_dir"
		count=$((count + 1))
	done
	echo "  wrote ${count} feature files to ${out_dir}"
	total=$((total + count))
done

echo "-----------------------------------------------------------------"
echo "Emitted ${total} mel40 feature files under ${FEAT_ROOT}"
echo "Format per hop: 24-byte mfcc_data_header + 40 x int32 Q9.23 = 184 bytes"
echo "-----------------------------------------------------------------"
