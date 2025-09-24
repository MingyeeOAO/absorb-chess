@echo off
REM Build script for Absorb Chess Stockfish WASM (Windows)

REM Build flags
set CXXFLAGS=-std=c++17 -O3 -DNDEBUG -flto -DIS64BIT=1 -DHAS_PEXT=0
set WASM_FLAGS=-s WASM=1 -s ALLOW_MEMORY_GROWTH=1 -s MODULARIZE=1 -s EXPORT_NAME=AbsorbChessEngine
set BIND_FLAGS=--bind -s DISABLE_EXCEPTION_CATCHING=0

REM Navigate to source directory
cd /d "%~dp0src"

REM Source files
set SOURCES=absorb_chess_wasm.cpp absorb_tables.cpp benchmark.cpp bitbase.cpp bitboard.cpp endgame.cpp evaluate.cpp material.cpp misc.cpp movegen.cpp movepick.cpp pawns.cpp position.cpp psqt.cpp search.cpp thread.cpp timeman.cpp tt.cpp tune.cpp uci.cpp ucioption.cpp

echo Building Absorb Chess Stockfish WASM...

REM Ensure output directory exists
if not exist "..\..\..\..\frontend\engine" (
    mkdir "..\..\..\..\frontend\engine"
)

REM Build command
emcc %CXXFLAGS% %WASM_FLAGS% %BIND_FLAGS% -I. %SOURCES%  -o ..\..\..\..\frontend\engine\absorb_chess_stockfish.js

if %errorlevel% == 0 (
    echo Build successful! Files generated:
    echo   - absorb_chess_stockfish.js
    echo   - absorb_chess_stockfish.wasm
    echo.
    echo Copy these files to your web application directory.
) else (
    echo Build failed!
    pause
    exit /b 1
)

REM
cd /d ../

pause