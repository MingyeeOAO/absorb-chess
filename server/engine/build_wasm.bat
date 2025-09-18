@echo off
REM Build script for compiling C++ chess engine to WebAssembly on Windows

cd /d "%~dp0"

echo Building Chess Engine WebAssembly Module...

REM Check if emscripten is available
where emcc >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo "Error: Emscripten (emcc) not found!"
    echo "Please install Emscripten SDK first:"
    echo   "https://emscripten.org/docs/getting_started/downloads.html"
    exit /b 1
)


REM Create output directories
if not exist "..\..\frontend\web_assets" mkdir "..\..\frontend\web_assets"
if not exist "..\..\frontend\engine" mkdir "..\..\frontend\engine"

REM Compile to WebAssembly
emcc chess_engine_wasm.cpp chess_engine.cpp ^
    -o ../../frontend/engine/chess_engine.js ^
    -s WASM=1 ^
    -s EXPORTED_RUNTIME_METHODS="[\"ccall\", \"cwrap\"]" ^
    -s ALLOW_MEMORY_GROWTH=1 ^
    -s MODULARIZE=1 ^
    -s EXPORT_NAME="ChessEngineModule" ^
    -O3 ^
    --bind ^
    -std=c++17

REM Move the WASM file to the engine directory
if exist "..\..\frontend\web_assets\chess_engine.wasm" (
    move "..\..\frontend\web_assets\chess_engine.wasm" "..\..\frontend\engine\chess_engine.wasm"
)

if %ERRORLEVEL% EQU 0 (
    echo  Successfully built WebAssembly module!
    echo  Output files:
    echo    - chess_engine.js (in frontend/engine/)
    echo    - chess_engine.wasm (in frontend/engine/)
    echo  Location: frontend/
) else (
    echo  Build failed!
    exit /b 1
)