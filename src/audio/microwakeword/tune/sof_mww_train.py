# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2026 Intel Corporation. All rights reserved.
"""
Train a microWakeWord (MWW) streaming wake-word model against SOF mel40
features and emit drop-in replacements for:
  - ``src/audio/microwakeword/mww_model_data.{cc,h}`` (static C-array)
  - Topology2 config blob ``tools/topology/topology2/include/components/mww/<name>.conf``
  - Runtime sof-ctl IPC4 text blob ``tools/ctl/ipc4/mww/<name>.txt``

The model processes 40-bin mel features extracted by the SOF host testbench at
10 ms hop stride. During streaming deployment, the model receives 3 fresh hops
(``MWW_FEATURE_SLICE_COUNT = 3``, shape ``(1, 3, 40)``) per inference step.

Requires TensorFlow >= 2.10.
"""

from __future__ import annotations

import argparse
import datetime
import os
import struct
import sys
from pathlib import Path

import numpy as np

import sof_mww_dataset as mww_dataset

try:
    import tensorflow as tf
except ImportError as exc:
    print(
        "tensorflow not importable; please activate your training venv first",
        file=sys.stderr,
    )
    raise SystemExit(1) from exc


def build_mww_model(window_hops: int = 100, num_mels: int = 40) -> tf.keras.Model:
    """Build a causal convolutional keyword spotting model for offline training."""
    inputs = tf.keras.Input(shape=(window_hops, num_mels, 1), name="mel40_in")

    # Layer 1: Time-frequency convolution
    x = tf.keras.layers.Conv2D(
        filters=32,
        kernel_size=(5, 4),
        strides=(1, 2),
        padding="same",
        activation="relu",
        name="conv1",
    )(inputs)
    x = tf.keras.layers.BatchNormalization(name="bn1")(x)

    # Layer 2: Depthwise separable convolution across time
    x = tf.keras.layers.DepthwiseConv2D(
        kernel_size=(5, 3),
        strides=(2, 2),
        padding="same",
        activation="relu",
        name="dwconv2",
    )(x)
    x = tf.keras.layers.Conv2D(
        filters=48,
        kernel_size=(1, 1),
        strides=(1, 1),
        padding="same",
        activation="relu",
        name="pwconv2",
    )(x)
    x = tf.keras.layers.BatchNormalization(name="bn2")(x)

    # Layer 3: Temporal reduction
    x = tf.keras.layers.GlobalAveragePooling2D(name="gap")(x)
    x = tf.keras.layers.Dropout(0.3, name="dropout")(x)
    x = tf.keras.layers.Dense(32, activation="relu", name="dense1")(x)
    outputs = tf.keras.layers.Dense(1, activation="sigmoid", name="prob")(x)

    return tf.keras.Model(inputs=inputs, outputs=outputs, name="mww_base_model")


def build_streaming_inference_model(
    trained_model: tf.keras.Model,
    slice_hops: int = 3,
    num_mels: int = 40,
    buffer_hops: int = 97,
) -> tf.keras.Model:
    """Wrap trained weights into a streaming inference model with state variables.

    Total context = buffer_hops + slice_hops = 100 hops (1.0 sec).
    """
    total_hops = buffer_hops + slice_hops

    class StreamingMWW(tf.keras.Model):
        def __init__(self, base_model: tf.keras.Model, **kwargs):
            super().__init__(**kwargs)
            self.base_model = base_model
            self.state = tf.Variable(
                initial_value=tf.zeros((1, buffer_hops, num_mels, 1), dtype=tf.float32),
                trainable=False,
                name="stream_state",
            )

        @tf.function(
            input_signature=[
                tf.TensorSpec(shape=(1, slice_hops, num_mels), dtype=tf.float32, name="input_hops")
            ]
        )
        def call(self, inputs):
            # inputs shape: (1, slice_hops, num_mels) -> expand to (1, slice_hops, num_mels, 1)
            x_in = tf.expand_dims(inputs, axis=-1)
            # Concatenate past ring-buffer state with new slices
            full_window = tf.concat([self.state, x_in], axis=1)
            # Update state by dropping the oldest slice_hops
            new_state = full_window[:, slice_hops:, :, :]
            self.state.assign(new_state)
            # Run base model inference
            return self.base_model(full_window)

    streaming_model = StreamingMWW(trained_model, name="mww_streaming_model")
    return streaming_model


