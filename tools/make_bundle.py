#!/usr/bin/env python3
"""Generate a schema-valid *placeholder* aaom bundle.json for building/testing the
plugin before a real one is exported from the AllAmpsOnMe training repo
(`openamp emulate-export-bundle`).

The weights are random, so this produces noise, not a real amp — its only jobs
are (1) let the plugin link its embedded binary data and (2) let the standalone
self-test exercise the fold + NAM weight-order path end to end. The tensor layout,
shapes and field names match projects/AAOM/dsp/MorphModel.cpp exactly; swap this
file for the exporter's output once available (keep the field names in sync).

Usage:  python3 tools/make_bundle.py [out.json] [--channels 8] [--embed 128] [--seed 0]
"""
import argparse
import base64
import hashlib
import json
import random
import struct

# A2 layer schedule: three runs — 14x k6, 2x k15, 7x k6 (23 layers total).
KERNEL_SIZES = [6] * 14 + [15] * 2 + [6] * 7
_DIL_RUN = [1, 3, 7, 17, 41, 101, 239]
DILATIONS = _DIL_RUN + _DIL_RUN + [1, 13] + _DIL_RUN
assert len(KERNEL_SIZES) == len(DILATIONS) == 23
HEAD_KERNEL = 16
HEAD_SCALE = 0.01


def tensor(flat, shape):
    n = 1
    for s in shape:
        n *= s
    assert len(flat) == n, f"shape {shape} wants {n} floats, got {len(flat)}"
    blob = struct.pack("<%df" % len(flat), *flat)
    return {"shape": list(shape), "dtype": "float32", "data": base64.b64encode(blob).decode("ascii")}


def rnd(rng, n, scale=0.1):
    return [rng.gauss(0.0, scale) for _ in range(n)]


def make_bundle(channels, embed, seed):
    rng = random.Random(seed)
    C, E, K = channels, embed, HEAD_KERNEL

    layers = []
    for k in KERNEL_SIZES:
        layers.append({
            "conv_w": tensor(rnd(rng, C * C * k), [C, C, k]),
            "conv_b": tensor(rnd(rng, C), [C]),
            "mixin_w": tensor(rnd(rng, C), [C, 1, 1]),
            "film_w": tensor(rnd(rng, 2 * C * E, scale=0.02), [2 * C, E]),
            "film_b": tensor([1.0] * C + [0.0] * C, [2 * C]),  # gamma~1, beta~0 at init
            "x1_w": tensor(rnd(rng, C * C), [C, C, 1]),
            "x1_b": tensor(rnd(rng, C), [C]),
        })

    weights = {
        "rechannel_w": tensor(rnd(rng, C), [C, 1, 1]),
        "layers": layers,
        "head_w": tensor(rnd(rng, C * K), [1, C, K]),
        "head_b": tensor(rnd(rng, 1), [1]),
    }

    profiles = [
        {"name": "Table mean", "embedding": [0.0] * E},
        {"name": "Profile A", "embedding": rnd(rng, E, scale=1.0)},
        {"name": "Profile B", "embedding": rnd(rng, E, scale=1.0)},
    ]

    fake_ckpt = f"placeholder-{channels}-{embed}-{seed}".encode()
    sha8 = hashlib.sha256(fake_ckpt).hexdigest()[:8]

    return {
        "aaom_bundle": 1,
        "run": {"name": "placeholder", "sha8": sha8, "manifest_sha256": "0" * 64,
                "epoch": 0, "val_esr": -1.0, "n_devices": 0},
        "sample_rate": 48000,
        "arch": {
            "type": "film_wavenet_a2",
            "channels": C,
            "embedding_dim": E,
            "kernel_sizes": KERNEL_SIZES,
            "dilations": DILATIONS,
            "head_kernel": K,
            "head_scale": HEAD_SCALE,
            "receptive_field": sum((k - 1) * d for k, d in zip(KERNEL_SIZES, DILATIONS)) + 1,
        },
        "weights": weights,
        "profiles": profiles,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out", nargs="?", default="projects/AAOM/assets/bundle.json")
    ap.add_argument("--channels", type=int, default=8)
    ap.add_argument("--embed", type=int, default=128)
    ap.add_argument("--seed", type=int, default=0)
    args = ap.parse_args()

    bundle = make_bundle(args.channels, args.embed, args.seed)
    import os
    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    with open(args.out, "w") as f:
        json.dump(bundle, f)
    print(f"wrote {args.out}  (C={args.channels}, E={args.embed}, 23 layers, placeholder weights)")


if __name__ == "__main__":
    main()
