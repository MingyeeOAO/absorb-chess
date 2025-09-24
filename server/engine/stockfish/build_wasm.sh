#!/bin/bash
# Build script for Absorb Chess WASM in WSL/Linux

echo "🔧 Building Absorb Chess WASM Engine..."

# Check if Emscripten is available
if ! command -v emcc &> /dev/null; then
    echo "❌ Emscripten not found. Please install it first:"
    echo "   git clone https://github.com/emscripten-core/emsdk.git"
    echo "   cd emsdk"
    echo "   ./emsdk install latest"
    echo "   ./emsdk activate latest"
    echo "   source ./emsdk_env.sh"
    exit 1
fi

# Get current directory and navigate to stockfish directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "✅ Emscripten found, compiling..."

# Compile with Emscripten
emcc -O2 \
  -std=c++17 \
  -DNDEBUG \
  -DUSE_POPCNT \
  -DNO_PREFETCH \
  -I src \
  -s EXPORTED_FUNCTIONS="['_malloc','_free']" \
  -s EXPORTED_RUNTIME_METHODS="['ccall','cwrap']" \
  -s MODULARIZE=1 \
  -s EXPORT_NAME="AbsorbChessModule" \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s MAXIMUM_MEMORY=268435456 \
  -s STACK_SIZE=8388608 \
  -s DISABLE_EXCEPTION_CATCHING=0 \
  -s ASSERTIONS=0 \
  --bind \
  src/absorb_chess_wasm.cpp \
  src/absorb_tables.cpp \
  src/bitboard.cpp \
  src/endgame.cpp \
  src/evaluate.cpp \
  src/material.cpp \
  src/movegen.cpp \
  src/movepick.cpp \
  src/position.cpp \
  src/psqt.cpp \
  src/search.cpp \
  src/thread.cpp \
  src/tt.cpp \
  src/uci.cpp \
  src/ucioption.cpp \
  src/tune.cpp \
  src/syzygy/tbprobe.cpp \
  -o "../../frontend/engine/absorb_chess_engine.js"

if [ $? -eq 0 ]; then
    echo "✅ WASM build successful!"
    echo "📁 Output files:"
    echo "   ../../frontend/engine/absorb_chess_engine.js"
    echo "   ../../frontend/engine/absorb_chess_engine.wasm"
    echo ""
    echo "🎯 Next steps:"
    echo "   1. Update frontend/engine/engine.js to use AbsorbChessModule"
    echo "   2. Test the absorb chess functionality"
else
    echo "❌ WASM build failed!"
    echo "💡 Check that all source files exist and dependencies are met"
    exit 1
fi
movegen.cpp
movepick.cpp
pawns.cpp
position.cpp
psqt.cpp
search.cpp
thread.cpp
timeman.cpp
tt.cpp
tune.cpp
uci.cpp
ucioption.cpp
"

# Build command
echo "Building Absorb Chess Stockfish WASM..."

emcc $CXXFLAGS $WASM_FLAGS $BIND_FLAGS \
    -I. \
    $SOURCES \
    -o absorb_chess_stockfish.js

if [ $? -eq 0 ]; then
    echo "Build successful! Files generated:"
    echo "  - absorb_chess_stockfish.js"
    echo "  - absorb_chess_stockfish.wasm"
else
    echo "Build failed!"
    exit 1
fi