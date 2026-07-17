# Download JUCE directly to build dir
include(FetchContent)
FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 1a7300e7cfb303af29c6559c686aeefbc92a44f0
)
FetchContent_MakeAvailable(juce)

# This function adds a JUCE plugin target to this project
# Arguments:
#   - TARGET: The name of your plugin target, this allows you to identify which of your plugins will be built.
#   - VERSION: Version on your plugin.
#   - PLUGIN_NAME: The name which will be displayed on the DAW.
#   - PROD_NAME: Internal product name, cannot contain whitespace.
#   - PROD_CODE: 4 letter code unique identifier to your plugin, at least 1 capitalized letter.
#   - SYNTH: TRUE if the plugin is a synth
#   - SOURCES: A list of all the source files of you plugin.
#   - INCLUDE_DIRS: A list of the include directories required by your sources.
#   - LINK_LIBS: A list of the libraries or target dependencies.
function(add_plugin target)
    # parse input args
    set(one_value_args TARGET VERSION PLUGIN_NAME PROD_NAME PROD_CODE SYNTH)
    set(multi_value_args SOURCES INCLUDE_DIRS LINK_LIBS)
    cmake_parse_arguments(AP "" "${one_value_args}" "${multi_value_args}" ${ARGN})

    # info and debug
    message(STATUS "Adding JUCE plugin target: ${target}")
    message(STATUS "  PLUGIN_NAME: ${AP_PLUGIN_NAME}")
    message(STATUS "  VERSION: ${AP_VERSION}")
    message(STATUS "  PROD_NAME: ${AP_PROD_NAME}")
    message(STATUS "  PROD_CODE: ${AP_PROD_CODE}")
    ## Too verbose, uncomment just if you need to debug...
    #message(STATUS "  SOURCES: ${AP_SOURCES}")
    #message(STATUS "  INCLUDE_DIRS: ${AP_INCLUDE_DIRS}")
    #message(STATUS "  LINK_LIBS: ${AP_LINK_LIBS}")

    # Add juce plugin target
    juce_add_plugin(${target}
        PRODUCT_NAME ${AP_PROD_NAME}
        VERSION ${AP_VERSION}
        MICROPHONE_PERMISSION_ENABLED TRUE
        COMPANY_COPYRIGHT ${company_copyright}
        COMPANY_NAME ${company_name}
        FORMATS VST3 AU Standalone
        PLUGIN_NAME ${AP_PLUGIN_NAME}
        PLUGIN_MANUFACTURER_CODE ${company_code}
        PLUGIN_CODE ${AP_PROD_CODE}
        BUNDLE_ID ${company_bundle_id}.${AP_PROD_NAME}
        HARDENED_RUNTIME_ENABLED FALSE
        IS_SYNTH ${AP_SYNTH}
        NEEDS_MIDI_INPUT ${AP_SYNTH}
        NEEDS_MIDI_OUTPUT FALSE
        COPY_PLUGIN_AFTER_BUILD TRUE
    )

    # Add parameter utils
    target_sources(${target}
        PRIVATE
            ${CMAKE_SOURCE_DIR}/mrta_utils/GUI/GenericParameterEditor.cpp
            ${CMAKE_SOURCE_DIR}/mrta_utils/GUI/ParameterComponents.cpp
            ${CMAKE_SOURCE_DIR}/mrta_utils/Parameter/ParameterFIFO.cpp
            ${CMAKE_SOURCE_DIR}/mrta_utils/Parameter/ParameterInfo.cpp
            ${CMAKE_SOURCE_DIR}/mrta_utils/Parameter/ParameterManager.cpp
            ${CMAKE_SOURCE_DIR}/mrta_utils/Processor/BaseProcessor.cpp
            ${AP_SOURCES}
    )

    target_include_directories(${target}
        PRIVATE
            ${CMAKE_SOURCE_DIR}/mrta_utils/GUI
            ${CMAKE_SOURCE_DIR}/mrta_utils/Parameter
            ${CMAKE_SOURCE_DIR}/mrta_utils/Processor
            ${AP_INCLUDE_DIRS}
    )

    # C++ 17
    target_compile_features(${target}
        PUBLIC
            cxx_std_17
    )

    target_compile_definitions(${target}
        PRIVATE
            JUCE_ASIO=1 JUCE_DIRECTSOUND=0
            JUCE_USE_FLAC=0 JUCE_USE_OGGVORBIS=0 JUCE_USE_WINDOWS_MEDIA_FORMAT=0
            JUCE_VST3_CAN_REPLACE_VST2=0
            JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
            JUCE_SILENCE_XCODE_15_LINKER_WARNING=1 JUCE_WIN_PER_MONITOR_DPI_AWARE=1
            WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES
    )

    if (LINUX)
        target_compile_definitions(${target} PRIVATE JUCE_JACK=1)
    endif()

    target_link_libraries(${target}
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
            ${AP_LINK_LIBS}
    )

    # Xcode 15 linker workaround
    # warning: If you are using Link Time Optimisation (LTO), the new linker introduced in Xcode 15 may produce a broken binary.
    # As a workaround, add either '-Wl,-weak_reference_mismatches,weak' or '-Wl,-ld_classic' to your linker flags.
    # Once you've selected a workaround, you can add JUCE_SILENCE_XCODE_15_LINKER_WARNING to your preprocessor definitions to silence this warning.
    if(APPLE)
        target_link_options(${target} PUBLIC -Wl,-ld_classic)
    endif()
endfunction(add_plugin)
