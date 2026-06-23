# SAM E51 zVibe Z-Machine Interpreter

Production-ready Z-Machine interpreter for SAM E51 Curiosity Nano with SmartEEPROM save/restore functionality.

This implementation supports both **single-game** and **multi-game** modes, with simplified peripheral drivers based on the original MCC Harmony design.

## Build Modes

The interpreter automatically detects and builds in the appropriate mode:

### Single-Game Mode (Default)
```bash
make                                        # Build with default game (Czech test)
make STORY_FILE=~/games/mygame.z3          # Build with custom game
make program                               # Build and program device
```
- **One game per build**: Specified via `STORY_FILE` parameter
- **Automatic startup**: Game launches immediately after power-on
- **Smaller memory footprint**: Only one game in flash

### Multi-Game Mode (Automatic Detection)
```bash
cd ../../games
./get_games.py sync                       # Regenerate registry.json from eblong.com
./get_games.py browse                     # Download game collection
./build_games.py same51                   # Generate multi-game files
cd ../target/same51 && make program       # Build and program device
```
- **Multiple games**: Interactive menu system
- **Game selection**: Choose from menu at startup
- **Button navigation**: Press button to return to game menu
- **Larger game library**: Multiple Infocom classics included

## Quick Start

### Connect and Play
1. Connect SAM E51 Curiosity Nano via USB
2. Open terminal at **115200 baud** (8N1)
3. **Single-game**: Game starts automatically
4. **Multi-game**: Select game from menu (1-9, then Enter)
5. Use standard Z-machine commands: `look`, `take lamp`, `save`, `restore`
6. Press button to restart (single-game) or return to menu (multi-game)

## Build Options

### Available Targets

| Target | Description | Features |
|--------|-------------|----------|
| `make` | Auto-detect build mode | Single-game or multi-game |
| `make program` | Build and program device | One-step deployment |
| `make clean` | Remove build artifacts | Clean development |
| `make help` | Show all options | Complete command reference |

### Single-Game Configuration

```bash
# Build only
make STORY_FILE=mygame.z3

# Build and program (recommended - preserves single-game mode)
make STORY_FILE=mygame.z3 program

# Build with different games  
make STORY_FILE=../../games/catalog/czech.z3 program
make STORY_FILE=~/games/mygame.z3 program
```

**Important:** Use the combined `make STORY_FILE=<file> program` command for single-game builds. Running `make program` separately will use the default build mode detection.

### Multi-Game Setup

```bash
# Set up multi-game mode
cd ../../games
./get_games.py sync                        # Regenerate registry.json from eblong.com
./get_games.py browse                      # Download game collection
./build_games.py same51                    # Build multi-game system
cd ../target/same51
make                                       # Build firmware with generated files

# Return to single-game mode
make clean
make STORY_FILE=mygame.z3                  # Back to single-game mode
```

## Features

### Multi-Game System
- **Automatic mode detection**: Builds single-game or multi-game based on `src/game_registry.c`
- **Interactive game menu**: Select from 9+ classic Infocom games
- **Seamless switching**: Button press returns to game selection menu
- **Individual game saves**: Each game maintains its own save state
- **Game library management**: Easy addition/removal of games via Python scripts
- **Memory efficient**: Games loaded on-demand from flash

### SmartEEPROM Save/Restore (Both Modes)
- **Delta compression**: Saves only bytes that differ from original game data (typically 500-2000 bytes vs 14KB+ for full saves)
- **Per-game save slots**: 16KB SmartEEPROM divided into 8 x 2KB slots — each game gets its own independent save
- **Graceful overflow**: Games beyond slot 8 are marked `[no save]` in the menu with clear error messages
- **Optimized configuration**: SBLK=3, PSZ=7 (512-byte pages) for maximum performance
- **Hardware wear leveling** for reliability (214x wear leveling factor)
- **Fast writes**: BUFFERED mode + 32-bit word access + automatic page reallocation
- **Persistent saves** survive power cycles
- **Data validation** with delta magic number header
- **Automatic fuse configuration** for optimal 16KB operation

### UART Interface
- **115200 baud** serial communication
- **ANSI terminal compatible** output
- **Command echo** and line editing
- **Save/restore status** messages

### Hardware Integration
- **LED activity indicator** (blinks on keypress)
- **Button reset** functionality
- **Automatic game startup** after programming
- **Status feedback** via UART

## File Structure

