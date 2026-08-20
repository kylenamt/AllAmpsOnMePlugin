# AllAmpsOnMe Morph (`amp_morph`)

A VST3/AU/Standalone amp plugin that runs the trained one-to-many FiLM-WaveNet
(NAM **A2** topology) and lets you **morph between amp/pedal captures in real
time** on a 2D XY pad. Paste embedding *profiles* into up to four corners, drag
the dot, and the conditioning embedding is bilinearly blended and folded into the
network weights every block — audibly continuous while dragging.

See [`.claude/PLAN.md`](../../.claude/PLAN.md) for the full design/spec.

## How it works

For a **fixed** embedding `e`, the per-layer FiLM (`gamma, beta = film(e)`) folds
exactly into plain A2 conv/mixin weights, yielding a bit-for-bit **stock NAM A2
WaveNet**. So the DSP inner loop is unmodified
[NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore); morphing
just re-derives ~12k folded floats and hot-swaps them via `WaveNet::set_weights_`.

```
UI (XY pad, corner profiles)          [message thread]
        │  lock-free CornerFifo + automatable MorphX/MorphY params
        ▼
MorphEngine  — bilinear blend → one-pole embedding smoothing        [audio thread]
        │
MorphModel   — fold FiLM into A2 weights → NAM WaveNet (generic path)
        │
Resampler48k — host SR ⇄ 48 kHz around the model
        │
ParametricEqualizer — Bass/Mid/Treble/Presence tone stack, at host SR
        │
FftConvolver — cabinet IR, partitioned FFT convolution, at host SR
```

Source layout:

| file | role |
|---|---|
| `dsp/MorphModel.{h,cpp}` | bundle parse + FiLM/delta fold + NAM WaveNet build / `set_weights_` |
| `dsp/MorphEngine.{h,cpp}` | 4 corners, bilinear morph over cached folded corners, weight smoothing |
| `dsp/CornerFifo.h` | lock-free SPSC corner-update queue (message → audio) |
| `dsp/Resampler.h` | JUCE-free cubic host⇄48 kHz resampling |
| `dsp/FftConvolver.{h,cpp}` | radix-2 FFT + partitioned convolution for the cab IR |
| `AAOMProcessor.{h,cpp}` | JUCE processor: params, gains, state, corner logic |
| `AAOMEditor.{h,cpp}`, `gui/` | XY pad + corner slots UI |
| `assets/bundle.json` | the embedded model (see below) |

The `dsp/` engine is JUCE-free and covered by `tools/selftest.cpp`.

## Providing a model (`bundle.json`)

The plugin embeds one `bundle.json` produced by the AllAmpsOnMe training repo:

```
openamp emulate-export-bundle <run_dir>      # → <run>/export/bundle.json
```

Copy it to `projects/AAOM/assets/bundle.json` and rebuild — it is baked into the
binary via `juce_add_binary_data`. It carries **both** the shared base network
and the embedding `profiles` (the "Table mean" profile is always present).

> `assets/bundle.json` currently holds the real `wavenet_a2` E=128 export
> (run sha8 `0eb7e6b1`). `tools/make_bundle.py` can regenerate a random
> *placeholder* of the same shape if you need to build without a real model.

**Dev iteration** without rebuilding — load a bundle from disk:

```
cmake -B build -DAAOM_DEV_BUNDLE_PATH=/abs/path/to/bundle.json
```

### Conditioning architectures

`arch.type` selects how the embedding reaches the weights. Both fold to the exact
same NAM stream, so the audio path and its per-sample cost are identical; they
differ only in what `MorphModel::foldEmbedding` reads.

**`film_wavenet_a2`** — per-layer `gamma`/`beta` scale the layer's *output*, folded
in with `W *= gamma`, `b = gamma*b + beta`, `mixin *= gamma`. Per layer:

| key | shape | notes |
|---|---|---|
| `film_w` | `[2C, E]` | rows `0..C-1` = gamma, rows `C..2C-1` = beta |
| `film_b` | `[2C]` | same row split |

**`delta_wavenet_a2`** — the embedding writes a low-rank residual straight onto the
layer's weights, so folding is an addition rather than an identity. Needs
`arch.delta_rank` (`R`) at the top level, and per layer:

| key | shape | notes |
|---|---|---|
| `delta_coeff_w` | `[R, E]` | row-major; `coeff = delta_coeff_w · e + delta_coeff_b` |
| `delta_coeff_b` | `[R]` | |
| `delta_basis` | `[R, C*C*k + 2C]` | row-major; **already** `scale * normalize(basis)` |

