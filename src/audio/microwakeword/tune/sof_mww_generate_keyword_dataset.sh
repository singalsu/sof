#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2026 Intel Corporation. All rights reserved.
#
# Generate a synthetic keyword WAV dataset for microWakeWord (MWW) with
# piper-sample-generator (multi-speaker LibriTTS-R), augment it (impulse-response
# convolution, volume jitter, resample to 16 kHz), and drop the result into
# the layout expected by sof_mfcc_extract_features.sh:
#
#   <out_root>/<label>/*.wav
#
# Companion `silence` and `unknown` classes are NOT produced here — those are
# dataset-wide and best sourced from Speech Commands v2 (see
# sof_mww_prepare_silence_unknown.sh).
#
# Requirements:
#   - piper-sample-generator installed in an activated venv (or $PIPER_VENV set).
#   - LibriTTS-R generator checkpoint at $PIPER_MODEL. Fetch once with:
#       wget -O <models>/en_US-libritts_r-medium.pt \
#         https://github.com/rhasspy/piper-sample-generator/releases/download/v2.0.0/en_US-libritts_r-medium.pt
#   - sox on PATH.
#
# Env knobs (all optional):
#   PIPER_VENV      Path to a Piper venv to auto-activate.
#   PIPER_MODEL     Path to the LibriTTS-R .pt checkpoint.
#   PIPER_REPO      Path to a piper-sample-generator git clone root.
#   MAX_SAMPLES     Positive clips per speaking-rate loop (default 400).
#   MAX_SPEAKERS    Cap on speaker-embedding index (default 700).
#   SLERP_WEIGHTS   Speaker blending weights passed to generator (default "0.0").
#   GAIN_AUG        1 = rewrite each augmented WAV in place with Gaussian level jitter.
#                   0 = leave baseline (default 0).
#   GAIN_PEAK_DBFS  Peak-normalization target in dBFS (default -10).
#   GAIN_SIGMA_DB   Gaussian jitter sigma in dB (default 5).
#   GAIN_HEADROOM_DB Headroom below full scale (default 1).

set -e

KEYWORDS=()
LABELS=()
OUT_ROOT=""

