#!/bin/bash

# This command should be run to create the build folder.
# After that, just run the build.sh script specifying
# the target, build config (Debug/Release) and format (VST3/AU/Standalone).

cmake -S . -B build