`flat = coeff · delta_basis` splits as `(C*C*k, C, C)` into `dW`, `db`, `dm`, added
onto `conv_w`, `conv_b`, `mixin_w`. The plugin never normalises or rescales the
basis — the exporter must ship it premultiplied, matching `DeltaWeightGen.
effective_basis()`. `layer1x1`, `rechannel_w` and the head are unconditioned in
both archs.

Anything else (`tabledelta_wavenet_a2`, `mlpfilm_wavenet_a2`, …) is rejected at
parse time rather than reinterpreted. A bundle with no `arch.type` is read as FiLM.

> **Why the fold is cheap either way.** The folded stream is an *affine* function
> of `e` in both archs, so `MorphEngine` folds each corner once and bilinearly
> blends the four cached streams per block — exactly equal to folding the blended
> embedding (the selftest checks this renders bit-identical audio). A drag costs a
> 4-way weighted sum of ~12k floats regardless of `E`, `R`, or architecture; a full
> fold happens only when a corner is reassigned, one per block at most.

### Pasteable profiles

Extra amps for the corners come from profile JSON exported separately:

```
openamp emulate-export-profile <run_dir> --device N | --mean | --pair NAME
```

Copy the JSON to the clipboard and click **Paste** on a corner. The plugin
hard-rejects an `embedding_dim` mismatch and warns (with a "Load anyway"
override) if the profile's `run` sha8 differs from the loaded model. Corner
profiles and the dot position persist in the plugin state.

## Cabinet IR

The **CAB** strip under the morph pad loads an impulse response (WAV or AIFF)
and convolves it onto the signal after the tone stack, i.e. `model → tone → cab`
— the order a real rig runs in, and the one the NAM plugin uses. The switch
beside the chip is the automatable `CabOn` parameter; the chip's tooltip reports
what was loaded and how it ended up partitioned.

On load the IR is conditioned once, on the message thread, with audio suspended
across the swap (same as a model swap):

- **channel 0 only** — IR packs often hold several mic positions in one file,
  and summing those comb-filters them
- **resampled to the host rate** (cubic Lagrange), and re-conditioned from the
  original file samples whenever the rate changes, so a session opened at 96 kHz
  convolves the same cabinet it was saved with
- **trailing noise floor trimmed** (below −80 dB of the peak) and capped at
  2 seconds — past that a cab IR is only costing partitions
- **normalised to unit energy**, so swapping cabs does not swing the level

`FftConvolver` splits the IR into a direct-form *head* (the first `P` taps) and
an FFT *tail* (`h[P…]`, uniformly partitioned, overlap-save over a frequency
delay line). The tail only depends on input from at least `P` samples ago, so
its forward FFT, spectral multiply-accumulates and inverse FFT always run a full
block ahead of the samples that need them. The result is **no added latency**:
`setLatencySamples` still reports the resampler's delay alone, and loading or
dropping a cab never makes the host re-sync. `P` grows with the IR length (128 →
1024) to keep the partition count bounded.

Only the IR's path is stored in the session — IR files are large and shared
between projects. A file that has moved leaves the cab empty and says so on the
chip tooltip rather than failing the restore.

## Build

Prerequisites: CMake ≥ 3.25 and a C++20 compiler. On Linux also install the deps
in [`readme-linux.md`](../../readme-linux.md). First configure pulls JUCE and
NeuralAmpModelerCore via FetchContent.

```
cmake -S . -B build                          # configure (clones JUCE + NAM)
cmake --build build --target amp_morph_VST3   # or amp_morph_Standalone / amp_morph_AU (macOS)
```

The model always runs at 48 kHz; other host rates are handled by `Resampler48k`.

## Self-test (no JUCE, no DAW)

```
./tools/run_selftest.sh
```

Verifies bundle parse, that the folded weight stream is exactly the length NAM
consumes, deterministic + embedding-sensitive folding, that the fold is affine in
the embedding and that the engine's corner-blend therefore renders bit-identical
audio to folding a blended embedding, the delta arch's residual against a
closed-form miniature (plus its rejection paths), finite render, the morph
`set_weights_` path, the lock-free corner queue, the resampler round-trip, and
that the cab-IR convolution matches a direct sum sample-for-sample (across
partition boundaries, ragged block sizes and in-place processing) while staying
sample-aligned with its input.

## Status

- **M2 (static playback)** and a **first cut of M3 (morph engine)** are done and
  compile into a loadable VST3.
- **M4 (XY-pad UI, corner paste/clear, state)** and **M5 (gains, resampling, CI,
  this README)** are implemented.
- **Remaining:** numerically A/B a *real* exported bundle against the training
  repo's notebook renders, and confirm click-free drags / CPU headroom in a DAW.
