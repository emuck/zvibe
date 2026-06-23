@echo off
REM ZVibe Z-Machine Interpreter Launcher
REM Usage: launch_zvibe.bat "path\to\game.z3"

setlocal

REM Get the directory where this batch file is located
set ZVIBE_DIR=%~dp0

REM Check if a game file was provided
if "%~1"=="" (
    echo Usage: %0 "path\to\game.z3"
    echo.
    echo Example: %0 "games\zork1.z3"
    pause
    exit /b 1
)

REM Check if the game file exists
if not exist "%~1" (
    echo Error: Game file "%~1" not found
    pause
    exit /b 1
)

REM Launch ZVibe console with the game file
echo Starting ZVibe with: %~1
echo.
"%ZVIBE_DIR%win\zvibe_console.exe" "%~1"

REM Pause to see any error messages
if errorlevel 1 (
    echo.
    echo ZVibe exited with error code %errorlevel%
    pause
)