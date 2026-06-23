# ZVibe Icon Integration Guide

This guide explains how to integrate custom icons with ZVibe executables and file associations on Windows.

## 🎨 Icon Integration Overview

ZVibe includes a complete icon integration system that provides:
- **Executable Icons**: Custom icons embedded in .exe files
- **File Association Icons**: Icons for .z3 game files in Windows Explorer
- **Desktop Integration**: Professional appearance in shortcuts and taskbar

## 📁 Current Icon Setup

### Generated Icons
The build system uses custom icons:

```
target/windows/icons/
├── ZVibe.ico            # Custom icon for both applications
└── Z3.ico               # Custom icon for Z3 game files
```

### Windows Installation
Icons are deployed to Windows installation:

```
C:\tmp\zvibe_win\
├── win\
│   ├── zvibe_console.exe    # Executable with embedded blue icon
│   └── zvibe_minimal.exe    # Executable with embedded gray icon
├── Z3.ico                  # Icon for .z3 file associations
└── register_z3_files.reg   # Registry file using custom icon
```

## 🔧 Build System Integration

### Resource Files
Each executable has its own resource file:

- `zvibe_console.rc` - Console application resources
- `zvibe_minimal.rc` - Minimal application resources

### Makefile Targets
Icons are automatically embedded during build:

```bash
make windows-console    # Builds console.exe with embedded icon
make windows-minimal    # Builds minimal.exe with embedded icon
make windows-all        # Builds all executables with icons
make windows-deploy     # Build + update Windows installation
```

### Cross-Compilation
Uses MinGW-w64 resource compiler:
- `x86_64-w64-mingw32-windres` compiles .rc files
- Resources embedded during final linking step
- No external dependencies required

## 🎨 Customizing Icons

### Option 1: Replace Placeholder Icons

1. **Create custom .ico files** (recommended sizes: 16x16, 32x32, 48x48, 256x256):
   ```
   ZVibe.ico        - Both console and minimal applications
   Z3.ico           - Z3 game files
   ```

2. **Rebuild executables**:
   ```bash
   make windows-deploy
   ```

### Option 2: Automated Icon Generation

Use the provided script to create simple icons:

```bash
python3 create_placeholder_icons.py
```

Or with ImageMagick (if installed):
```bash
./create_icons.sh
```

### Option 3: Professional Icon Creation

**Recommended tools:**
- **GIMP** - Free, supports ICO format
- **Paint.NET** - Windows, with ICO plugin
- **Online converters** - PNG to ICO conversion
- **Icon editors** - IcoFX, IconWorkshop

**Icon specifications:**
- **Format**: .ico (Windows icon format)
- **Sizes**: Multiple sizes in one file (16x16, 32x32, 48x48)
- **Color depth**: 32-bit with alpha channel recommended
- **Style**: Professional, consistent with application theme

## 📋 Icon Usage in Windows

### Executable Icons
- **File Explorer**: Icons appear next to .exe files
- **Desktop Shortcuts**: Icons used in desktop shortcuts
- **Taskbar**: Icons shown in Windows taskbar
- **Alt+Tab**: Icons displayed in application switcher

### File Association Icons
- **Z3 Files**: Custom icon for .z3 game files in Explorer
- **Right-click Menu**: Icon shown in context menu
- **File Properties**: Icon displayed in file properties dialog

## 🔧 Manual Icon Integration

### For Existing Installation

1. **Replace icon files**:
   ```
   C:\tmp\zvibe_win\Z3.ico
   ```

2. **Update registry** (run as Administrator):
   ```cmd
   regedit /s register_z3_files.reg
   ```

3. **Refresh icon cache**:
   ```cmd
   ie4uinit.exe -show
   ```

### For New Installation

Icons are automatically included when using:
- `Create-Desktop-Shortcuts.ps1`
- `register_z3_files.reg`
- Windows deployment scripts

## 🔍 Troubleshooting Icons

### Icons Not Appearing

**Check icon files exist**:
```cmd
dir C:\tmp\zvibe_win\*.ico
```

**Verify executable resources**:
```bash
x86_64-w64-mingw32-objdump -p zvibe_console.exe | grep -i icon
```

**Refresh Windows icon cache**:
```cmd
ie4uinit.exe -show
taskkill /f /im explorer.exe
start explorer.exe
```

### Icon Quality Issues

**Common problems:**
- Icons too small (recommend 32x32 minimum)
- Missing alpha channel (causes jagged edges)
- Single size only (provide multiple sizes)
- Wrong format (must be .ico, not .png)

**Solutions:**
- Use proper icon editor
- Include multiple icon sizes (16x16, 32x32, 48x48)
- Test icons at different sizes
- Use vector graphics for scaling

### File Association Issues

**Registry not applied**:
```cmd
# Run as Administrator
regedit /s register_z3_files.reg
```

**Icon path incorrect**:
- Ensure Z3.ico exists at specified path
- Use absolute paths in registry
- Check for spaces in path names

## 📈 Advanced Customization

### Multiple Icon Themes

Create different icon sets for themes:
```
icons/
├── classic/
│   ├── zvibe_console.ico
│   └── zvibe_minimal.ico
├── modern/
│   ├── zvibe_console.ico
│   └── zvibe_minimal.ico
└── dark/
    ├── zvibe_console.ico
    └── zvibe_minimal.ico
```

### Version Information

Resource files include version info:
- Product name and version
- File description
- Copyright information
- Company name

Customize in `.rc` files:
```rc
VALUE "ProductName", "Your Custom ZVibe"
VALUE "FileDescription", "Custom Z-Machine Interpreter"
VALUE "CompanyName", "Your Organization"
```

### Icon Animation (Advanced)

For dynamic icons:
- Use animated .ico files
- Multiple frames for different states
- Supported by some Windows versions

## 🎯 Best Practices

### Icon Design
- **Consistent style** across all ZVibe icons
- **Recognizable at small sizes** (16x16)
- **High contrast** for visibility
- **Professional appearance** for distribution

### File Organization
- Keep icon sources (e.g., .psd, .svg) for editing
- Maintain consistent naming convention
- Include multiple sizes in .ico files
- Test icons on different backgrounds

### Distribution
- Include icons in installation packages
- Document icon customization for users
- Provide icon sources for community customization
- Test icon appearance on different Windows themes

The icon integration system provides a professional appearance for ZVibe on Windows while maintaining easy customization for different use cases and branding needs.