#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2026 Intel Corporation. All rights reserved.
#
# Generate a synthetic "hey linux" WAV dataset with piper-sample-generator,
# augment it (impulse-response convolution, volume jitter, resample to 16 kHz),
# and drop the result into the layout expected by run_mfcc_train.sh:
#
#   <out_root>/hey_linux/*.wav
#
# Companion `silence` and `unknown` classes are NOT produced here — those are
# dataset-wide and best sourced from Speech Commands v2 (see README).
#
# Requirements:
#   - piper-sample-generator installed in an activated venv (or $PIPER_VENV set
#     to a venv root and this script activates it for you).
#   - LibriTTS-R generator checkpoint at $PIPER_MODEL (defaults to the copy in
#     ~/git/piper-sample-generator/models/). Fetch once with:
#       wget -O <models>/en_US-libritts_r-medium.pt \
#         https://github.com/rhasspy/piper-sample-generator/releases/download/v2.0.0/en_US-libritts_r-medium.pt
#
# Env knobs (all optional):
#   PIPER_VENV      Path to a Piper venv to auto-activate.
#   PIPER_MODEL     Path to the LibriTTS-R .pt checkpoint.
#   PIPER_REPO      Path to a piper-sample-generator git clone root. Needed
#                   because upstream does not pip-install the sibling
#                   piper_train package that __main__.py imports from;
#                   auto-detected when PIPER_MODEL sits under <repo>/models/.
#   MAX_SAMPLES     Positive clips per speaking-rate loop (default 400).
#   MAX_SPEAKERS    Cap on speaker-embedding index (default 700; upstream
#                   warns higher indices produce artifacts).
#   KEYWORD         Utterance text (default "hey linux").
#   LABEL           Output subdir name (default derived from KEYWORD).

set -e

OUT_ROOT="${1:-}"
if [[ -z "$OUT_ROOT" ]]; then
	cat >&2 <<EOF
Usage: $0 <out_root>

  out_root   Dataset root; <label>/*.wav is produced under it, ready to feed
             to run_mfcc_train.sh <out_root> <feat_root>.

Optional env: PIPER_VENV, PIPER_MODEL, MAX_SAMPLES, MAX_SPEAKERS, KEYWORD, LABEL.
EOF
	exit 1
fi

: "${KEYWORD:=hey linux}"
: "${LABEL:=$(echo "$KEYWORD" | tr ' A-Z' '_a-z')}"
: "${MAX_SAMPLES:=400}"
: "${MAX_SPEAKERS:=700}"
: "${PIPER_MODEL:=$HOME/git/piper-sample-generator/models/en_US-libritts_r-medium.pt}"

if [[ -n "$PIPER_VENV" ]]; then
	# shellcheck disable=SC1091
	source "$PIPER_VENV/bin/activate"
fi

if ! command -v python3 >/dev/null; then
	echo "python3 not on PATH" >&2
	exit 1
fi

if ! python3 -c "import piper_sample_generator" 2>/dev/null; then
	echo "piper_sample_generator not importable — activate the Piper venv first" >&2
	echo "(or set PIPER_VENV=/path/to/venv)" >&2
	exit 1
fi

# Upstream pyproject.toml only packages piper_sample_generator; the sibling
# piper_train package that __main__.py imports from is not pip-installed. If
# PIPER_REPO points at a git clone (auto-detected from PIPER_MODEL location),
# put it on PYTHONPATH so `python -m piper_sample_generator` can find both.
if [[ -z "$PIPER_REPO" && "$PIPER_MODEL" == */models/*.pt ]]; then
	candidate=$(cd "$(dirname "$PIPER_MODEL")/.." && pwd)
	if [[ -d "$candidate/piper_train" ]]; then
		PIPER_REPO="$candidate"
	fi
fi
if [[ -n "$PIPER_REPO" ]]; then
	if ! python3 -c "import piper_train" 2>/dev/null; then
		export PYTHONPATH="$PIPER_REPO${PYTHONPATH:+:$PYTHONPATH}"
		echo ">>> Added $PIPER_REPO to PYTHONPATH for piper_train"
	fi
fi

if ! python3 -c "import piper_train" 2>/dev/null; then
	cat >&2 <<EOF
piper_train not importable. Upstream ships it in the git repo but does not
pip-install it. Either:
  - set PIPER_REPO=/path/to/piper-sample-generator (git clone root), or
  - run this script with the repo root on PYTHONPATH.
EOF
	exit 1
fi

if [[ ! -f "$PIPER_MODEL" ]]; then
	cat >&2 <<EOF
Piper model not found at: $PIPER_MODEL

Fetch once with:
  mkdir -p "$(dirname "$PIPER_MODEL")"
  wget -O "$PIPER_MODEL" \\
    https://github.com/rhasspy/piper-sample-generator/releases/download/v2.0.0/en_US-libritts_r-medium.pt

Or set PIPER_MODEL=/path/to/en_US-libritts_r-medium.pt.
EOF
	exit 1
fi

RAW_DIR="$OUT_ROOT/_raw/$LABEL"
FINAL_DIR="$OUT_ROOT/$LABEL"
mkdir -p "$RAW_DIR" "$FINAL_DIR"

echo ">>> Generating '$KEYWORD' into $RAW_DIR"
echo "    model=$PIPER_MODEL"
echo "    max_samples=$MAX_SAMPLES per rate loop, max_speakers=$MAX_SPEAKERS"

# Loop over speaking rates for prosody variety. --length-scales can take
# multiple values but cycling here also cycles the RNG seed and speaker draws.
for scale in 0.9 1.0 1.1; do
	echo ">>> length_scale=$scale"
	python3 -m piper_sample_generator "$KEYWORD" \
		--model "$PIPER_MODEL" \
		--max-samples "$MAX_SAMPLES" \
		--max-speakers "$MAX_SPEAKERS" \
		--length-scales "$scale" \
		--output-dir "$RAW_DIR"
	# piper writes 0.wav..N.wav each run; rename to avoid overwrite next loop.
	tag="s$(echo "$scale" | tr -d .)"
	for f in "$RAW_DIR"/*.wav; do
		base=$(basename "$f" .wav)
		# Only rename freshly emitted files (no tag prefix yet).
		case "$base" in
			s*_*) continue ;;
		esac
		mv "$f" "$RAW_DIR/${tag}_${base}.wav"
	done
done

echo ">>> Augmenting into $FINAL_DIR (16 kHz, IR convolution, volume jitter)"
python3 -m piper_sample_generator.augment \
	--sample-rate 16000 \
	"$RAW_DIR" "$FINAL_DIR"

n_raw=$(find "$RAW_DIR" -name '*.wav' | wc -l)
n_final=$(find "$FINAL_DIR" -name '*.wav' | wc -l)
echo ">>> Done: $n_raw raw / $n_final augmented WAVs in $FINAL_DIR"
echo ">>> Next: run_mfcc_train.sh $OUT_ROOT <feat_root>"
