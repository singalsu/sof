# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2026 Intel Corporation. All rights reserved.
"""
SOF mel40 feature loader for microWakeWord (MWW) streaming wake-on-voice training.

Consumes the raw hop records emitted by ``sof_mfcc_extract_features.sh`` (one
file per input WAV, produced by the SOF host testbench):

    <feat_root>/<label>/<basename>.raw

Each hop starts with the on-device wire format:

    struct mfcc_data_header  (24 bytes)  magic, frame_number, reserved,
                                         energy, noise_energy, vad_flag
    int32_t values[40]                   Q9.23 mel-log bins

The loader locates hops by scanning for ``MFCC_MAGIC`` (0x6d666363). At each
magic offset it reads the 24-byte header and the following 40 int32 Q9.23 mel
bins, converts to float32 (scaled by 2^-23), and slices recordings into
fixed-size 100-hop windows (1.0 second at 10ms hop stride) for training.

Standalone CLI mode prints dataset shape and class counts:

    python3 sof_mww_dataset.py <feat_root> silence unknown <keyword>
"""

from __future__ import annotations

import glob
import os
import sys

import numpy as np

HOP_HEADER_BYTES = 24
HOP_BINS = 40
HOP_BYTES = HOP_HEADER_BYTES + HOP_BINS * 4
WINDOW_HOPS = 100  # 1.0 second at 10 ms hop stride
MFCC_MAGIC = 0x6D666363  # 'ccfm' on disk, matches struct mfcc_data_header.magic

# Soft mel-log AGC applied to log10(mel_power) features. Units mirror the Q9.23
# runtime AGC in mww.c: gain target 0.0 (=0 dB), floor -2.0 (=-20 dB),
# instant attack against MEL_CLIP_MAX=+1.0 (+10 dB), additive release step
# per 10 ms hop giving ~0.5 dB/sec recovery (100 hops/sec).
AGC_GAIN_TARGET = 0.0
AGC_GAIN_FLOOR = -2.0
AGC_CLIP_MAX = 1.0
AGC_RELEASE_STEP_PER_HOP = 0.0005  # 0.5 dB/s / (100 hops/s * 10 dB/unit)


def apply_soft_agc(X: np.ndarray) -> np.ndarray:
    """Apply the mel-log AGC to each window independently.

    ``X`` has shape ``(N, WINDOW_HOPS, HOP_BINS, 1)`` or ``(N, WINDOW_HOPS, HOP_BINS)``.
    Each window gets its own AGC state initialized to ``AGC_GAIN_TARGET``; state
    is not carried across windows.
    """
    if X.size == 0:
        return X
    has_channel = (X.ndim == 4)
    if not has_channel:
        X = X[..., np.newaxis]
    N, T, F, C = X.shape
    out = np.empty_like(X)
    for n in range(N):
        gain = AGC_GAIN_TARGET
        for t in range(T):
            hop = X[n, t, :, 0]
            headroom = AGC_CLIP_MAX - float(hop.max())
            if gain > headroom:
                gain = headroom
            if gain < AGC_GAIN_FLOOR:
                gain = AGC_GAIN_FLOOR
            out[n, t, :, 0] = hop + gain
            if gain < AGC_GAIN_TARGET:
                gain = min(gain + AGC_RELEASE_STEP_PER_HOP, AGC_GAIN_TARGET)
    if not has_channel:
        out = out[..., 0]
    return out


def load_raw_hops(path: str) -> tuple[np.ndarray, np.ndarray]:
    """Read a testbench-emitted .raw file, return (N_hops, 40) float32 mel bins and (N_hops,) vad flags.

    Scans the file for MFCC magic markers and decodes the 184-byte hop payloads.
    """
    data = np.fromfile(path, dtype=np.uint8)
    if data.size < HOP_BYTES:
        return np.zeros((0, HOP_BINS), dtype=np.float32), np.zeros((0,), dtype=np.uint32)
    n_u32 = data.size // 4
    u32 = data[: n_u32 * 4].view(np.uint32)
    magic_positions = np.flatnonzero(u32 == MFCC_MAGIC) * 4
    if magic_positions.size == 0:
        return np.zeros((0, HOP_BINS), dtype=np.float32), np.zeros((0,), dtype=np.uint32)

    valid_pos = [p for p in magic_positions if p + HOP_BYTES <= data.size]
    if not valid_pos:
        return np.zeros((0, HOP_BINS), dtype=np.float32), np.zeros((0,), dtype=np.uint32)

    n_hops = len(valid_pos)
    mel = np.empty((n_hops, HOP_BINS), dtype=np.float32)
    vad = np.empty((n_hops,), dtype=np.uint32)

    for i, p in enumerate(valid_pos):
        header_bytes = data[p : p + HOP_HEADER_BYTES]
        vad[i] = np.frombuffer(header_bytes[20:24], dtype=np.uint32)[0]
        mel_bytes = data[p + HOP_HEADER_BYTES : p + HOP_BYTES]
        mel_q23 = np.frombuffer(mel_bytes, dtype=np.int32)
        mel[i] = mel_q23.astype(np.float32) / (1 << 23)

    return mel, vad


