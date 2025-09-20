#!/bin/bash

echo "Building ChessEngineV2 WebAssembly module..."

# Check if Emscripten is available
if ! command -v emcc &> /dev/null; then
    echo "Error: Emscripten (emcc) not found in PATH"
    echo "Please install Emscripten SDK and activate the environment"
    echo "Visit: https://emscripten.org/docs/getting_started/downloads.html"
    exit 1
fi

# Compile the V2 engine to WebAssembly
emcc \
    chess_engine_v2.cpp \
    chess_engine_v2_wasm.cpp \
    -o chess_engine_v2.js \
    -s WASM=1 \
    -s MODULARIZE=1 \
    -s EXPORT_NAME="ChessEngineV2Module" \
    -s EXPORTED_RUNTIME_METHODS="['cwrap', 'ccall']" \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s TOTAL_MEMORY=67108864 \
    -O3 \
    --bind \
    -std=c++17

if [ $? -eq 0 ]; then
    echo "Build successful! Generated files:"
    echo "- chess_engine_v2.js"
    echo "- chess_engine_v2.wasm"
else
    echo "Build failed!"
    exit 1
fi