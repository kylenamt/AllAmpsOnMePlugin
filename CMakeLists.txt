cmake_minimum_required(VERSION 3.25)

# Windows static runtime
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# MacOS: Apple Silicon only. A universal binary carries a full second copy of the
# code and the embedded model JSON, which roughly doubled the macOS download.
set(CMAKE_OSX_ARCHITECTURES "arm64")

# MacOS minimum deployment target. arm64 macOS starts at 11.0 (Big Sur), so the
# old 10.15 floor no longer describes anything this can run on.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0")

# Main project
project(AllAmpsOnMePlugin VERSION 0.1.0 LANGUAGES C CXX)

# Get 'add_plugin' function and add JUCE to project
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(add_plugin)

# General plugin identity
set(company_name "AllAmpsOnMe")
set(company_copyright "AllAmpsOnMe")
set(company_code "Aaom")
set(company_bundle_id "com.allampsonme")

# ---------------------------------------------------------------------------
# NeuralAmpModelerCore (Eigen, MIT). Populated only — we compile its sources
# directly into the plugin target (it exposes no library target) and route
# A2-shaped models through the generic WaveNet path (A2-fast OFF) so the live
# model can be re-weighted for morphing via WaveNet::set_weights_().
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(NeuralAmpModelerCore
    GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
    GIT_TAG 3cde95c354d5ba6da01316cad90b05cfc4855053
    GIT_SUBMODULES "Dependencies/eigen" # pull Eigen only (not AudioDSPTools)
    GIT_SHALLOW FALSE
)
FetchContent_GetProperties(NeuralAmpModelerCore)
if(NOT neuralampmodelercore_POPULATED)
    FetchContent_Populate(NeuralAmpModelerCore)
endif()
set(nam_dir "${neuralampmodelercore_SOURCE_DIR}")

file(GLOB nam_core_sources
    "${nam_dir}/NAM/*.cpp"
    "${nam_dir}/NAM/wavenet/*.cpp")
# Exclude the A2 fast path; we use the generic WaveNet so weights can be hot-swapped.
list(FILTER nam_core_sources EXCLUDE REGEX "a2_fast\\.cpp$")

set(nam_include_dirs
    "${nam_dir}"
    "${nam_dir}/Dependencies/nlohmann"
    "${nam_dir}/Dependencies/eigen")

# ---------------------------------------------------------------------------
# amp_morph — the AllAmpsOnMe embedding-morph amp plugin
# ---------------------------------------------------------------------------
set(aaom_source "${CMAKE_CURRENT_SOURCE_DIR}/projects/AAOM")
set(dsp_source "${CMAKE_CURRENT_SOURCE_DIR}/projects/DSP")

# Embed every built-in model package into the binary. Each model lives in its
# own assets/<run>/ folder holding an exported bundle plus its profile
# catalogue; the two are run-locked to each other. The BinaryData symbols are
# derived from the file names and are wired up in AAOMProcessor's built-in
# table, which also decides which one is the default.
juce_add_binary_data(amp_morph_bundle SOURCES
    "${aaom_source}/assets/nam_a2_256/nam_a2_256_bundle.json"
    "${aaom_source}/assets/nam_a2_256/nam_a2_256_profiles.json"
    "${aaom_source}/assets/wavenet_a2/bundle.json"
    "${aaom_source}/assets/wavenet_a2/profiles.json")

add_plugin(amp_morph
    VERSION 0.1.0
    PLUGIN_NAME "AllAmpsOnMe Morph"
    PROD_NAME AllAmpsOnMeMorph
    PROD_CODE Aaom
    SYNTH FALSE
    SOURCES
        ${aaom_source}/AAOMProcessor.cpp
        ${aaom_source}/AAOMEditor.cpp
        ${aaom_source}/gui/MorphPad.cpp
        ${aaom_source}/gui/AmpSlotComponent.cpp
        ${aaom_source}/gui/ProfileLibrary.cpp
        ${aaom_source}/gui/LcdReadout.cpp
        ${aaom_source}/gui/AAOMLookAndFeel.cpp
        ${aaom_source}/dsp/MorphModel.cpp
        ${aaom_source}/dsp/MorphEngine.cpp
        ${dsp_source}/Biquad.cpp
        ${dsp_source}/ParametricEQ.cpp
        ${nam_core_sources}
    INCLUDE_DIRS
        ${aaom_source}
        ${aaom_source}/dsp
        ${dsp_source}
        ${nam_include_dirs}
    LINK_LIBS
        amp_morph_bundle)

# NAM core needs C++20 and float I/O samples (matching JUCE's float buffers).
target_compile_features(amp_morph PUBLIC cxx_std_20)
target_compile_definitions(amp_morph PRIVATE NAM_SAMPLE_FLOAT=1)

# Optional dev iteration: load a bundle from disk instead of the embedded one.
#   cmake -B build -DAAOM_DEV_BUNDLE_PATH=/abs/path/to/bundle.json
if(DEFINED AAOM_DEV_BUNDLE_PATH)
    target_compile_definitions(amp_morph PRIVATE AAOM_DEV_BUNDLE_PATH="${AAOM_DEV_BUNDLE_PATH}")
endif()
