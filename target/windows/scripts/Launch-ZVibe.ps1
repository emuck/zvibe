# ZVibe Z-Machine Interpreter Launcher (PowerShell)
# Usage: Launch-ZVibe.ps1 -GameFile "path\to\game.z3"

param(
    [Parameter(Mandatory=$true)]
    [string]$GameFile,

    [Parameter(Mandatory=$false)]
    [switch]$Minimal = $false
)

# Get the directory where this script is located
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition

# Choose executable based on -Minimal switch
if ($Minimal) {
    $ZVibeExe = Join-Path $ScriptDir "win\zvibe_minimal.exe"
    Write-Host "Using minimal ZVibe interpreter"
} else {
    $ZVibeExe = Join-Path $ScriptDir "win\zvibe_console.exe"
    Write-Host "Using full ZVibe console"
}

# Check if ZVibe executable exists
if (-not (Test-Path $ZVibeExe)) {
    Write-Error "ZVibe executable not found: $ZVibeExe"
    Read-Host "Press Enter to exit"
    exit 1
}

# Check if game file exists
if (-not (Test-Path $GameFile)) {
    Write-Error "Game file not found: $GameFile"
    Read-Host "Press Enter to exit"
    exit 1
}

# Launch ZVibe
Write-Host "Starting ZVibe with: $GameFile"
Write-Host "Executable: $ZVibeExe"
Write-Host ""

try {
    & $ZVibeExe $GameFile
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "ZVibe exited with code $LASTEXITCODE"
        Read-Host "Press Enter to exit"
    }
} catch {
    Write-Error "Failed to start ZVibe: $_"
    Read-Host "Press Enter to exit"
}