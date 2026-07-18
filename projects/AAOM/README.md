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
```

Source layout:

| file | role |
|---|---|
| `dsp/MorphModel.{h,cpp}` | bundle parse + FiLM fold + NAM WaveNet build / `set_weights_` |
| `dsp/MorphEngine.{h,cpp}` | 4 corners, bilinear morph, embedding smoothing |
| `dsp/CornerFifo.h` | lock-free SPSC corner-update queue (message → audio) |
| `dsp/Resampler.h` | JUCE-free cubic host⇄48 kHz resampling |
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

### Pasteable profiles

Extra amps for the corners come from profile JSON exported separately:

```
openamp emulate-export-profile <run_dir> --device N | --mean | --pair NAME
```

Copy the JSON to the clipboard and click **Paste** on a corner. The plugin
hard-rejects an `embedding_dim` mismatch and warns (with a "Load anyway"
override) if the profile's `run` sha8 differs from the loaded model. Corner
profiles and the dot position persist in the plugin state.

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
consumes, deterministic + embedding-sensitive folding, finite render, the morph
`set_weights_` path, the lock-free corner queue, and the resampler round-trip.

## Status

- **M2 (static playback)** and a **first cut of M3 (morph engine)** are done and
  compile into a loadable VST3.
- **M4 (XY-pad UI, corner paste/clear, state)** and **M5 (gains, resampling, CI,
  this README)** are implemented.
- **Remaining:** numerically A/B a *real* exported bundle against the training
  repo's notebook renders, and confirm click-free drags / CPU headroom in a DAW.
