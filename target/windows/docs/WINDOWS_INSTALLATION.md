# ZVibe Windows Installation Guide

This guide helps you set up ZVibe for easy desktop launching on Windows 11.

## Quick Setup (5 minutes)

### 1. Create Desktop Shortcuts

**Option A: Double-click `Setup-Desktop-Shortcuts.bat` (Recommended)**

That's it — it runs the PowerShell script for you and creates the shortcuts.

**Expect a security prompt here.** ZVibe's Windows files aren't code-signed
(that requires a paid certificate), so Windows treats them as unrecognized
software freshly downloaded from the internet and will warn you before
running them. This is expected, happens with any unsigned indie or
open-source tool, and isn't a sign anything is wrong with the download.
You'll see one of two prompts, depending on your Windows security settings:

- **SmartScreen** ("Windows protected your PC"): click **"More info"** then
  **"Run anyway."**
- **Smart App Control** (Windows 11, blocks instead of warning): right-click
  the file → **Properties** → check **"Unblock"** at the bottom → **OK**, then
  try again. To unblock the whole extracted folder at once, run this in
  PowerShell:
  ```powershell
  Get-ChildItem "C:\tmp\zvibe_win" -Recurse | Unblock-File
  ```
  (Smart App Control can also be turned off entirely in Windows Security
  settings, but that's a one-way change — Microsoft requires a clean
  Windows reinstall to turn it back on — so it isn't necessary and isn't
  recommended just to run ZVibe.)

**Option B: Run the PowerShell script directly**
1. Right-click on `Create-Desktop-Shortcuts.ps1`
2. Select "Run with PowerShell"

(Double-clicking the `.ps1` file itself just opens it in a text editor —
that's a Windows security default, not a bug. Use the `.bat` file or the
right-click menu instead.)

**Option C: Manual Shortcuts**
1. Right-click on desktop → New → Shortcut
2. Browse to: `C:\tmp\zvibe_win\win\zvibe_console.exe`
3. Add arguments: `restaurant.z3` (or path to your game)
4. Name it "ZVibe Console"

### 2. Set Up File Associations (Optional)

1. Double-click `register_z3_files.reg`
2. Click "Yes" to add to registry
3. **Refresh file icons**: Press F5 in Windows Explorer or restart Explorer to see new `.z3` file icons
4. Now you can right-click any `.z3` file and select "Play with ZVibe"

## Usage Options

### 🖥️ Desktop Shortcuts
After setup, you'll have these desktop icons:

- **Play Restaurant**: One-click launch straight into the bundled game, with its own icon
- **ZVibe Console**: Full interactive experience with status line (same game)
- **ZVibe Minimal**: Clean text output for scripting
- **ZVibe Game Launcher**: Browse and select any other Z3 game
- **ZVibe Folder**: Quick access to installation directory

### 🎮 Playing Games

**Method 1: Double-click "Play Restaurant"**
- The simplest path: double-click the desktop icon and you're in the game

**Method 2: Right-click .z3 files** (after registry setup)
- Right-click any `.z3` file → "Play with ZVibe"

**Method 3: Drag & Drop**
- Drag a `.z3` file onto `zvibe_console.exe` or `zvibe_minimal.exe`

**Method 4: Command Line**
```cmd
cd C:\tmp\zvibe_win\win
zvibe_console.exe "path\to\your\game.z3"
```

**Method 5: Batch Launcher**
- Double-click `launch_zvibe.bat`
- Follow prompts to select a game file

## File Types

### Executables
- **zvibe_console.exe**: Full-featured console with status line and saves
- **zvibe_minimal.exe**: Lightweight version for automation/scripting
- **libzvibe.dll**: Shared library for integration with other apps

### Launchers
- **launch_zvibe.bat**: Simple batch file launcher
- **Launch-ZVibe.ps1**: PowerShell launcher with error handling

### Setup Files
- **register_z3_files.reg**: Associates .z3 files with ZVibe
- **Create-Desktop-Shortcuts.ps1**: Automatically creates desktop shortcuts

## Game Files

ZVibe supports **Z-machine Version 3** story files (`.z3` extension):

- **Included**: `restaurant.z3` — "The Restaurant at the End of the Universe," an original game written for ZVibe
- **Compatible**: Most classic text adventure games
- **Examples**: Zork series, Hitchhiker's Guide, Planetfall, etc.

### Where to Find Games
- **Interactive Fiction Archive**: https://www.ifarchive.org/
- **Classic Text Adventure Collections**
- **Modern IF competitions**

## Customization

### Change Default Game
Edit the desktop shortcuts to point to your preferred game:
1. Right-click shortcut → Properties
2. In "Target" field, change `restaurant.z3` to your game path
3. Example: `"C:\Games\zork1.z3"`

### Create Game-Specific Shortcuts
1. Copy an existing shortcut
2. Change the name and target game
3. Example: "Zork I" shortcut pointing to `zork1.z3`

### Move Installation
If you move the ZVibe folder:
1. Update desktop shortcuts (Properties → Target)
2. Re-run `register_z3_files.reg` with new paths
3. Or re-run `Create-Desktop-Shortcuts.ps1`

## Advanced Usage

### Python Integration
```python
import ctypes
lib = ctypes.CDLL('./libzvibe.dll')
# Use ZVibe API from Python on Windows
```

### Command Line Options
```cmd
zvibe_console.exe game.z3           # Play with full console
zvibe_minimal.exe game.z3           # Play with minimal output
zvibe_console.exe --help            # Show help (if implemented)
```

### Batch Processing
```batch
@echo off
for %%f in (*.z3) do (
    echo Testing %%f
    zvibe_minimal.exe "%%f" < commands.txt > "%%f.log"
)
```

## Troubleshooting

### "Windows protected your PC" / Smart App Control blocked this app
Expected — see [Create Desktop Shortcuts](#1-create-desktop-shortcuts) above.
ZVibe's files aren't code-signed, so freshly downloaded copies have no
reputation with Windows yet. Click "Run anyway" (SmartScreen) or unblock the
file via Properties → "Unblock" (Smart App Control). Not a sign of a problem
with the download.

### Shortcuts Don't Work
- Check that paths in shortcuts match your installation location
- Make sure `zvibe_console.exe` exists in the `win` folder

### "File Association Failed"
- Run `register_z3_files.reg` as Administrator
- Check Windows file association settings in Default Apps

### "PowerShell Execution Policy" Error
```powershell
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
```

### Game File Not Found
- Use full paths in shortcuts: `"C:\full\path\to\game.z3"`
- Place game files in the same folder as the executable
- Check file permissions

### Console Display Issues
- Use Windows Terminal for better experience
- Avoid Command Prompt for interactive games
- Status line works best in modern terminals

## System Requirements

- **OS**: Windows 11 (or Windows 10)
- **Architecture**: 64-bit (x64)
- **Dependencies**: None (uses system libraries only)
- **Disk Space**: ~1MB for executables
- **Memory**: ~25KB per running game (very efficient!)

## Uninstallation

To remove ZVibe:
1. Delete desktop shortcuts
2. Delete the ZVibe folder (`C:\tmp\zvibe_win`)
3. Optional: Remove file association with `unregister_z3_files.reg`

## Support

For issues or questions:
- Check the main ZVibe documentation
- Review build logs in WSL2
- Test with the included `restaurant.z3` file first