cmake_minimum_required(VERSION 3.25)

# Windows static runtime
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# MacOS unversal binary
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")

# MacOS minimum deployment target
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15")

# Main project
project(MRTA VERSION 0.1.0 LANGUAGES C CXX)

# Get 'add_plugin' function and add JUCE to project
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(add_plugin)


# General variables
# You should change this to something more interesting
set(company_name "Theo's Additive Synthesizer")
set(company_copyright "TASP")
set(company_code "TASP")
set(company_bundle_id "com.ModernRealTimeAudio")

## Plugin projects

# DSP directory helper variable
set(dsp_source ${CMAKE_CURRENT_SOURCE_DIR}/projects/DSP)

# GUI directory helper variable
set(gui_source ${CMAKE_CURRENT_SOURCE_DIR}/projects/GUI)

# MRTA framework helper variable
set(mrta_source ${CMAKE_CURRENT_SOURCE_DIR}/mrta_utils)

# Example project
set(mfrtaa_source ${CMAKE_CURRENT_SOURCE_DIR}/projects/MyFirstRealTimeAudioApp)

# additive synthesizer project
set(additive_synth_source ${CMAKE_CURRENT_SOURCE_DIR}/projects/AdditiveSynthesizer)

add_plugin(additive_synth
    VERSION 0.1.0
    PLUGIN_NAME "Additive Synthesizer"
    PROD_NAME AdditiveSynthesizer
    PROD_CODE AdSy
    SYNTH TRUE
    SOURCES
        ${additive_synth_source}/AdditiveSynthesizerEditor.cpp
        ${additive_synth_source}/AdditiveSynthesizerProcessor.cpp
        ${dsp_source}/AddSynth/AddSynth.cpp
        ${dsp_source}/AddSynth/PartialBank.cpp
        ${dsp_source}/AddSynth/SpectralCoef.cpp
    ${dsp_source}/StateVariableFilter.cpp
    ${dsp_source}/EnvelopeGenerator.cpp
    INCLUDE_DIRS
        ${dsp_source}
        ${additive_synth_source})