def split_train_val(
    X: np.ndarray,
    y: np.ndarray,
    val_frac: float = 0.2,
    seed: int = 0,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """Stratified train/val split."""
    rng = np.random.default_rng(seed)
    idx = np.arange(y.size)
    train_mask = np.zeros(y.size, dtype=bool)
    for c in np.unique(y):
        cls_idx = idx[y == c]
        rng.shuffle(cls_idx)
        cut = int(round(cls_idx.size * (1.0 - val_frac)))
        train_mask[cls_idx[:cut]] = True
    return X[train_mask], y[train_mask], X[~train_mask], y[~train_mask]


def representative_dataset(
    X: np.ndarray,
    n_samples: int = 200,
    slice_hops: int = 3,
    num_mels: int = 40,
):
    """Yield representative calibration inputs with shape (1, slice_hops, num_mels)."""
    rng = np.random.default_rng(0)
    for _ in range(n_samples):
        idx = rng.integers(0, X.shape[0])
        # Pick 3 consecutive hops from window
        start_t = rng.integers(0, X.shape[1] - slice_hops + 1)
        slice_3 = X[idx, start_t : start_t + slice_hops, :, 0]  # shape: (slice_hops, num_mels)
        yield [slice_3[np.newaxis, ...].astype(np.float32)]


def convert_to_tflite_int8(
    model: tf.keras.Model,
    rep_X: np.ndarray,
    n_rep: int = 200,
) -> bytes:
    """Convert model to int8 quantized TFLite flatbuffer."""
    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.representative_dataset = lambda: representative_dataset(
        rep_X, n_samples=n_rep, slice_hops=3, num_mels=40
    )
    converter.target_spec.supported_ops = [
        tf.lite.OpsSet.TFLITE_BUILTINS_INT8,
    ]
    converter.inference_input_type = tf.int8
    converter.inference_output_type = tf.int8
    return converter.convert()


# SOF IPC4 ABI definitions
SOF_IPC4_ABI_MAGIC = 0x34464F53  # 'SOF4'
SOF_ABI_VERSION = (3 << 24) | (29 << 12) | 1  # 3.29.1
SOF_CTRL_CMD_BINARY = 3


def build_ipc4_abi_blob(tflite_bytes: bytes, param_id: int = 0) -> bytes:
    """Pack tflite model bytes behind a standard 32-byte struct sof_abi_hdr."""
    size = len(tflite_bytes)
    abi_header = struct.pack("<IIIIIIII", SOF_IPC4_ABI_MAGIC, param_id, size, SOF_ABI_VERSION, 0, 0, 0, 0)
    pad = (4 - (size % 4)) % 4
    return abi_header + tflite_bytes + b"\x00" * pad


def emit_c_array(
    tflite_bytes: bytes,
    out_cc: Path,
    out_h: Path,
    symbol: str = "mww_model_data",
) -> None:
    """Emit C array source and header."""
    guard = symbol.upper() + "_H_"
    with open(out_h, "w") as f:
        f.write("// SPDX-License-Identifier: BSD-3-Clause\n")
        f.write("// Auto-generated by sof_mww_train.py — do not edit.\n\n")
        f.write(f"#ifndef {guard}\n#define {guard}\n\n")
        f.write("#include <cstddef>\n#include <cstdint>\n\n")
        f.write(f"extern const unsigned char {symbol}[];\n")
        f.write(f"extern const size_t {symbol}_size;\n\n")
        f.write(f"#endif  // {guard}\n")

    with open(out_cc, "w") as f:
        f.write("// SPDX-License-Identifier: BSD-3-Clause\n")
        f.write("// Auto-generated by sof_mww_train.py — do not edit.\n\n")
        f.write(f'#include "{out_h.name}"\n\n')
        f.write(f"alignas(16) const unsigned char {symbol}[] = {{\n")
        line = "    "
        for i, b in enumerate(tflite_bytes):
            line += f"0x{b:02x}, "
            if (i + 1) % 12 == 0:
                f.write(line.rstrip() + "\n")
                line = "    "
        if line.strip():
            f.write(line.rstrip() + "\n")
        f.write("};\n\n")
        f.write(f"const size_t {symbol}_size = {len(tflite_bytes)};\n")


def emit_topology2_conf(
    tflite_bytes: bytes,
    out_conf: Path,
    model_name: str,
) -> None:
    """Emit Topology2 configuration blob in .conf format."""
    blob8 = build_ipc4_abi_blob(tflite_bytes)
    words = struct.unpack(f"<{len(blob8) // 4}I", blob8)
    out_conf.parent.mkdir(parents=True, exist_ok=True)
    today = datetime.date.today().strftime("%d-%b-%Y")
    data_name = f"mww_config_{model_name}"
    with open(out_conf, "w") as f:
        f.write(f"# Exported microWakeWord Model Control Words {today}\n")
        f.write(f"# Model: {model_name}\n")
        f.write(f"# Auto-generated by sof_mww_train.py — do not edit.\n")
        f.write(f'Object.Base.data."{data_name}" {{\n')
        f.write('\twords "\n')
        lines = []
        for i in range(0, len(words), 8):
            chunk = words[i : i + 8]
            lines.append(",".join(f"0x{w:08x}" for w in chunk))
        for idx, line in enumerate(lines):
            if idx == len(lines) - 1:
                f.write(f"\t\t{line}\"\n")
            else:
                f.write(f"\t\t{line},\n")
        f.write("}\n")


def emit_sofctl_ipc4_txt(
    tflite_bytes: bytes,
    out_txt: Path,
) -> None:
    """Emit sof-ctl IPC4 text blob in comma-separated uint32 CSV format."""
    size = len(tflite_bytes)
    tlv_cmd = SOF_CTRL_CMD_BINARY
    tlv_size = 32 + size
    pad = (4 - (size % 4)) % 4
    payload_padded = tflite_bytes + b"\x00" * pad
    tlv_header = struct.pack("<II", tlv_cmd, tlv_size)
    abi_header = struct.pack("<IIIIIIII", SOF_IPC4_ABI_MAGIC, 0, size, SOF_ABI_VERSION, 0, 0, 0, 0)
    blob = tlv_header + abi_header + payload_padded

    words = struct.unpack(f"<{len(blob) // 4}I", blob)
    out_txt.parent.mkdir(parents=True, exist_ok=True)
    with open(out_txt, "w") as f:
        f.write(", ".join(f"0x{w:08x}" for w in words) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--feat-root", required=True, help="Directory with <label>/*.raw feature files")
    parser.add_argument("--keyword", required=True, action="append", help="Target positive keyword class(es)")
    parser.add_argument("--name", default="mww_model", help="Base name for exported artifacts")
    parser.add_argument("--out-dir", default=".", help="Output directory for trained artifacts")
    parser.add_argument("--epochs", type=int, default=30, help="Training epochs (default 30)")
    parser.add_argument("--batch-size", type=int, default=64, help="Batch size (default 64)")
    parser.add_argument("--lr", type=float, default=1e-3, help="Learning rate (default 1e-3)")
    parser.add_argument("--val-frac", type=float, default=0.2, help="Validation fraction (default 0.2)")
    parser.add_argument("--seed", type=int, default=0, help="Random seed (default 0)")

    args = parser.parse_args()
    labels = ["silence", "unknown"] + args.keyword
    print(f">>> Loading dataset from {args.feat_root} for classes: {labels}")

    X_raw, y = mww_dataset.load_dataset(
        args.feat_root,
        labels=labels,
        seed=args.seed,
    )

    if X_raw.shape[0] == 0:
        print("Error: empty dataset loaded. Check feat-root path.", file=sys.stderr)
        return 1

    print(f">>> Applying soft mel-log AGC (target 0 dB, release 0.5 dB/s)...")
    X = mww_dataset.apply_soft_agc(X_raw)

    X_train, y_train, X_val, y_val = split_train_val(X, y, val_frac=args.val_frac, seed=args.seed)
    print(f">>> Training split: {X_train.shape[0]} samples (pos: {np.sum(y_train == 1)}, neg: {np.sum(y_train == 0)})")
    print(f">>> Validation split: {X_val.shape[0]} samples (pos: {np.sum(y_val == 1)}, neg: {np.sum(y_val == 0)})")

    model = build_mww_model(window_hops=mww_dataset.WINDOW_HOPS, num_mels=mww_dataset.HOP_BINS)
    model.compile(
        optimizer=tf.keras.optimizers.Adam(learning_rate=args.lr),
        loss=tf.keras.losses.BinaryCrossentropy(),
        metrics=["accuracy", tf.keras.metrics.Precision(name="prec"), tf.keras.metrics.Recall(name="rec")],
    )

    print(">>> Training base model...")
    model.fit(
        X_train,
        y_train,
        validation_data=(X_val, y_val),
        epochs=args.epochs,
        batch_size=args.batch_size,
        verbose=2,
    )

    print(">>> Wrapping into streaming inference model...")
    streaming_model = build_streaming_inference_model(model, slice_hops=3, num_mels=40)

    print(">>> Quantizing to int8 with representative dataset...")
    tflite_bytes = convert_to_tflite_int8(streaming_model, rep_X=X_train)
    print(f">>> Quantized TFLite size: {len(tflite_bytes)} bytes")

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    # 1. Save .tflite flatbuffer
    tflite_path = out_dir / f"{args.name}_quantized_model.tflite"
    with open(tflite_path, "wb") as f:
        f.write(tflite_bytes)
    print(f">>> Saved {tflite_path}")

    # 2. Save static C array files
    cc_path = out_dir / "mww_model_data.cc"
    h_path = out_dir / "mww_model_data.h"
    emit_c_array(tflite_bytes, cc_path, h_path, symbol="mww_model_data")
    print(f">>> Emitted C array to {cc_path} and {h_path}")

    # 3. Save Topology2 config
    conf_path = out_dir / f"{args.name}.conf"
    emit_topology2_conf(tflite_bytes, conf_path, model_name=args.name)
    print(f">>> Emitted Topology2 conf to {conf_path}")

    # 4. Save sof-ctl IPC4 text
    txt_path = out_dir / f"{args.name}.txt"
    emit_sofctl_ipc4_txt(tflite_bytes, txt_path)
    print(f">>> Emitted sof-ctl text to {txt_path}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
