#!/usr/bin/env bash
# set -euo pipefail

# Build script for a distribution/release configuration
# Produces a stripped binary with optimizations enabled.

OUTPUT="carrotcode"
rm -f "./${OUTPUT}"

g++ -std=c++17 -O3 -DNDEBUG -s \
    -o "${OUTPUT}" \
    code/linux.cpp \
    code/document.cpp \
    code/syntax.cpp \
    code/strings.cpp \
    code/software_renderer.cpp \
    code/imgui.cpp \
    code/view.cpp \
    code/application.cpp \
    -lX11

printf 'Built %s\n' "${OUTPUT}"
