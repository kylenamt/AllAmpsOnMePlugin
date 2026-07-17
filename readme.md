# Additive Synthesizer

A polyphonic additive synthesizer VST3/AU plugin built with JUCE and CMake. Synthesizes audio by summing up to 512 sine-wave partials per voice with full spectral, envelope, and filter control.

## Credits

The additive synthesizer (`projects/AdditiveSynthesizer/` and `projects/DSP/AddSynth/`) was implemented by me as part of the **Audio Programming Project by Aalto University** course. The supporting framework (`mrta_utils/`, `cmake/`, `configure.sh`, `build.sh`) was provided by the course.

## Features

- **8-voice polyphony** with per-voice additive synthesis
- **Up to 512 partials** per voice (configurable at runtime)
- **Spectral shaping** — manual partial amplitudes, classic waveform presets (Sine, Saw, Square, Triangle), and sampled harmonic tables (Flute, Trumpet C, Violin)
- **Inharmonicity, Offset, Stretch, and Odd/Even Balance** controls for spectral morphing
- **Amplitude ADSR envelope**
- **State Variable Filter** with its own ADSR envelope and LFO
- Formats: **VST3 · AU · Standalone**
- Platform: **macOS** (universal arm64/x86_64), Windows, Linux

## Parameters

| Section | Parameter | Range |
|---|---|---|
| Partials | Num Partials | 1 – 512 |
| | Shape | Sine / Saw / Square / Triangle |
| | Brightness | 0 – 1 |
| Spectral | Manual Level | 0 – 1 |
| | Preset Level | 0 – 1 |
| | Table Level | 0 – 1 |
| | Table Select | Flute / Trumpet C / Violin |
| | Inharmonicity | 0 – 1 |
| | Offset | -1 – 1 |
| | Stretch | 0.5 – 1.5 |
| | Odd/Even Balance | -1 – 1 |
| Amp Env | Attack / Decay / Release | 1 – 2000 ms |
| | Sustain | 0 – 1 |
| Filter | Cutoff | 20 – 20000 Hz |
| | Resonance | 0.1 – 5.0 |
| | Env Amount | 0 – 5000 Hz |
| Filter Env | Attack / Decay / Release | 1 – 2000 ms |
| | Sustain | 0 – 1 |
| Filter LFO | Rate | 0 – 20 Hz |
| | Depth | 0 – 2000 Hz |
| Output | Master Gain | -60 – +24 dB |

## Downloads

