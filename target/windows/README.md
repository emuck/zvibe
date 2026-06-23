# ZVibe Windows Target

Windows build system: native Windows executables cross-compiled from WSL2 Ubuntu.

## Directory Structure

```
target/windows/
├── README.md                    # This file
├── Makefile                     # Windows-specific build system
├── icons/                       # Application and file icons
│   ├── ZVibe.ico               # Main application icon (custom)
│   └── Z3.ico                  # Z3 game file icon (custom)
├── resources/                   # Windows resource files
│   ├── zvibe_console.rc        # Console app resources
│   ├── zvibe_minimal.rc        # Minimal app resources
│   └── zvibe.rc                # Shared resources
├── scripts/                     # Windows installation scripts
│   ├── update_windows_build.sh        # Update existing installation
│   ├── build_and_update_windows.sh    # Build and update
│   ├── create_placeholder_icons.py    # Generate placeholder icons
│   ├── Create-Desktop-Shortcuts.ps1   # Desktop shortcuts
│   ├── Launch-ZVibe.ps1               # PowerShell launcher
│   ├── launch_zvibe.bat               # Batch launcher
│   ├── register_z3_files.reg          # File associations
│   └── unregister_z3_files.reg        # Remove associations
├── docs/                        # Windows-specific documentation
│   ├── ICON_INTEGRATION.md     # Icon customization guide
│   └── WINDOWS_INSTALLATION.md # End-user installation guide
└── build/                       # Build output (created during build)
    ├── obj/                     # Object files
    ├── bin/                     # Windows executables (.exe)
    └── lib/                     # Windows libraries (.dll)
```

## Quick Start

### Prerequisites
```bash
sudo apt install mingw-w64 gcc-mingw-w64
```

### Basic Usage
```bash
cd target/windows

# Build all Windows targets
make all

# Deploy to Windows (builds + installs)
make deploy

# Individual targets
make console    # Console application only
make minimal    # Minimal application only
make shared     # Shared library only
```

### Windows Installation
```bash
# Full deployment with all setup files
make deploy

# Quick update of existing installation
make update
```

## Build Targets

| Target | Description | Output |
|--------|-------------|---------|
| `all` | Build console, minimal, and shared library | All .exe and .dll files |
| `console` | Full-featured console application | `zvibe_console.exe` |
| `minimal` | Lightweight version for scripting | `zvibe_minimal.exe` |
| `shared` | Shared library for integration | `libzvibe.dll` |
| `install` | Build and copy to Windows installation | Updates `C:\tmp\zvibe_win` |
| `deploy` | Full deployment with setup files | Complete Windows package |
| `update` | Quick update using existing scripts | Updates executables only |
| `icons` | Show icon management information | Help with icon customization |
| `clean` | Remove build artifacts | Cleans `build/` directory |
| `clean-all` | Remove build and Windows installation | Complete cleanup |
| `help` | Show available targets | Usage information |

## Icon Customization

### Using Your Custom Icon
The build system is configured to use your custom `ZVibe.ico` file for both console and minimal applications.

**Current setup:**
- `icons/ZVibe.ico` - Your custom icon (39KB)
- Both executables will embed this icon
- Professional appearance in Windows Explorer, taskbar, etc.

### Managing Icons
```bash
# Show current icon status
make icons

# Create placeholder icons (if needed)
python3 scripts/create_placeholder_icons.py

# Replace with custom icons
# Just replace files in icons/ directory and rebuild
```

## Development Workflow

### Standard Development Cycle
```bash
# 1. Make code changes in src/ or console/
# 2. Build and deploy
make deploy

# 3. Test on Windows
# Open Windows Terminal or Command Prompt:
# cd C:\tmp\zvibe_win\win
# zvibe_console.exe czech.z3
```

### Quick Updates
```bash
# For rapid iteration during development
make update
```

### Clean Rebuilds
```bash
# Remove all build artifacts and rebuild
make clean-all
make deploy
```

## Output Files

### Executables (in `build/bin/`)
- **zvibe_console.exe** - Full console with status line, saves, terminal control
- **zvibe_minimal.exe** - Lightweight for scripting, no saves or status line

### Libraries (in `build/lib/`)
- **libzvibe.dll** - Shared library for integration with other applications
- **libzvibe.dll.a** - Import library for linking with Visual Studio

### Windows Installation (in `C:\tmp\zvibe_win`)
```
C:\tmp\zvibe_win\
├── win\
│   ├── zvibe_console.exe        # With embedded custom icon
│   ├── zvibe_minimal.exe        # With embedded custom icon
│   └── czech.z3                 # Test game
├── win_lib\
│   ├── libzvibe.dll             # Shared library
│   └── libzvibe.dll.a           # Import library
├── Z3.ico                       # Icon for .z3 file associations
├── Create-Desktop-Shortcuts.ps1 # Desktop integration
├── Launch-ZVibe.ps1             # PowerShell launcher
├── launch_zvibe.bat             # Batch launcher
├── register_z3_files.reg        # File associations
├── unregister_z3_files.reg      # Remove associations
└── WINDOWS_INSTALLATION.md      # End-user guide
```

## Integration with Main Project

This Windows target integrates cleanly with the main ZVibe project:

- **Shared source code** - Uses same `src/` and console application code
- **Independent builds** - Doesn't interfere with Linux/macOS builds
- **Consistent API** - Same zvibe_api.h interface across all platforms
- **Cross-platform testing** - Same test games work everywhere

## Features

### Windows-Specific Optimizations
- **Native Windows executables** - No runtime dependencies
- **Custom icons embedded** - Professional Windows appearance
- **File associations** - Right-click .z3 files to play
- **Desktop integration** - Shortcuts, taskbar, Alt+Tab support
- **Registry integration** - Proper Windows file type handling
- **Status bar optimization** - Single update per command, change detection
- **Console API integration** - Uses Windows Console API instead of POSIX

### Cross-Platform Compatibility
- **Same Z-machine engine** - Identical game compatibility
- **Same memory architecture** - 85-95% RAM reduction
- **Same API interface** - Easy integration with other applications
- **Same performance optimizations** - Status line, memory efficiency

## Maintenance

### Updating Icons
1. Replace `icons/ZVibe.ico` with your new icon
2. Run `make deploy`
3. Icons automatically embedded in new executables

### Updating Scripts
- Launcher scripts in `scripts/` are copied during deployment
- Modify scripts and run `make deploy` to update Windows installation

### Updating Documentation
- Documentation in `docs/` is copied to Windows installation
- Update and run `make deploy` to distribute changes

## Best Practices

### Icon Management
- Keep source files (.psd, .svg) for icon editing
- Include multiple sizes in .ico files (16x16, 32x32, 48x48, 256x256)
- Test icons at different sizes and color depths
- Maintain consistent visual style across all icons

### Build Management
- Use `make deploy` for complete updates
- Use `make update` for quick executable-only updates
- Use `make clean-all` before major releases
- Test on actual Windows system after deployment

### Distribution
- The `C:\tmp\zvibe_win` directory contains everything needed for distribution
- Can be zipped and distributed as complete Windows package
- Includes all necessary setup files and documentation
- No external dependencies required

## Documentation

- `docs/ICON_INTEGRATION.md` — icon customization guide
- `docs/WINDOWS_INSTALLATION.md` — end-user setup and usage

## License

BSD-3-Clause. See [../../LICENSE](../../LICENSE).