@echo off
echo Building ChessEngineV2 WebAssembly module...


REM Compile the V2 engine to WebAssembly
emcc ^
    chess_engine_v2.cpp ^
    chess_engine_v2_wasm.cpp ^
    -o ../../frontend/engine/chess_engine_v2.js ^
    -s WASM=1 ^
    -s MODULARIZE=1 ^
    -s EXPORT_NAME="ChessEngineV2Module" ^
    -s EXPORTED_RUNTIME_METHODS="['cwrap', 'ccall']" ^
    -s ALLOW_MEMORY_GROWTH=1 ^
    -s TOTAL_MEMORY=67108864 ^
    -O3 ^
    --bind ^
    -std=c++17

if %ERRORLEVEL% equ 0 (
    echo Build successful! Generated files:
    echo - chess_engine_v2.js
    echo - chess_engine_v2.wasm
) else (
    echo Build failed!
    pause
)