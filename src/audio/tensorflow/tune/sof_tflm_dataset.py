# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2026 Intel Corporation. All rights reserved.
"""
SOF mel40 feature loader for TFLM wake-on-voice training.

Consumes the raw hop records that ``sof_mfcc_extract_features.sh`` emits (one
file per input WAV, produced by the SOF testbench):

    <feat_root>/<label>/<basename>.raw

Each hop starts with the on-device wire format:

    struct mfcc_data_header  (24 bytes)  magic, frame_number, reserved,
                                         energy, noise_energy, vad_flag
    int32_t values[40]                   Q9.23 mel-log bins

The loader locates hops by scanning for ``MFCC_MAGIC`` (0x6d666363) so it
works for both the packed on-wire compress layout (184 bytes/hop, back to
back) and the legacy PCM-sink layout the testbench emits when the MFCC
config has ``compress_output=false`` (24-byte header + 160 mel bytes +
zero-padding out to ``sink_frame_bytes * frame_shift`` per hop; e.g. 2560
bytes for a stereo S32 16 kHz sink at 20 ms hop). At each magic offset it
reads the 24-byte header and the following 40 int32 Q9.23 mel bins,
converts to float32, and slices each recording into fixed-size 49-hop
windows to match the tflmcly sliding window (49 hops * 40 channels =
1960-value input tensor).

Standalone CLI mode prints dataset shape and per-label counts for a quick
sanity check::

    python3 sof_tflm_dataset.py <feat_root> silence unknown <keyword>
"""

from __future__ import annotations

import glob
import os
import sys

import numpy as np

HOP_HEADER_BYTES = 24
HOP_BINS = 40
HOP_BYTES = HOP_HEADER_BYTES + HOP_BINS * 4
WINDOW_HOPS = 49
MFCC_MAGIC = 0x6D666363  # 'ccfm' on disk, matches struct mfcc_data_header.magic


def load_raw_hops(path: str) -> np.ndarray:
    """Read a testbench-emitted .raw file, return (N_hops, 40) float32.

    Scans the file for MFCC magic markers and decodes the fixed 184-byte
    hop payload at each hit, so the same loader works for compress-packed
    (184 B/hop) and legacy PCM-padded (e.g. 2560 B/hop) layouts.
    """
    data = np.fromfile(path, dtype=np.uint8)
    if data.size < HOP_BYTES:
        return np.zeros((0, HOP_BINS), dtype=np.float32)
    n_u32 = data.size // 4
    u32 = data[: n_u32 * 4].view(np.uint32)
    magic_positions = np.flatnonzero(u32 == MFCC_MAGIC) * 4
    if magic_positions.size == 0:
        return np.zeros((0, HOP_BINS), dtype=np.float32)
    # Drop any tail magic without a full 184-byte payload behind it.
    magic_positions = magic_positions[
        magic_positions + HOP_BYTES <= data.size
    ]
    if magic_positions.size == 0:
        return np.zeros((0, HOP_BINS), dtype=np.float32)
    offsets = magic_positions[:, None] + (
        HOP_HEADER_BYTES + np.arange(HOP_BINS * 4)
    )
    mel_bytes = data[offsets].reshape(-1, HOP_BINS * 4)
    q = mel_bytes.view(np.int32).reshape(-1, HOP_BINS)
    return q.astype(np.float32) / float(1 << 23)


def window_hops(
    feat: np.ndarray,
    n: int = WINDOW_HOPS,
    hop_step: int | None = None,
) -> np.ndarray:
    """Slice a (N_hops, 40) feature stream into (K, n, 40) windows."""
    if feat.shape[0] == 0:
        return np.zeros((1, n, HOP_BINS), dtype=np.float32)
    if feat.shape[0] < n:
        # Repeat last frame to reach one full window.
        pad = n - feat.shape[0]
        feat = np.concatenate(
            [feat, np.repeat(feat[-1:], pad, axis=0)], axis=0
        )
    if hop_step is None:
        hop_step = n
    starts = list(range(0, feat.shape[0] - n + 1, hop_step))
    if not starts:
        starts = [0]
    return np.stack([feat[s : s + n] for s in starts], axis=0)


def load_dataset(
    feat_root: str,
    labels: list[str],
    hop_step: int | None = None,
) -> tuple[np.ndarray, np.ndarray]:
    """Return X (N, 49, 40, 1) float32 and y (N,) int32 for the given labels.

    Label index in ``labels`` becomes the integer class id — keep the caller's
    order stable across training and inference. For the SOF tflmcly KPB rule
    to fire, put silence at index 0 and unknown at index 1; any keyword class
    must sit at index >= 2.
    """
    X_parts: list[np.ndarray] = []
    y_parts: list[np.ndarray] = []
    for label_idx, label in enumerate(labels):
        pattern = os.path.join(feat_root, label, "*.raw")
        files = sorted(glob.glob(pattern))
        if not files:
            print(f"warning: no .raw files under {pattern}", file=sys.stderr)
            continue
        for path in files:
            feat = load_raw_hops(path)
            wins = window_hops(feat, hop_step=hop_step)
            X_parts.append(wins)
            y_parts.append(
                np.full(wins.shape[0], label_idx, dtype=np.int32)
            )
    if not X_parts:
        raise RuntimeError(f"no .raw features found under {feat_root}")
    X = np.concatenate(X_parts, axis=0).astype(np.float32)
    y = np.concatenate(y_parts, axis=0)
    return X[..., np.newaxis], y


def _cli() -> int:
    if len(sys.argv) < 3:
        print(
            "usage: sof_tflm_dataset.py <feat_root> <label1> [label2 ...]",
            file=sys.stderr,
        )
        return 1
    X, y = load_dataset(sys.argv[1], sys.argv[2:])
    print(f"X: {X.shape} {X.dtype} min={X.min():.3f} max={X.max():.3f}")
    print(f"y: {y.shape} {y.dtype} counts={np.bincount(y).tolist()}")
    return 0


if __name__ == "__main__":
    sys.exit(_cli())
