#!/bin/bash
# Build script for compiling C++ chess engine to WebAssembly

# Make sure we're in the engine directory
cd "$(dirname "$0")"

echo "🔧 Building Chess Engine WebAssembly Module..."

# Check if emscripten is available
if ! command -v emcc &> /dev/null; then
    echo "❌ Error: Emscripten (emcc) not found!"
    echo "Please install Emscripten SDK first:"
    echo "  https://emscripten.org/docs/getting_started/downloads.html"
    exit 1
fi

# Create output directory for web assets
mkdir -p ../../web_assets

# Compile to WebAssembly
emcc chess_engine_wasm.cpp chess_engine.cpp \
    -o ../../web_assets/chess_engine.js \
    -s WASM=1 \
    -s EXPORTED_RUNTIME_METHODS='["ccall", "cwrap"]' \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s MODULARIZE=1 \
    -s EXPORT_NAME="ChessEngineModule" \
    -O3 \
    --bind \
    -std=c++17

if [ $? -eq 0 ]; then
    echo "✅ Successfully built WebAssembly module!"
    echo "📁 Output files:"
    echo "   - chess_engine.js"
    echo "   - chess_engine.wasm"
    echo "📍 Location: web_assets/"
else
    echo "❌ Build failed!"
    exit 1
fi