@echo off
REM Double-clickable entry point for Create-Desktop-Shortcuts.ps1
REM
REM .ps1 files open in a text editor by default on Windows (a security
REM default, not a bug) -- double-clicking the PowerShell script directly
REM just shows you its source instead of running it. This batch file exists
REM so there's a real double-click-to-run option.

setlocal
set SCRIPT_DIR=%~dp0

powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%Create-Desktop-Shortcuts.ps1"

echo.
pause