```
target/same51/
├── Makefile                           # Auto-detecting build system (single/multi-game)
├── README.md                          # This file  
├── src/                               # Simplified peripheral drivers
│   ├── zvibe_same51.c                 # Single-game implementation
│   ├── zvibe_same51_multi.c           # Multi-game implementation
│   ├── story_data.c                   # Generated from .z3 file (single-game)
│   ├── game_registry.c                # Generated game registry (multi-game)
│   ├── *_data.c                       # Generated game data files (multi-game)
│   ├── save_delta.c                   # Delta compression for saves
│   ├── clock.c                        # System clock configuration
│   ├── dma.c                          # DMA controller
│   ├── flash.c                        # Flash & SmartEEPROM
│   ├── gpio.c                         # GPIO & External interrupts
│   ├── system.c                       # Interrupt controller & Event system
│   ├── timer.c                        # RTC timer functionality
│   └── uart.c                         # SERCOM5 UART driver
│   └── save_delta.h                   # Delta compression header
├── include/                           # Driver headers
│   ├── definitions.h                  # System-wide definitions
│   ├── clock.h, dma.h, flash.h        # Driver interface headers
│   ├── gpio.h, system.h, timer.h      # Driver interface headers
│   └── uart.h                         # UART interface header
├── build/                             # Build artifacts directory
│   ├── obj/                           # Object files (.o, .d)
│   └── bin/                           # Final binaries
├── startup/                           # Device startup code
├── packs/                             # Device headers and CMSIS core
├── tests/smarteeprom/                 # SmartEEPROM test suite
│   ├── README_SMARTEEPROM.md          # Complete SmartEEPROM documentation
│   └── smarteeprom_test.c             # Hardware test program
└── tools/                             # Build utilities (legacy)
    ├── z3_to_c_array.py               # Story file converter (for single-game builds)
    ├── memory_usage.sh                # Memory analysis utility
    └── README.md                      # Migration guide to unified build system
```

**Note:** Multi-game files (game_registry.c, *_data.c) are generated by `../../games/build_games.py` and placed in the `src/` directory. Game files are managed centrally in `../../games/`.

### Peripheral Drivers

The `src/` directory contains simplified peripheral drivers based on the original MCC Harmony design:

| Driver | Purpose |
|--------|---------|
| `clock.c` | System clock configuration (DPLL, GCLK) |
| `dma.c` | DMA controller |
| `flash.c` | Flash memory and SmartEEPROM operations |
| `gpio.c` | GPIO pins and external interrupts |
| `system.c` | Interrupt controller and system functions |
| `timer.c` | RTC timer for periodic operations |
| `uart.c` | SERCOM5 UART for serial communication |

## Game Save/Restore Usage

### In-Game Commands (Both Modes)
```
> save
[Game saved to SmartEEPROM]

> restore  
[Game restored from SmartEEPROM]

> restart
[Restarting game]
```

### Multi-Game Menu System
```
============================================
          Z-Machine Game Collection
============================================
Select a game to play:

1. Game 1
2. Game 2  
3. Game 3
4. Game 4
5. Game 5
6. Game 6
7. Game 7
8. Game 8
9. Game 9

Enter your choice (1-9): _
```

**Navigation:**
- **Type number + Enter**: Start selected game
- **Press hardware button**: Return to game menu (during gameplay)
- **Power cycle**: Returns to game menu

### Save Status Messages
```
[Saved game found - type 'restore' to load]
[Save not available for this game (EEPROM full)]
[Save failed: delta too large for slot]
[No saved game found]
[Restore failed: corrupt save data]
```

### Save Data Details
- **Automatic**: No file management needed
- **Per-game isolation**: Saving in one game never affects another
- **Persistent**: Survives power cycles and device reprogramming
- **Delta compressed**: Only changed bytes stored (typically 500-2000 bytes per save)
- **Smart fusing**: Automatic 16KB SmartEEPROM configuration

## Hardware Requirements

### SAM E51 Curiosity Nano
- **Microcontroller**: ATSAME51J20A (or compatible SAM E51)
- **Flash Memory**: 1MB (sufficient for firmware + SmartEEPROM)
- **RAM**: 256KB (sufficient for Z-machine execution)

### Development Tools
- **XC32 Compiler**: Microchip toolchain (v4.60+ or v5.0+); see macOS notes below
- **edbg Programmer**: Cross-platform CMSIS-DAP programmer
- **Python 3**: For story file conversion
- **Terminal Program**: 115200 baud serial connection (picocom, minicom, screen)

### macOS Setup

#### edbg (Recommended Programmer)
Build from source — no Homebrew formula exists. On Intel and Apple Silicon:

```bash
git clone https://github.com/ataradov/edbg
cd edbg
make
sudo cp edbg /usr/local/bin/
```

Verify:
```bash
edbg -l    # should show: nEDBG CMSIS-DAP
```

> **Note**: The macOS HID kernel extension does not conflict with this build.
> MPLAB X (ipecmd.sh) is no longer required.

#### XC32 Compiler
Both v4.60 and v5.0+ work correctly. v5.0+ requires the SAME51 Device Family Pack (DFP),
which is installed automatically when you install MPLAB X or via `mchp-packs-cli`.
The Makefile auto-detects the latest installed version; override with `make XC32_VERSION=v4.60`.

#### UART Timing
The firmware shows **"Press RETURN to start..."** at boot and waits — so you will never miss the game output.
- Connect your terminal (`picocom -b 115200 /dev/cu.usbmodem*`) at any time and press **Enter** to start
- Type `~reset~` + Enter at any prompt to restart the game cleanly (used by the automated test script)