def slice_into_windows(
    mel: np.ndarray,
    window_hops: int = WINDOW_HOPS,
    hop_step: int = 10,
) -> np.ndarray:
    """Slice (N_hops, 40) into (M, window_hops, 40) overlapping windows."""
    n_hops = mel.shape[0]
    if n_hops < window_hops:
        # Zero-pad short recordings at the end
        pad = np.zeros((window_hops - n_hops, HOP_BINS), dtype=mel.dtype)
        return np.concatenate([mel, pad], axis=0)[np.newaxis, ...]

    starts = list(range(0, n_hops - window_hops + 1, hop_step))
    if not starts or starts[-1] != (n_hops - window_hops):
        starts.append(n_hops - window_hops)
    windows = [mel[s : s + window_hops] for s in starts]
    return np.stack(windows, axis=0)


def load_dataset(
    feat_root: str,
    labels: list[str],
    window_hops: int = WINDOW_HOPS,
    hop_step_keyword: int = 5,
    hop_step_negative: int = 20,
    gain_aug_db_min: float = -25.0,
    gain_aug_db_max: float = 5.0,
    seed: int = 0,
) -> tuple[np.ndarray, np.ndarray]:
    """Load and slice feature recordings from <feat_root>/<label>/*.raw.

    Labels typically start with negative classes (``silence``, ``unknown``)
    followed by the positive keyword class(es).
    Returns (X, y) where y is binary (0 for negative classes, 1 for keyword).
    """
    rng = np.random.default_rng(seed)
    all_X: list[np.ndarray] = []
    all_y: list[int] = []

    for class_idx, label in enumerate(labels):
        label_dir = os.path.join(feat_root, label)
        raw_files = sorted(glob.glob(os.path.join(label_dir, "*.raw")))
        if not raw_files:
            print(f"Warning: no .raw feature files found in {label_dir}", file=sys.stderr)
            continue

        is_keyword = (label not in ("silence", "unknown", "noise", "background"))
        step = hop_step_keyword if is_keyword else hop_step_negative
        y_val = 1 if is_keyword else 0

        for f in raw_files:
            mel, _ = load_raw_hops(f)
            if mel.shape[0] == 0:
                continue

            windows = slice_into_windows(mel, window_hops=window_hops, hop_step=step)

            # Apply random gain augmentation during loading
            if gain_aug_db_min is not None and gain_aug_db_max is not None:
                gains_db = rng.uniform(gain_aug_db_min, gain_aug_db_max, size=(windows.shape[0], 1, 1))
                gains_lin = gains_db * 0.1  # 1 decade = 10 dB
                windows = windows + gains_lin

            for w in windows:
                all_X.append(w)
                all_y.append(y_val)

    if not all_X:
        return np.zeros((0, window_hops, HOP_BINS, 1), dtype=np.float32), np.zeros((0,), dtype=np.int32)

    X = np.stack(all_X, axis=0)[..., np.newaxis]
    y = np.array(all_y, dtype=np.int32)
    return X, y


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <feat_root> <label1> [<label2> ...]")
        sys.exit(1)

    feat_root = sys.argv[1]
    labels = sys.argv[2:]
    X, y = load_dataset(feat_root, labels)
    print(f"Loaded dataset: X shape = {X.shape}, y shape = {y.shape}")
    print(f"Positive samples (y=1): {np.sum(y == 1)}, Negative samples (y=0): {np.sum(y == 0)}")
