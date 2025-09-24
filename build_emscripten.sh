#!/usr/bin/env bash
set -euo pipefail

# Build the WebAssembly target using Emscripten.
# Produces carrotcode.js/carrotcode.wasm next to the copied HTML+JS shell in ./emscripten.
# Requires emcc/em++ available on PATH.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT_DIR}/emscripten"
SOURCE_DIR="${ROOT_DIR}/code"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# Copy the host HTML/JS so the output folder is self-contained.
cp "${ROOT_DIR}/favicon.ico" .
cp "${SOURCE_DIR}/emscripten.html" ./index.html
cp "${SOURCE_DIR}/emscripten.js" .

EMCC_FLAGS=(
    -std=c++17
    -O3
    -sALLOW_MEMORY_GROWTH=1
    -sASSERTIONS=0
    -sERROR_ON_UNDEFINED_SYMBOLS=0
    -sENVIRONMENT=web
    -sEXPORTED_FUNCTIONS="['_main','_malloc','_free','_CarrotPlatformOnCopyFinished','_CarrotPlatformOnPasteResult','_CarrotPlatformOnPasteCanceled','_CarrotPlatformOnYesNoResult','_CarrotPlatformOnSaveResult','_CarrotPlatformOnSaveCanceled','_CarrotPlatformOnOpenFileResult','_CarrotPlatformOnOpenFileCanceled','_CarrotPlatformOnWindowResized','_CarrotPlatformSetDevicePixelRatio','_CarrotPlatformTriggerFileOpen']"
    -sEXPORTED_RUNTIME_METHODS="['ccall','cwrap']"
    -sSTRICT=1
    -sDISABLE_EXCEPTION_CATCHING=2
)

em++ \
    "${SOURCE_DIR}/emscripten.cpp" \
    "${SOURCE_DIR}/document.cpp" \
    "${SOURCE_DIR}/syntax.cpp" \
    "${SOURCE_DIR}/strings.cpp" \
    "${SOURCE_DIR}/software_renderer.cpp" \
    "${SOURCE_DIR}/imgui.cpp" \
    "${SOURCE_DIR}/view.cpp" \
    "${SOURCE_DIR}/application.cpp" \
    -o carrotcode.js \
    "${EMCC_FLAGS[@]}"

echo "Built WebAssembly target in ${BUILD_DIR}".