usage() {
	cat >&2 <<EOF
Usage: $0 --keyword "<text>" [--keyword "<text>" ...] \\
          [--label <dir> ...] [--model <path.pt>] <out_root>

  --keyword TEXT   Spoken phrase to synthesize (repeatable; at least one).
  --label DIR      Output subdir name under <out_root>. Repeat once per
                   --keyword to override the default naming. If omitted,
                   each label is derived from its keyword by lower-casing
                   and replacing spaces with underscores.
  --model PATH     Path to PyTorch .pt checkpoint (default: \$PIPER_MODEL).
  --venv  PATH     Path to piper virtual environment (default: \$PIPER_VENV).
  out_root         Dataset root; <label>/*.wav is produced under it for each
                   keyword, ready to feed to
                   sof_mfcc_extract_features.sh <out_root> <feat_root>.

Optional env: PIPER_VENV, PIPER_MODEL, PIPER_REPO, MAX_SAMPLES, MAX_SPEAKERS.
EOF
	exit 1
}

while [[ $# -gt 0 ]]; do
	case "$1" in
		-k|--keyword)
			[[ $# -ge 2 ]] || usage
			KEYWORDS+=("$2"); shift 2 ;;
		-l|--label)
			[[ $# -ge 2 ]] || usage
			LABELS+=("$2"); shift 2 ;;
		-m|--model)
			[[ $# -ge 2 ]] || usage
			PIPER_MODEL="$2"; shift 2 ;;
		--venv)
			[[ $# -ge 2 ]] || usage
			PIPER_VENV="$2"; shift 2 ;;
		-h|--help)
			usage ;;
		--)
			shift; break ;;
		-*)
			echo "unknown option: $1" >&2; usage ;;
		*)
			if [[ -z "$OUT_ROOT" ]]; then
				OUT_ROOT="$1"; shift
			else
				echo "unexpected positional: $1" >&2; usage
			fi ;;
	esac
done

if [[ -z "$OUT_ROOT" || ${#KEYWORDS[@]} -eq 0 ]]; then
	usage
fi

if [[ ${#LABELS[@]} -gt 0 && ${#LABELS[@]} -ne ${#KEYWORDS[@]} ]]; then
	echo "--label given ${#LABELS[@]} time(s) but --keyword given ${#KEYWORDS[@]} time(s); counts must match" >&2
	exit 1
fi

if [[ ${#LABELS[@]} -eq 0 ]]; then
	for kw in "${KEYWORDS[@]}"; do
		LABELS+=("$(echo "$kw" | tr ' A-Z' '_a-z')")
	done
fi

: "${MAX_SAMPLES:=400}"
: "${MAX_SPEAKERS:=700}"
: "${SLERP_WEIGHTS:=0.0}"
: "${GAIN_AUG:=0}"
: "${GAIN_PEAK_DBFS:=-10}"
: "${GAIN_SIGMA_DB:=5}"
: "${GAIN_HEADROOM_DB:=1}"

gauss_offset_db() {
	local sigma="$1"
	local peak="$2"
	local headroom="$3"
	awk -v s="$sigma" \
	    -v cap="$(awk -v p="$peak" -v h="$headroom" 'BEGIN{print -p-h}')" \
	    -v seed="$RANDOM$(date +%N)" 'BEGIN {
		srand(seed)
		u1 = rand(); if (u1 < 1e-12) u1 = 1e-12
		u2 = rand()
		v = s * sqrt(-2 * log(u1)) * cos(6.283185307 * u2)
		if (v > cap) v = cap
		printf "%.3f", v
	}'
}

apply_gain_jitter() {
	local dir="$1"
	local sum=0 n=0 mn=999 mx=-999 off tmp
	tmp=$(mktemp --suffix=.wav)
	for wav in "$dir"/*.wav; do
		[ -f "$wav" ] || continue
		off=$(gauss_offset_db "$GAIN_SIGMA_DB" "$GAIN_PEAK_DBFS" "$GAIN_HEADROOM_DB")
		sox "$wav" "$tmp" gain -n "$GAIN_PEAK_DBFS" gain "$off"
		mv "$tmp" "$wav"
		sum=$(awk -v a="$sum" -v b="$off" 'BEGIN{printf "%.3f", a+b}')
		mn=$(awk -v a="$mn" -v b="$off" 'BEGIN{print (b<a)?b:a}')
		mx=$(awk -v a="$mx" -v b="$off" 'BEGIN{print (b>a)?b:a}')
		n=$((n + 1))
	done
	rm -f "$tmp"
	if [ "$n" -gt 0 ]; then
		awk -v n="$n" -v s="$sum" -v mn="$mn" -v mx="$mx" \
		    -v p="$GAIN_PEAK_DBFS" -v h="$GAIN_HEADROOM_DB" 'BEGIN{
			printf "    gain jitter n=%d  offset mean=%.2f dB  min=%.2f  max=%.2f  target peak=%s dBFS  ceiling=%s dBFS\n", n, s/n, mn, mx, p, -h
		}'
	fi
}

if [[ -n "$PIPER_VENV" ]]; then
	if [[ -f "$PIPER_VENV/bin/activate" ]]; then
		# shellcheck disable=SC1091
		source "$PIPER_VENV/bin/activate"
	else
		echo ">>> PIPER_VENV=$PIPER_VENV has no bin/activate; assuming environment is active" >&2
	fi
fi

if ! command -v python3 >/dev/null; then
	echo "python3 not on PATH" >&2
	exit 1
fi

if ! command -v sox >/dev/null; then
	echo "sox not on PATH" >&2
	exit 1
fi

if [[ -z "$PIPER_MODEL" ]]; then
	for cand in \
		"$HOME/git/piper-sample-generator/models/en_US-libritts_r-medium.pt" \
		"$HOME/.local/share/piper-sample-generator/en_US-libritts_r-medium.pt"
	do
		if [[ -f "$cand" ]]; then
			PIPER_MODEL="$cand"
			break
		fi
	done
fi

if [[ -z "$PIPER_MODEL" || ! -f "$PIPER_MODEL" ]]; then
	cat >&2 <<EOF
Piper generator checkpoint not found: ${PIPER_MODEL:-<unset>}
Fetch it once with:
  mkdir -p \$HOME/git/piper-sample-generator/models
  wget -O \$HOME/git/piper-sample-generator/models/en_US-libritts_r-medium.pt \\
    https://github.com/rhasspy/piper-sample-generator/releases/download/v2.0.0/en_US-libritts_r-medium.pt
or point \$PIPER_MODEL at the downloaded .pt file.
EOF
	exit 1
fi

if [[ -z "$PIPER_REPO" ]]; then
	model_dir="$(cd "$(dirname "$PIPER_MODEL")" && pwd)"
	parent="$(cd "$model_dir/.." 2>/dev/null && pwd || true)"
	if [[ -d "$parent/piper_sample_generator" ]]; then
		PIPER_REPO="$parent"
	fi
fi

if [[ -n "$PIPER_REPO" ]]; then
	export PYTHONPATH="$PIPER_REPO${PYTHONPATH:+:$PYTHONPATH}"
fi

if ! python3 -c "import piper_sample_generator" 2>/dev/null; then
	cat >&2 <<EOF
piper_sample_generator not importable in current python environment.
Activate the piper venv or set PIPER_VENV=/path/to/venv.
EOF
	exit 1
fi

RESULTS=()
for i in "${!KEYWORDS[@]}"; do
	KEYWORD="${KEYWORDS[$i]}"
	LABEL="${LABELS[$i]}"
	OUT_DIR="$OUT_ROOT/$LABEL"
	mkdir -p "$OUT_DIR"

	echo ">>> [$((i + 1))/${#KEYWORDS[@]}] Synthesizing '$KEYWORD' -> $OUT_DIR"
	python3 -m piper_sample_generator \
		--generator "$PIPER_MODEL" \
		--text "$KEYWORD" \
		--output-dir "$OUT_DIR" \
		--max-samples "$MAX_SAMPLES" \
		--max-speakers "$MAX_SPEAKERS" \
		--slerp-weights "$SLERP_WEIGHTS" \
		--sample-rate 16000

	if [[ "$GAIN_AUG" = "1" ]]; then
		echo ">>> Applying gain jitter to $OUT_DIR (peak=${GAIN_PEAK_DBFS} dBFS, sigma=${GAIN_SIGMA_DB} dB, headroom=${GAIN_HEADROOM_DB} dB)"
		apply_gain_jitter "$OUT_DIR"
	fi

	count=$(find "$OUT_DIR" -name '*.wav' | wc -l)
	echo ">>> [$LABEL] Generated $count WAVs in $OUT_DIR"
	RESULTS+=("$LABEL: $count WAVs")
done

echo ">>> Done:"
for line in "${RESULTS[@]}"; do
	echo "    $line"
done
KW_ARGS=""
for lbl in "${LABELS[@]}"; do
	KW_ARGS+=" --keyword $lbl"
done
echo ">>> Next: sof_mww_train_pipeline.sh$KW_ARGS $OUT_ROOT <feat_root> <out_dir>"
