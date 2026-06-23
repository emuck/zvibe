# Quickstart

Build and run ZVibe on a desktop system.

## Prerequisites

- GCC or Clang
- Make
- A Z-machine version 3 story file (`.z3`)

## Build

```bash
cd target/console
make all
```

This produces:
- `build/bin/zvibe_console` - Full-featured interpreter
- `build/bin/zvibe_minimal` - Minimal interpreter (no saves)

## Run

```bash
./build/bin/zvibe_console path/to/game.z3
```

Or with the minimal interpreter:

```bash
./build/bin/zvibe_minimal path/to/game.z3
```

## Obtain Test Files

The repository includes the Czech Z-machine test suite (`games/catalog/czech.z3`), which is public domain:

```bash
./build/bin/zvibe_minimal ../../games/catalog/czech.z3
```

For actual games, you must supply your own legally obtained story files. See [STORY_FILES.md](STORY_FILES.md).

## Next Steps

- [INSTALL.md](INSTALL.md) - Full dependency list and cross-compilation
- [USER_GUIDE.md](USER_GUIDE.md) - CLI options and save/restore
- [TARGETS.md](TARGETS.md) - Embedded and FPGA targets