### Optional Components
- **Button**: Connected to EIC_PIN_15 for reset functionality
- **LED**: Connected to LED0 for activity indication
- **UART**: Built-in USB CDC for serial communication

## Development

### Adding Custom Games (Single-Game Mode)
1. Place Z-machine file (`.z3`) in target directory
2. Build with custom story: `make STORY_FILE=mygame.z3`
3. Program device: `make program`

### Managing Game Collections (Multi-Game Mode)
```bash
# Set up multi-game environment
cd ../../games
./get_games.py sync                        # Regenerate registry.json from eblong.com
./get_games.py browse                      # Downloads games interactively

# Add custom games to collection
cp ~/my_custom_game.z3 catalog/
./build_games.py same51                    # Rebuilds game registry

# Remove games from collection
rm catalog/unwanted_game.z3
./build_games.py same51                    # Rebuilds without removed game

# Check available games
./get_games.py list                        # List all registered games
cat registry.json                          # View game metadata
```

### Build Mode Switching
```bash
# Switch to multi-game mode
cd ../../games
./get_games.py sync                        # Regenerate registry.json from eblong.com
./get_games.py browse                      # Download games to registry
./build_games.py same51                    # Generate multi-game files
cd ../target/same51
make clean && make                         # Builds multi-game

# Switch to single-game mode
make clean && make STORY_FILE=mygame.z3    # Builds single-game
```

### Testing SmartEEPROM
```bash
make test                           # Run comprehensive hardware tests
# Connect terminal at 115200 baud, press button to start tests
```

### Running UART Game Tests
To run the fully automated test suite:

```bash
cd tests
./test_uart.py
```

The test script will automatically:
2. **Build** the firmware with the game embedded  
3. **Program** the SAM E51 device via edbg
4. **Execute** the complete game test via UART
5. **Verify** successful game completion

**Prerequisites**: SAM E51 Curiosity Nano connected via USB, XC32 compiler, edbg programmer installed.

Automated testing requires only device connection.

### Build Variants
- **Production**: Use SmartEEPROM variant for release builds
- **Development**: Use basic variant for faster iteration (no EEPROM config)
- **Testing**: Use test target for hardware validation

## Troubleshooting

### Common Issues

| Issue | Solution |
|-------|----------|
| No UART output after programming | Firmware waits at "Press RETURN to start..." — connect terminal and press Enter; or type `~reset~` + Enter |
| No UART output at all | Check 115200 baud, 8N1 settings; verify edbg programmed device (check `edbg -l`) |
| Game doesn't start | Verify story file exists and is valid Z-machine |
| Save/restore fails | Run `make test` to verify SmartEEPROM |
| Build errors | Check XC32 compiler installation |
| Programming fails | Verify edbg and SAM E51 connection |
| **Multi-Game Issues** | |
| Menu doesn't appear | Verify games are in `../../games/registry.json` and downloaded |
| Game selection fails | Check game number (1-9) and press Enter |
| Button doesn't return to menu | Hardware button should be connected to EIC_PIN_15 |
| Build fails in multi-game mode | Run `cd ../../games && ./build_games.py same51` to regenerate |
| **Single-Game Issues** | |
| Wrong build mode detected | Use `make STORY_FILE=<file> program` (combined command) |
| `make program` switches to multi-game | Run `make STORY_FILE=<file> program` in single command |
| Custom game not found | Check `STORY_FILE` path is correct and file exists |

### Debug Information
- **UART messages**: All operations provide detailed status
- **LED feedback**: Blinks on activity, patterns for save/restore
- **Button reset**: Hardware reset for recovery

### Getting Help
1. **Check build output**: Error messages are usually descriptive
2. **Run tests**: `make test` for hardware validation
3. **Check documentation**: `tests/smarteeprom/README_SMARTEEPROM.md` for details
4. **Verify connections**: UART at 115200 baud, USB connected

## Technical Details

### Memory Usage
- **Single-Game Mode**: ~155KB flash, ~25KB RAM (one game embedded)
- **Multi-Game Mode**: ~430KB flash, ~25KB RAM (4 games embedded)
- **SmartEEPROM**: 16KB allocated, 2-12KB used per save
- **Z-machine Games**: Variable RAM usage based on game complexity
- **Game Storage**: Each game 80-130KB in flash (compressed)
- **Flash Limits**: Conservative 550KB / Aggressive 750KB / Max 950KB game data

### Performance
- **Game Speed**: Real-time Z-machine execution
- **Save Time**: <50ms typical (optimized with 32-bit writes + BUFFERED mode)
- **Restore Time**: <25ms typical (optimized with 32-bit reads)
- **Startup Time**: <1 second from power-on
- **Write Optimization**: 4x faster with 32-bit word access vs byte writes

### Compatibility
- **Z-machine Version 3**: Full compatibility
- **Game Library**: Z-machine Version 3 games
- **Custom Games**: Any valid Z-machine v3 file

## License

BSD-3-Clause. See [../../LICENSE](../../LICENSE).
