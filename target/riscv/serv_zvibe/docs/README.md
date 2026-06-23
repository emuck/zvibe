# Servant ZVibe Documentation

Complete documentation for the RISC-V ZVibe platform.

## Quick Navigation

**New to the project?** Start here:
1. [Quick Start](quickstart.md) - Get running on hardware in ~15 minutes
2. [Build Guide](build.md) - Tool requirements, both boards, simulation
3. [Architecture](architecture.md) - Understand the system design

**Looking for specific features?**
- [Save/Restore System](save_restore.md) - Persistent game saves with delta compression
- [Multi-Game Setup](../common/sw/README.md) - Building and managing multiple games

## Documentation Structure

### User Documentation

| Document | Description | Audience |
|----------|-------------|----------|
| [quickstart.md](quickstart.md) | Get FPGA running in 10 minutes | New users |
| [build.md](build.md) | Complete build instructions | Developers |
| [save_restore.md](save_restore.md) | Save/restore feature guide | Users, Developers |

### Technical Documentation

| Document | Description | Audience |
|----------|-------------|----------|
| [architecture.md](architecture.md) | System architecture and design | Developers |
| [rtl/qspi_xip.md](rtl/qspi_xip.md) | XIP flash read controller spec | Hardware developers |
| [rtl/qspi_write.md](rtl/qspi_write.md) | Flash write controller spec | Hardware developers |
| [rtl/uart.md](rtl/uart.md) | UART controller specification | Hardware developers |

### Development Documentation

| Document | Description | Audience |
|----------|-------------|----------|
| [archive/](archive/) | Historical design docs and audits | Reference |

## Common Tasks

### Getting Started
1. Read [quickstart.md](quickstart.md)
2. Build and program FPGA: [build.md](build.md#fpga-build-arty-s7-50)
3. Connect to UART and test

### Adding Games
1. Use game management tools: [build.md](build.md#game-management)
2. Build multi-game system: [../common/sw/README.md](../common/sw/README.md#multi-game-system)
3. Program to flash: [build.md](build.md#program-qspi-flash-persistent)

### Understanding Save/Restore
1. Read overview: [save_restore.md](save_restore.md#overview)
2. Learn delta compression: [save_restore.md](save_restore.md#delta-compression)
3. Check API documentation: [save_restore.md](save_restore.md#api-firmware)

### Adding New Board Support
1. Follow porting guide: [../README.md](../README.md#adding-a-new-board)
2. Review architecture: [architecture.md](architecture.md)
3. Check Arty S7-50 example: [../boards/arty_s7_50/](../boards/arty_s7_50/)

### Debugging Hardware Issues
1. Review RTL specifications: [rtl/](rtl/)
2. Consult archived notes: [archive/](archive/)

## Documentation Conventions

### File Organization
- **Active docs**: Current, maintained documentation
- **archive/**: Historical documents (design notes, audits, bug fixes)
- **rtl/**: Hardware specifications for RTL modules
- **development/**: Development history and debugging notes

### Code References
Documentation uses this format for code references:
- `file_path:line_number` - Navigate to specific code locations
- Example: `s25fl_xip.v:123` - Line 123 of the XIP controller

### Command Examples
All commands show working directory context:
```bash
# Working directory shown in comments
cd boards/arty_s7_50/fpga
make build
```

## Related Documentation

- **Main Project**: [../../../../README.md](../../../../README.md) - ZVibe project overview
- **Firmware**: [../common/sw/README.md](../common/sw/README.md) - Software documentation
- **Board-Specific**: [../boards/arty_s7_50/README.md](../boards/arty_s7_50/README.md) - Arty S7-50 documentation

## Contributing to Documentation

When adding or updating documentation:

1. **Keep it current**: Update docs when implementation changes
2. **Be concise**: Focus on what users need to know
3. **Use examples**: Code snippets and command examples help understanding
4. **Cross-reference**: Link to related documentation
5. **Archive old docs**: Move outdated design docs to `archive/` with context

## Getting Help

- Check [quickstart.md](quickstart.md#troubleshooting) for common issues
- Review [build.md](build.md#troubleshooting) for build problems
- See [save_restore.md](save_restore.md#troubleshooting) for save/restore issues
- Consult archived bug fixes in [archive/](archive/)

---
Last updated: 2026-02-15
