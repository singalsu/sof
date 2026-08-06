#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2026 Intel Corporation. All rights reserved.
#
# Prepare the silence/ and unknown/ dataset classes for TFLM wake-on-voice
# training by fetching Google Speech Commands v2 and slicing/sampling from it.
#
# Output layout (merges with any existing keyword class dirs under <out_root>):
#   <out_root>/silence/*.wav   1-second slices of _background_noise_/*.wav
#   <out_root>/unknown/*.wav   random sample of non-target Speech Commands words
#
# Speech Commands v2 is 2.4 GB (~106k files). It is cached under $SC_CACHE
# (default ~/.cache/speech_commands_v2) so subsequent runs are instant.
#
# Requirements: sox on PATH, ~3 GB of free disk for the cache.

set -e

OUT_ROOT="${1:-}"
if [[ -z "$OUT_ROOT" ]]; then
	cat >&2 <<EOF
Usage: $0 <out_root>

  out_root   Dataset root; silence/ and unknown/ are added under it, alongside
             any keyword class directories already present.

Optional env:
  SC_CACHE          Directory holding the unpacked Speech Commands v2 tree
                    (default: ~/.cache/speech_commands_v2).
  N_SILENCE         Number of 1-second silence clips to emit (default 500).
  N_UNKNOWN         Number of unknown-class clips to sample (default 1500).
  UNKNOWN_WORDS     Space-separated word list to sample for unknown
                    (default: every SC v2 word except _background_noise_).
EOF
	exit 1
fi

: "${SC_CACHE:=$HOME/.cache/speech_commands_v2}"
: "${N_SILENCE:=500}"
: "${N_UNKNOWN:=1500}"

if ! command -v sox >/dev/null; then
	echo "sox not on PATH — please install (e.g. apt install sox)" >&2
	exit 1
fi

# Fetch + unpack once.
if [[ ! -d "$SC_CACHE/_background_noise_" ]]; then
	echo ">>> Fetching Speech Commands v2 into $SC_CACHE (~2.4 GB, one-time)"
	mkdir -p "$SC_CACHE"
	url="http://download.tensorflow.org/data/speech_commands_v0.02.tar.gz"
	tarball="$SC_CACHE/speech_commands_v0.02.tar.gz"
	if [[ ! -f "$tarball" ]]; then
		wget -O "$tarball" "$url"
	fi
	tar -C "$SC_CACHE" -xzf "$tarball"
fi

SILENCE_OUT="$OUT_ROOT/silence"
UNKNOWN_OUT="$OUT_ROOT/unknown"
mkdir -p "$SILENCE_OUT" "$UNKNOWN_OUT"

# --- silence class ---------------------------------------------------------
# Each _background_noise_/*.wav is ~60 s. Slice into 1 s chunks at random
# offsets to build up the target count; sox trim handles this cleanly.
echo ">>> Producing $N_SILENCE silence clips into $SILENCE_OUT"
mapfile -t noise_wavs < <(find "$SC_CACHE/_background_noise_" -name '*.wav')
if [[ ${#noise_wavs[@]} -eq 0 ]]; then
	echo "No _background_noise_ wavs found in $SC_CACHE" >&2
	exit 1
fi
i=0
while [[ $i -lt $N_SILENCE ]]; do
	src="${noise_wavs[$((RANDOM % ${#noise_wavs[@]}))]}"
	dur=$(sox --i -D "$src" | awk '{printf "%d", $1}')
	if [[ $dur -lt 2 ]]; then continue; fi
	start=$((RANDOM % (dur - 1)))
	sox "$src" -r 16000 -c 1 -b 16 "$SILENCE_OUT/silence_${i}.wav" \
		trim "$start" 1
	i=$((i + 1))
done

# --- unknown class ---------------------------------------------------------
# Sample from every top-level SC v2 word directory except _background_noise_
# (and skip any that collide with keyword classes already under out_root).
echo ">>> Sampling $N_UNKNOWN unknown clips into $UNKNOWN_OUT"
if [[ -z "${UNKNOWN_WORDS:-}" ]]; then
	mapfile -t word_dirs < <(find "$SC_CACHE" -maxdepth 1 -mindepth 1 -type d \
		! -name '_background_noise_')
else
	word_dirs=()
	for w in $UNKNOWN_WORDS; do word_dirs+=("$SC_CACHE/$w"); done
fi

# Skip words whose name already exists as an out_root/<word>/ class dir.
filtered=()
for d in "${word_dirs[@]}"; do
	w=$(basename "$d")
	if [[ -d "$OUT_ROOT/$w" && "$w" != "unknown" && "$w" != "silence" ]]; then
		echo "  skipping '$w' (collides with existing keyword class)"
		continue
	fi
	[[ -d "$d" ]] && filtered+=("$d")
done

# Enumerate all candidate wavs, shuf to N_UNKNOWN, sox-convert to 16k/mono/16b.
tmp_list=$(mktemp)
trap 'rm -f "$tmp_list"' EXIT
for d in "${filtered[@]}"; do
	find "$d" -name '*.wav' >> "$tmp_list"
done
if ! shuf --version >/dev/null 2>&1; then
	echo "shuf not on PATH — please install coreutils" >&2
	exit 1
fi
shuf -n "$N_UNKNOWN" "$tmp_list" | \
while read -r src; do
	base=$(basename "$src" .wav)
	# Prefix with parent dir name so we don't collide across words.
	parent=$(basename "$(dirname "$src")")
	sox "$src" -r 16000 -c 1 -b 16 \
		"$UNKNOWN_OUT/${parent}_${base}.wav" \
		2>/dev/null || true
done

n_silence=$(find "$SILENCE_OUT" -name '*.wav' | wc -l)
n_unknown=$(find "$UNKNOWN_OUT" -name '*.wav' | wc -l)
echo ">>> Done: $n_silence silence / $n_unknown unknown WAVs under $OUT_ROOT"
