cmake_minimum_required(VERSION 3.25)

# Windows static runtime
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# MacOS unversal binary
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")

# MacOS minimum deployment target
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15")

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

# Embed the exported bundle.json (BinaryData::bundle_json) and the default
# profile catalogue (BinaryData::profiles_json) into the binary.
juce_add_binary_data(amp_morph_bundle SOURCES
    "${aaom_source}/assets/bundle.json"
    "${aaom_source}/assets/profiles.json")

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
        ${nam_core_sources}
    INCLUDE_DIRS
        ${aaom_source}
        ${aaom_source}/dsp
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
