# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2026 Intel Corporation. All rights reserved.
"""
Off-device verifier for the int8 tflite emitted by ``sof_tflm_train.py``.

Loads the quantized model, reproduces the same stratified train/val split
that training used (same ``--val-frac`` / ``--seed`` defaults), and prints
per-class precision / recall plus a confusion matrix on the held-out
validation split. This catches models that pass overall accuracy but fail
the positive keyword class before we burn a hardware build.

Example::

    python3 sof_tflm_verify.py \\
        --tflite ~/wov/model/<name>_quantized_model.tflite \\
        --feat-root ~/wov/feats \\
        --labels silence,unknown,<keyword>
"""

from __future__ import annotations

import argparse
import sys

import numpy as np

from sof_tflm_dataset import load_dataset
from sof_tflm_train import split_train_val

try:
    import tflite_runtime.interpreter as tflite  # type: ignore
except ImportError:
    from tensorflow.lite.python.interpreter import Interpreter as _Interp

    class tflite:  # type: ignore
        Interpreter = _Interp


def _quantize(x: np.ndarray, scale: float, zero_point: int, dtype) -> np.ndarray:
    q = np.round(x / scale) + zero_point
    info = np.iinfo(dtype)
    return np.clip(q, info.min, info.max).astype(dtype)


def _dequantize(q: np.ndarray, scale: float, zero_point: int) -> np.ndarray:
    return (q.astype(np.float32) - zero_point) * scale


def run_inference(model_path: str, X: np.ndarray) -> np.ndarray:
    interp = tflite.Interpreter(model_path=model_path)
    interp.allocate_tensors()
    in_det = interp.get_input_details()[0]
    out_det = interp.get_output_details()[0]

    in_scale, in_zp = in_det["quantization"]
    out_scale, out_zp = out_det["quantization"]
    in_dtype = in_det["dtype"]

    preds = np.empty((X.shape[0],), dtype=np.int32)
    for i in range(X.shape[0]):
        x = X[i : i + 1]
        if np.issubdtype(in_dtype, np.integer):
            x = _quantize(x, in_scale, in_zp, in_dtype)
        interp.set_tensor(in_det["index"], x)
        interp.invoke()
        y = interp.get_tensor(out_det["index"])
        if np.issubdtype(y.dtype, np.integer):
            y = _dequantize(y, out_scale, out_zp)
        preds[i] = int(np.argmax(y[0]))
    return preds


def confusion(y_true: np.ndarray, y_pred: np.ndarray, n: int) -> np.ndarray:
    m = np.zeros((n, n), dtype=np.int64)
    for t, p in zip(y_true, y_pred):
        m[t, p] += 1
    return m


def print_report(labels: list[str], y_true: np.ndarray, y_pred: np.ndarray) -> None:
    n = len(labels)
    m = confusion(y_true, y_pred, n)
    total = m.sum()
    correct = np.trace(m)
    print(f"\nsamples: {total}  overall accuracy: {correct / total:.4f}\n")

    label_w = max(len(l) for l in labels)
    header = f"  {'class':<{label_w}}  {'support':>7}  {'precision':>9}  {'recall':>7}  {'f1':>6}"
    print(header)
    print("  " + "-" * (len(header) - 2))
    for c in range(n):
        tp = m[c, c]
        fn = m[c, :].sum() - tp
        fp = m[:, c].sum() - tp
        support = m[c, :].sum()
        precision = tp / (tp + fp) if (tp + fp) else 0.0
        recall = tp / (tp + fn) if (tp + fn) else 0.0
        f1 = 2 * precision * recall / (precision + recall) if (precision + recall) else 0.0
        print(
            f"  {labels[c]:<{label_w}}  {support:>7d}  {precision:>9.4f}  "
            f"{recall:>7.4f}  {f1:>6.4f}"
        )

    print("\nconfusion matrix (rows=true, cols=pred):")
    header_cols = "  " + " " * (label_w + 2) + "  ".join(f"{l:>{label_w}}" for l in labels)
    print(header_cols)
    for r in range(n):
        row = "  ".join(f"{m[r, c]:>{label_w}d}" for c in range(n))
        print(f"  {labels[r]:<{label_w}}  {row}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--tflite", required=True, help="int8 .tflite from sof_tflm_train.py")
    ap.add_argument("--feat-root", required=True, help="dir with <label>/*.raw features")
    ap.add_argument(
        "--labels",
        required=True,
        help="comma-separated class list, same order as training",
    )
    ap.add_argument("--val-frac", type=float, default=0.15,
                    help="must match the training run's --val-frac")
    ap.add_argument("--seed", type=int, default=1,
                    help="must match the training run's --seed")
    ap.add_argument("--window-hop-step", type=int, default=None,
                    help="pass the same value used at feature loading time")
    ap.add_argument(
        "--split",
        choices=("val", "train", "all"),
        default="val",
        help="which slice of the reproduced split to evaluate on",
    )
    args = ap.parse_args()

    labels = [s.strip() for s in args.labels.split(",") if s.strip()]
    X, y = load_dataset(args.feat_root, labels, hop_step=args.window_hop_step)
    print(f"loaded X={X.shape} y={y.shape} counts={np.bincount(y).tolist()}")

    X_tr, y_tr, X_va, y_va = split_train_val(X, y, args.val_frac, args.seed)
    if args.split == "val":
        X_eval, y_eval = X_va, y_va
    elif args.split == "train":
        X_eval, y_eval = X_tr, y_tr
    else:
        X_eval, y_eval = X, y
    print(f"evaluating on '{args.split}' split: {X_eval.shape[0]} samples")

    y_pred = run_inference(args.tflite, X_eval)
    print_report(labels, y_eval, y_pred)
    return 0


if __name__ == "__main__":
    sys.exit(main())