Prebuilt binaries for **macOS** and **Windows** are attached to each
[GitHub Release](https://github.com/kylenamt/AdditiveSynthesizer/releases). Grab the zip
for your OS, unzip it, and copy the plugin into your plugin folder:

- **macOS** — VST3 → `~/Library/Audio/Plug-Ins/VST3/` · AU → `~/Library/Audio/Components/`
- **Windows** — VST3 → `C:\Program Files\Common Files\VST3\`

The Standalone app is in the zip too — just run it directly, no install needed.

> **Blocked on first launch?** These builds are unsigned. On macOS, right-click the
> plugin/app → **Open** (or run `xattr -dr com.apple.quarantine <file>`). On Windows, click
> **More info → Run anyway** on the SmartScreen prompt. This is expected for unsigned builds.

Don't see a release for your platform, or want the very latest code? Build it yourself below.

## Getting Started

New to building software? No problem. The whole process is: **install a
couple of tools → download the code → build it → play it.**

You only need a terminal (the **Terminal** app on macOS/Linux, **PowerShell** on Windows).
Copy and paste one command at a time.

### Step 1 — Install the build tools

You need two things: **CMake** (the build system) and a **C++ compiler**. JUCE, the audio
framework, downloads itself automatically the first time you build — you don't install it.

<details open>
<summary><b>macOS</b></summary>

```sh
# Install Apple's compiler (Command Line Tools). A popup will ask you to confirm.
xcode-select --install

# Install Homebrew (the macOS package manager) if you don't have it, then CMake.
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
brew install cmake git
```
</details>

<details>
<summary><b>Windows</b></summary>

Install **Visual Studio Community** (free) and during setup tick the
**"Desktop development with C++"** workload — this gives you the MSVC compiler.
Then install [CMake](https://cmake.org/download/) and [Git](https://git-scm.com/download/win),
accepting the option to "Add to PATH" in each installer.
</details>

<details>
<summary><b>Linux</b></summary>

You'll need a compiler, CMake, Git, and several audio/GUI development libraries.
See [readme-linux.md](readme-linux.md) for the exact `apt` / `dnf` package list.
</details>

### Step 2 — Download the code

```sh
git clone https://github.com/kylenamt/AdditiveSynthesizer.git
cd AdditiveSynthesizer
```

Every command from here on is run **inside this folder**.

### Step 3 — Set up the build (run once)

```sh
./configure.sh
```

This creates a `build/` folder and downloads JUCE. **The first run takes several
minutes** because JUCE is a large download — that's normal, let it finish. You only
ever need to do this once.

### Step 4 — Build the synth

This builds **all formats in one go** — VST3, AU, and Standalone:

```sh
./build.sh additive_synth All Release
```

**VST3 is the main format** (it works in nearly every DAW). When the build finishes you'll have:

- **VST3 plugin** — copied automatically into your system plugin folder, ready for any DAW
- **AU plugin** (macOS only) — copied into your AU folder, for GarageBand / Logic
- **Standalone app** — `build/additive_synth_artefacts/Standalone/AdditiveSynthesizer.app`

> Only want one format? Pass it instead of `All`, e.g. `./build.sh additive_synth VST3 Release`.
> On Windows/Linux, drop the `./` and run `build.sh ...` the same way (or `bash build.sh ...`).

### Step 5 — Play it 🎹

- **In a DAW (recommended).** The **VST3** you just built is already copied into place.
  Open your DAW (Reaper, Ableton, FL Studio, Cubase…), add **Additive Synthesizer** as an
  instrument on a track, and play with the piano roll or on-screen keyboard. On macOS the
  **AU** version works in GarageBand / Logic too — GarageBand is free and great for a
  first test.

- **Standalone app (no DAW needed).** Double-click `AdditiveSynthesizer.app` (macOS) — the
  synth window opens. Open its **Options → Audio/MIDI Settings**, choose your
  speakers/headphones as the output and your MIDI keyboard as the input, then play. *(The
  standalone app needs a MIDI keyboard to make sound. No keyboard? Use the DAW option
  above, which gives you an on-screen piano.)*

That's it — you're running the synth from source! 🎉

---

### Build options reference

`build.sh` takes `<target> <format> <config>`:

| Argument | Values | Default |
|---|---|---|
| target | `additive_synth` | — |
| format | `All` · `VST3` · `AU` (macOS) · `Standalone` | `Standalone` |
| config | `Release` · `Debug` | `Debug` |

`All` builds every format at once; otherwise pick a single one. Use `Release` for normal
playing (faster), `Debug` for development.

Plugin builds are copied automatically to the system plugin folders:
- macOS VST3 → `~/Library/Audio/Plug-Ins/VST3/`
- macOS AU → `~/Library/Audio/Components/`

## Debugging in VSCode

Open the project in VSCode with the [CodeLLDB](https://marketplace.visualstudio.com/items?itemName=vadimcn.vscode-lldb) extension installed. Press `F5` to build the VST3 in Debug mode and launch REAPER with the debugger attached.

## Project Structure

```
projects/
  AdditiveSynthesizer/   # JUCE plugin (Processor + Editor)
  DSP/
    AddSynth/            # Core additive engine (voices, partial bank, wavetable)
    EnvelopeGenerator    # ADSR
    StateVariableFilter  # SVF
mrta_utils/              # Shared framework (parameters, GUI components)
cmake/                   # add_plugin() helper
```
