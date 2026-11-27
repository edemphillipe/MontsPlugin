#!/usr/bin/env python3
"""Independent check of AIDA-X capture reading.

Re-implements the Keras LSTM/GRU + Dense network directly from the AIDA-X .json weights
with numpy (no RTNeural), runs the same input the C++ test used, and compares with the
output Guitar Studio Pro produced.

usage: verify_aidax.py model.json aida_in.f32 aida_out.f32
"""
import json
import sys

import numpy as np


def sigmoid(x):
    return 1.0 / (1.0 + np.exp(-x))


def run(model, x):
    layers = model["layers"]
    rnn, dense = layers[0], layers[1]
    hidden = rnn["shape"][-1]
    in_gain = 10 ** (model.get("in_gain", 0.0) / 20) if isinstance(model.get("in_gain"), (int, float)) else 1.0
    out_gain = 10 ** (model.get("out_gain", 0.0) / 20) if isinstance(model.get("out_gain"), (int, float)) else 1.0
    skip = model.get("in_skip", 0) == 1

    W = np.array(rnn["weights"][0], dtype=np.float64)  # (in, k*H)
    U = np.array(rnn["weights"][1], dtype=np.float64)  # (H, k*H)
    b = np.array(rnn["weights"][2], dtype=np.float64)
    Wd = np.array(dense["weights"][0], dtype=np.float64)  # (H, 1)
    bd = np.array(dense["weights"][1], dtype=np.float64)

    h = np.zeros(hidden)
    c = np.zeros(hidden)
    y = np.zeros_like(x, dtype=np.float64)
    n_in = model["in_shape"][-1]

    for t, sample in enumerate(x):
        xin = np.full(n_in, 0.5)
        xin[0] = sample * in_gain
        if rnn["type"] == "lstm":
            z = xin @ W + h @ U + b
            i, f, g, o = np.split(z, 4)
            c = sigmoid(f) * c + sigmoid(i) * np.tanh(g)
            h = sigmoid(o) * np.tanh(c)
        elif rnn["type"] == "gru":
            bx = b[0] if b.ndim == 2 else b
            bh = b[1] if b.ndim == 2 else np.zeros_like(b)
            xz, xr, xh = np.split(xin @ W + bx, 3)
            hz, hr, hh = np.split(h @ U + bh, 3)
            zg = sigmoid(xz + hz)
            r = sigmoid(xr + hr)
            hc = np.tanh(xh + r * hh)
            h = zg * h + (1 - zg) * hc
        else:
            raise SystemExit("unsupported layer " + rnn["type"])
        out = (h @ Wd + bd)[0]
        y[t] = ((xin[0] + out) if skip else out) * out_gain
    return y


def main():
    model = json.load(open(sys.argv[1]))
    x = np.fromfile(sys.argv[2], dtype=np.float32)
    ours = np.fromfile(sys.argv[3], dtype=np.float32)
    ref = run(model, x.astype(np.float64))
    err = np.max(np.abs(ref - ours))
    snr = 10 * np.log10(np.sum(ref ** 2) / max(np.sum((ref - ours) ** 2), 1e-30))
    print(f"samples={len(x)}  max|numpy-ours|={err:.2e}  SNR={snr:.1f} dB")
    sys.exit(0 if err < 1e-3 else 1)


if __name__ == "__main__":
    main()
