#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2026 Intel Corporation. All rights reserved.
#
# End-to-end microWakeWord (MWW) retraining pipeline:
#
#   1. Prepare silence/ + unknown/ negative classes (Speech Commands v2).
#   2. Assume the caller has generated <wav_root>/<keyword>/*.wav with
#      sof_mww_generate_keyword_dataset_piper_tts.sh or
#      sof_mww_generate_keyword_dataset.sh.
#   3. Extract 10ms mel40 features via sof_mfcc_extract_features.sh -> <feat_root>/.
#   4. Train streaming MWW model, int8-quantize, and emit:
#      - <out_dir>/<name>_quantized_model.tflite
#      - <out_dir>/mww_model_data.{cc,h} (drop-in C array)
#      - <out_dir>/<name>.conf (Topology2 data blob)
#      - <out_dir>/<name>.txt (sof-ctl IPC4 text blob)
#   5. Run off-device verification with sof_mww_verify.py.
#
# Usage:
#   sof_mww_train_pipeline.sh --keyword hey_intel <wav_root> <feat_root> <out_dir>

set -e

usage() {
	cat >&2 <<EOF
Usage: $0 --keyword <label> [--keyword <label> ...] [--name <base>] \
          [--format S16|S24|S32] [--tplg <path>] \
          <wav_root> <feat_root> <out_dir>

  --keyword LBL   Positive keyword class directory name (repeatable). Must
                  match a subdirectory under <wav_root>.
  --name BASE     Base name for the output model files (default: derived from
                  keyword labels).
  --format FMT    Testbench sample container format (S16, S24, S32). Default S32.
  --tplg PATH     Explicit path to benchmark topology file (default:
                  sof-hda-benchmark-mfccmel40_10ms<SF>.tplg).

  wav_root        Dataset root directory; must contain <keyword>/*.wav.
                  silence/ and unknown/ will be populated here.
  feat_root       Mel40 feature output root; <label>/*.raw is written here.
  out_dir         Output directory for trained model artifacts.

Env:
  SKIP_PREP        If set, do not re-fetch/re-slice Speech Commands v2.
  SKIP_FEATURES    If set, do not re-run testbench feature extraction.
  EPOCHS           Training epochs (default 30).
  BATCH_SIZE       Training batch size (default 64).
  LR               Learning rate (default 0.001).
  CLASS_WEIGHT_NEG Loss penalty for negative class (default 5.0).
  THRESHOLD        Verification detection threshold (default 0.85).
  GAIN_AUG         Passed to negative class preparation (default 0).
EOF
	exit 1
}

KEYWORDS=()
NAME=""
FORMAT=""
CUSTOM_TPLG=""
POSITIONAL=()

while [[ $# -gt 0 ]]; do
	case "$1" in
		-k|--keyword)
			[[ $# -ge 2 ]] || usage
			KEYWORDS+=("$2"); shift 2 ;;
		-n|--name)
			[[ $# -ge 2 ]] || usage
			NAME="$2"; shift 2 ;;
		--format)
			[[ $# -ge 2 ]] || usage
			FORMAT="$2"; shift 2 ;;
		--tplg)
			[[ $# -ge 2 ]] || usage
			CUSTOM_TPLG="$2"; shift 2 ;;
		-h|--help)
			usage ;;
		--)
			shift
			while [[ $# -gt 0 ]]; do POSITIONAL+=("$1"); shift; done ;;
		-*)
			echo "unknown option: $1" >&2; usage ;;
		*)
			POSITIONAL+=("$1"); shift ;;
	esac
done

if [[ ${#KEYWORDS[@]} -eq 0 || ${#POSITIONAL[@]} -ne 3 ]]; then
	usage
fi

WAV_ROOT="${POSITIONAL[0]}"
FEAT_ROOT="${POSITIONAL[1]}"
OUT_DIR="${POSITIONAL[2]}"

if [[ -z "$NAME" ]]; then
	if [[ ${#KEYWORDS[@]} -eq 1 ]]; then
		NAME="${KEYWORDS[0]}"
	else
		NAME=$(IFS=_ ; echo "${KEYWORDS[*]}")
	fi
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

for kw in "${KEYWORDS[@]}"; do
	if [[ ! -d "$WAV_ROOT/$kw" ]]; then
		cat >&2 <<EOF
Error: missing $WAV_ROOT/$kw/ directory.
Generate keyword recordings first using:
  $SCRIPT_DIR/sof_mww_generate_keyword_dataset_piper_tts.sh --keyword "$kw" $WAV_ROOT
EOF
		exit 1
	fi
done

# Step 1: Prepare silence & unknown negative classes
if [[ -z "$SKIP_PREP" ]]; then
	echo "=== Step 1/4: Preparing silence/ and unknown/ dataset classes ==="
	"$SCRIPT_DIR/sof_mww_prepare_silence_unknown.sh" "$WAV_ROOT"
else
	echo "=== Step 1/4: Skipping silence/unknown prep (SKIP_PREP set) ==="
fi

# Step 2: Testbench feature extraction
if [[ -z "$SKIP_FEATURES" ]]; then
	echo "=== Step 2/4: Extracting 10ms mel40 features with testbench ==="
	mkdir -p "$FEAT_ROOT"
	EXTRA_FEAT_ARGS=()
	[[ -n "$FORMAT" ]] && EXTRA_FEAT_ARGS+=(--format "$FORMAT")
	[[ -n "$CUSTOM_TPLG" ]] && EXTRA_FEAT_ARGS+=(--tplg "$CUSTOM_TPLG")
	"$SCRIPT_DIR/sof_mfcc_extract_features.sh" "$WAV_ROOT" "$FEAT_ROOT" "${EXTRA_FEAT_ARGS[@]}"
else
	echo "=== Step 2/4: Skipping feature extraction (SKIP_FEATURES set) ==="
fi

# Step 3: Train & export model
echo "=== Step 3/4: Training streaming MWW model and quantizing ==="
mkdir -p "$OUT_DIR"
KW_ARGS=()
for kw in "${KEYWORDS[@]}"; do
	KW_ARGS+=(--keyword "$kw")
done

python3 "$SCRIPT_DIR/sof_mww_train.py" \
	--feat-root "$FEAT_ROOT" \
	"${KW_ARGS[@]}" \
	--name "$NAME" \
	--out-dir "$OUT_DIR" \
	${EPOCHS:+--epochs "$EPOCHS"} \
	${BATCH_SIZE:+--batch-size "$BATCH_SIZE"} \
	${LR:+--lr "$LR"} \
	${CLASS_WEIGHT_NEG:+--class-weight-neg "$CLASS_WEIGHT_NEG"}

# Step 4: Verification
echo "=== Step 4/4: Running streaming verification ==="
python3 "$SCRIPT_DIR/sof_mww_verify.py" \
	--tflite "$OUT_DIR/${NAME}_quantized_model.tflite" \
	--feat-root "$FEAT_ROOT" \
	"${KW_ARGS[@]}" \
	${THRESHOLD:+--threshold "$THRESHOLD"}

echo "================================================================="
echo "MWW model training complete. Artifacts saved in $OUT_DIR:"
echo "  - $OUT_DIR/${NAME}_quantized_model.tflite"
echo "  - $OUT_DIR/mww_model_data.cc (static C array)"
echo "  - $OUT_DIR/mww_model_data.h"
echo "  - $OUT_DIR/${NAME}.conf (Topology2 data blob)"
echo "  - $OUT_DIR/${NAME}.txt (sof-ctl IPC4 text blob)"
echo "================================================================="
