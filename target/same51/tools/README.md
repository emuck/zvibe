# SAM E51 Tools (Legacy)

**DEPRECATED:** This directory contains legacy build scripts that are no longer maintained.

## Use the Unified Build System Instead

The ZVibe project now uses a unified multi-target build system located at the repository root.

### Building Multi-Game for SAM E51

```bash
# From target/same51/ directory:
cd ../../games
./get_games.py browse          # Download games interactively

./build_games.py same51        # Build multi-game system for SAM E51

cd ../target/same51
make                           # Compile firmware
make program                   # Program device
```

### Documentation

- See `../../games/build_games.py --help` for all options
- See `../../games/get_games.py --help` for game management
- See `../README.md` for complete SAM E51 build instructions

## Remaining Tools

This directory now contains only utilities still used by the local build system:

- `z3_to_c_array.py` - Converts .z3 files to C arrays (used by Makefile for single-game builds)
- `memory_usage.sh` - Memory analysis utility for build output

**Note:** Multi-game builds now use `../../games/build_games.py` which has its own game conversion logic.

## Benefits of Unified Build System

- Multi-target support (SAM E51, RISC-V, future targets)
- Centralized game registry with metadata (`../../games/registry.json`)
- Per-target game filtering (SAM E51: 8 games, RISC-V: 25 games)
- Better game management tools (`../../games/get_games.py`)
- Consistent build experience across all targets
