# Create Desktop Shortcuts for ZVibe
# Run this PowerShell script to create desktop shortcuts.
# No administrator rights required.

param(
    [string]$ZVibeDir = "C:\tmp\zvibe_win",
    [string]$DesktopPath = [Environment]::GetFolderPath("Desktop"),
    [string]$DefaultGame = "restaurant.z3"
)

Write-Host "Creating ZVibe desktop shortcuts..."
Write-Host "ZVibe Directory: $ZVibeDir"
Write-Host "Desktop Path: $DesktopPath"
Write-Host ""

# Create COM object for shortcuts
$WshShell = New-Object -ComObject WScript.Shell

# 1. Play Restaurant - the flagship one-click experience. Double-click and
#    you're straight into "The Restaurant at the End of the Universe" with
#    its own icon, no arguments or file picking required.
$PlayShortcut = $WshShell.CreateShortcut("$DesktopPath\Play Restaurant.lnk")
$PlayShortcut.TargetPath = "$ZVibeDir\win\zvibe_console.exe"
$PlayShortcut.WorkingDirectory = "$ZVibeDir\win"
$PlayShortcut.Description = "The Restaurant at the End of the Universe (ZVibe)"
$PlayShortcut.Arguments = "`"$DefaultGame`""
$PlayShortcut.IconLocation = "$ZVibeDir\Restaurant.ico"
$PlayShortcut.Save()
Write-Host "* Created: Play Restaurant.lnk"

# 2. Create shortcut for ZVibe Console
$ConsoleShortcut = $WshShell.CreateShortcut("$DesktopPath\ZVibe Console.lnk")
$ConsoleShortcut.TargetPath = "$ZVibeDir\win\zvibe_console.exe"
$ConsoleShortcut.WorkingDirectory = "$ZVibeDir\win"
$ConsoleShortcut.Description = "ZVibe Z-Machine Interpreter (Full Console)"
$ConsoleShortcut.Arguments = "`"$DefaultGame`""
$ConsoleShortcut.Save()
Write-Host "* Created: ZVibe Console.lnk"

# 3. Create shortcut for ZVibe Minimal
$MinimalShortcut = $WshShell.CreateShortcut("$DesktopPath\ZVibe Minimal.lnk")
$MinimalShortcut.TargetPath = "$ZVibeDir\win\zvibe_minimal.exe"
$MinimalShortcut.WorkingDirectory = "$ZVibeDir\win"
$MinimalShortcut.Description = "ZVibe Z-Machine Interpreter (Minimal)"
$MinimalShortcut.Arguments = "`"$DefaultGame`""
$MinimalShortcut.Save()
Write-Host "* Created: ZVibe Minimal.lnk"

# 4. Create shortcut for the batch launcher
$LauncherShortcut = $WshShell.CreateShortcut("$DesktopPath\ZVibe Game Launcher.lnk")
$LauncherShortcut.TargetPath = "$ZVibeDir\launch_zvibe.bat"
$LauncherShortcut.WorkingDirectory = $ZVibeDir
$LauncherShortcut.Description = "Launch ZVibe with any Z3 game file"
$LauncherShortcut.Save()
Write-Host "* Created: ZVibe Game Launcher.lnk"

# 5. Create shortcut to the ZVibe directory
$FolderShortcut = $WshShell.CreateShortcut("$DesktopPath\ZVibe Folder.lnk")
$FolderShortcut.TargetPath = $ZVibeDir
$FolderShortcut.Description = "Open ZVibe installation folder"
$FolderShortcut.Save()
Write-Host "* Created: ZVibe Folder.lnk"

Write-Host ""
Write-Host "Desktop shortcuts created successfully!"
Write-Host ""
Write-Host "Usage:"
Write-Host "- Double-click 'Play Restaurant' to jump straight into the bundled game"
Write-Host "- Double-click 'ZVibe Console' or 'ZVibe Minimal' for the same game, full/lightweight interpreter"
Write-Host "- Right-click any .z3 file -> 'Play with ZVibe' (after running register_z3_files.reg)"
Write-Host "- Use 'ZVibe Game Launcher' to select any other Z3 game file"
Write-Host "- 'ZVibe Folder' opens the installation directory"