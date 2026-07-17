#!/bin/bash

# Example:
# ./build.sh mfrtaa VST3 Release

# First arg: name of the target to be built 
target=${1}

# Second arg: Format of the target VST3/AU(MacOS only)/Standalone(default)
format=${2:-Standalone}

# Thrid arg: Build config. Release/Debug(default)
config=${3:-Debug}

# Fourth arg: Number of concurrent build jobs, 4 by default
jobs=${4:-4}

cmake --build build --target ${target}_${format} --config ${config} --parallel ${jobs}