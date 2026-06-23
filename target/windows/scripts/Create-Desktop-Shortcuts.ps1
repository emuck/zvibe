# Create Desktop Shortcuts for ZVibe
# Run this PowerShell script as Administrator to create desktop shortcuts

param(
    [string]$ZVibeDir = "C:\tmp\zvibe_win",
    [string]$DesktopPath = [Environment]::GetFolderPath("Desktop")
)

Write-Host "Creating ZVibe desktop shortcuts..."
Write-Host "ZVibe Directory: $ZVibeDir"
Write-Host "Desktop Path: $DesktopPath"
Write-Host ""

# Create COM object for shortcuts
$WshShell = New-Object -ComObject WScript.Shell

# 1. Create shortcut for ZVibe Console
$ConsoleShortcut = $WshShell.CreateShortcut("$DesktopPath\ZVibe Console.lnk")
$ConsoleShortcut.TargetPath = "$ZVibeDir\win\zvibe_console.exe"
$ConsoleShortcut.WorkingDirectory = $ZVibeDir
$ConsoleShortcut.Description = "ZVibe Z-Machine Interpreter (Full Console)"
$ConsoleShortcut.Arguments = "czech.z3"  # Default to test game
$ConsoleShortcut.Save()
Write-Host "✓ Created: ZVibe Console.lnk"

# 2. Create shortcut for ZVibe Minimal
$MinimalShortcut = $WshShell.CreateShortcut("$DesktopPath\ZVibe Minimal.lnk")
$MinimalShortcut.TargetPath = "$ZVibeDir\win\zvibe_minimal.exe"
$MinimalShortcut.WorkingDirectory = $ZVibeDir
$MinimalShortcut.Description = "ZVibe Z-Machine Interpreter (Minimal)"
$MinimalShortcut.Arguments = "czech.z3"  # Default to test game
$MinimalShortcut.Save()
Write-Host "✓ Created: ZVibe Minimal.lnk"

# 3. Create shortcut for the batch launcher
$LauncherShortcut = $WshShell.CreateShortcut("$DesktopPath\ZVibe Game Launcher.lnk")
$LauncherShortcut.TargetPath = "$ZVibeDir\launch_zvibe.bat"
$LauncherShortcut.WorkingDirectory = $ZVibeDir
$LauncherShortcut.Description = "Launch ZVibe with any Z3 game file"
$LauncherShortcut.Save()
Write-Host "✓ Created: ZVibe Game Launcher.lnk"

# 4. Create shortcut to the ZVibe directory
$FolderShortcut = $WshShell.CreateShortcut("$DesktopPath\ZVibe Folder.lnk")
$FolderShortcut.TargetPath = $ZVibeDir
$FolderShortcut.Description = "Open ZVibe installation folder"
$FolderShortcut.Save()
Write-Host "✓ Created: ZVibe Folder.lnk"

Write-Host ""
Write-Host "Desktop shortcuts created successfully!"
Write-Host ""
Write-Host "Usage:"
Write-Host "• Double-click 'ZVibe Console' or 'ZVibe Minimal' to play the test game"
Write-Host "• Right-click any .z3 file → 'Play with ZVibe' (after running register_z3_files.reg)"
Write-Host "• Use 'ZVibe Game Launcher' to select any Z3 game file"
Write-Host "• 'ZVibe Folder' opens the installation directory"