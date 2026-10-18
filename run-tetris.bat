@echo off
setlocal

REM Forge Tetris - Build (Debug) and run the game
REM Usage: run-tetris.bat [args...]

cd /d "%~dp0"

set BUILD_DIR=build

echo === Building Forge Tetris (Debug) ===
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cmake -S . -B "%BUILD_DIR%" -DCMAKE_BUILD_TYPE=Debug
if errorlevel 1 exit /b 1
cmake --build "%BUILD_DIR%" --target ForgeTetris --parallel
if errorlevel 1 exit /b 1
echo   ForgeTetris built

echo.
echo === Running Tetris ===
"%BUILD_DIR%\bin\Debug\ForgeTetris.exe" %*