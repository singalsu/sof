#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2026 Intel Corporation. All rights reserved.
#
# End-to-end SOF TFLM wake-word retraining pipeline:
#
#   1. Prepare silence/ + unknown/ WAVs (Speech Commands v2 fetch + slice).
#   2. Assume the caller has already run generate_hey_linux_dataset.sh (or
#      an equivalent) to populate <wav_root>/<keyword>/*.wav.
#   3. Emit mel40 features via run_mfcc_train.sh -> <feat_root>/.
#   4. Train a tiny_conv model, int8-quantize with a real-feature
#      representative dataset, and emit the drop-in
#      <name>_quantized_model_data.{cc,h} + <name>_labels.txt under <out_dir>.
#
# The labels are locked at silence(0), unknown(1), <keyword>(2) so the on-
# device tflmcly KPB trigger rule (max_idx >= 2) still applies.
#
# Prerequisites:
#   - Piper venv with piper-sample-generator (already used earlier).
#   - tflm-train venv with tensorflow + numpy at $TFLM_VENV.
#   - sof-testbench4 built; sof-hda-benchmark-mfccmel4032.tplg available.
#   - sox, xxd on PATH.

set -e

usage() {
	cat >&2 <<EOF
Usage: $0 <wav_root> <feat_root> <out_dir> [keyword]

  wav_root   Dataset root; must already contain <keyword>/*.wav from
             generate_hey_linux_dataset.sh. silence/ and unknown/ will be
             added here.
  feat_root  Mel40 feature output root; <label>/*.raw is produced here.
  out_dir    Where to write <keyword>_quantized_model.tflite, matching
             .cc/.h C-array files, and a labels.txt manifest.
  keyword    Keyword class dir name and file basename. Default: hey_linux.

Env:
  TFLM_VENV        Path to a venv containing tensorflow (default:
                   \$HOME/venvs/tflm-train).
  SKIP_PREP        If set, do not fetch/slice Speech Commands v2 again.
  SKIP_FEATURES    If set, do not re-run testbench feature extraction.
  EPOCHS, BATCH_SIZE, LR   Passed through to train_wov_tflm.py.
EOF
	exit 1
}

WAV_ROOT="${1:-}"
FEAT_ROOT="${2:-}"
OUT_DIR="${3:-}"
KEYWORD="${4:-hey_linux}"
if [[ -z "$WAV_ROOT" || -z "$FEAT_ROOT" || -z "$OUT_DIR" ]]; then
	usage
fi

: "${TFLM_VENV:=$HOME/venvs/tflm-train}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

if [[ ! -d "$WAV_ROOT/$KEYWORD" ]]; then
	echo "Missing $WAV_ROOT/$KEYWORD/ — run generate_${KEYWORD}_dataset.sh first" >&2
	exit 1
fi

# Step 1: silence + unknown (in Piper/system Python; no TF needed).
if [[ -z "${SKIP_PREP:-}" ]]; then
	echo ">>> [1/3] Preparing silence + unknown"
	"$SCRIPT_DIR/prepare_silence_unknown.sh" "$WAV_ROOT"
else
	echo ">>> [1/3] Skipping silence/unknown prep (SKIP_PREP set)"
fi

# Step 2: feature extraction via testbench.
if [[ -z "${SKIP_FEATURES:-}" ]]; then
	echo ">>> [2/3] Emitting mel40 features via SOF testbench"
	"$SCRIPT_DIR/run_mfcc_train.sh" "$WAV_ROOT" "$FEAT_ROOT"
else
	echo ">>> [2/3] Skipping feature extraction (SKIP_FEATURES set)"
fi

# Step 3: train + quantize (needs the TF venv).
if [[ ! -x "$TFLM_VENV/bin/python3" ]]; then
	cat >&2 <<EOF
tflm-train venv not found at $TFLM_VENV. Create it once with:

    python3 -m venv $TFLM_VENV
    source $TFLM_VENV/bin/activate
    pip install --upgrade pip
    pip install "tensorflow>=2.10" numpy

Then rerun this script (or set TFLM_VENV=/path/to/venv).
EOF
	exit 1
fi

echo ">>> [3/3] Training tiny_conv and int8-quantizing"
# shellcheck disable=SC1091
source "$TFLM_VENV/bin/activate"
cd "$SCRIPT_DIR"
python3 train_wov_tflm.py \
	--feat-root "$FEAT_ROOT" \
	--labels silence unknown "$KEYWORD" \
	--out-dir "$OUT_DIR" \
	--name "$KEYWORD" \
	${EPOCHS:+--epochs "$EPOCHS"} \
	${BATCH_SIZE:+--batch-size "$BATCH_SIZE"} \
	${LR:+--lr "$LR"}

cat <<EOF

>>> Model ready: $OUT_DIR

To wire it into SOF:
  cp $OUT_DIR/${KEYWORD}_quantized_model_data.{cc,h} \\
     $(cd "$SCRIPT_DIR"/../../tensorflow && pwd)/

Then in src/audio/tensorflow/speech.cc:
  #include "${KEYWORD}_quantized_model_data.h"
  // g_${KEYWORD}_quantized_model_data + _size instead of g_micro_speech_*.

And in src/audio/tensorflow/speech.h, update TFLM_CATEGORY_COUNT and
TFLM_CATEGORY_DATA to match ${OUT_DIR}/${KEYWORD}_labels.txt (silence=0,
unknown=1, ${KEYWORD}=2 — preserves the KPB max_idx >= 2 rule).
EOF
