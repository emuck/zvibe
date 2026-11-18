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

### Multi-Game Menu (versions 3 and 4)
```bash
cd ../../games
./get_games.py browse                     # Download game collection (version 3 games)
cd ../target/same51
make multi-program                        # Generate the menu from games/registry.json, build, program
```
- **One firmware for both versions**: the menu plays version 3 and version 4 stories (see "The multi-game menu" below)
- **Explicit mode**: a plain `make` is always the single-game firmware; the menu is always `make multi`
- **Button**: the board's button returns to the menu, even from a story waiting for a key or a line
- **Saves that survive an interrupted save**: one area per game and a write-ahead scratch (see "Saves")

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
cd ../../games
./get_games.py browse                      # Download game collection (version 3 games)
cd ../target/same51
make multi-program                         # The registry selection (games/registry.json)

# the version 4 games of the registry (they live outside the catalog, excluded by default):
make multi-program MULTI_ARGS="--only bureaucracy,trinity,amfv,nordandbert,generic-v4"
# or any files, V3 and V4 together, with menu names:
make multi-program MULTI_ARGS="--story Restaurant=../../games/catalog/restaurant.z3 --story Bureaucracy=~/infocom/bureaucracy.z4"

# the single-game firmware again
make STORY_FILE=mygame.z3 program
```

## Features

### Multi-Game System
- **Explicit build**: `make multi` generates the menu and builds it; a plain `make` is the single-game firmware
- **Interactive game menu**: up to nine games, versions 3 and 4 in one menu
- **Button returns to the menu**: from any story, whatever it is waiting for
- **Individual game saves**: each game has its own save area
- **Game library management**: `games/registry.json` and the Python scripts in `games/`, or a list of files
- **Memory efficient**: stories are read in place from flash

### SmartEEPROM Save/Restore
- **Optimized configuration**: SBLK=3, PSZ=7 (512-byte pages) for maximum performance, set automatically (the board resets itself once)
- **Hardware wear leveling** for reliability (214x wear leveling factor)
- **Persistent saves** survive power cycles
- **Version 3 single-game firmware**: one delta-compressed save in the whole SmartEEPROM, with a magic number and a CRC-32
  (written in place, so a power cut during a save loses that save and the one before it; the menu and V4 firmware below do not)
- **Version 4 and menu firmware**: the save store below. Saves are packed against the story in flash (only the bytes the game
  changed, typically 0.4 to 2 KB), and an interrupted save never costs the save before it

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
├── Makefile                           # Build system (single game; `make multi` for the menu)
├── README.md                          # This file  
├── src/                               # Simplified peripheral drivers
│   ├── zvibe_same51.c                 # Single-game implementation
│   ├── story_data.c                   # Generated from the story file (single-game)
│   ├── game_registry.c / .mk          # Generated by games/build_games.py same51 (menu)
│   ├── smarteeprom.c                  # SmartEEPROM as a byte store (word writes, end mark, flush)
│   ├── save_store.c                   # Saves that survive an interrupted save (areas + write-ahead scratch)
│   ├── terminal_io.c                  # Receive ring, key decoding, size probe, button abort (V4 and menu)
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

**Note:** the menu's files (`game_registry.c`, `game_registry.mk`) are generated by `../../games/build_games.py same51` (run by `make multi`) into `src/`. Game files are managed centrally in `../../games/`.

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

## Version 4 firmware

`make v4` builds the single-game firmware with Z-machine V4 support: the story
drives a full-screen terminal (split windows, cursor addressing, styles,
single-key input) through the screen layer in `src/screen`. The default build
stays V3-only and unchanged.

```
make v4-program                                   # Czech (V4) by default
make v4-program STORY_FILE=../console/tests/data/v4screen.z4
python3 tests/test_v4_game.py                     # needs pyserial and pyte
make v4-program STORY_FILE=../console/tests/data/v4save.z4
python3 tests/test_v4_save.py                     # save/restore, also across a reset
make v4-program STORY_FILE=../console/tests/data/v4bigsave.z4
python3 tests/test_v4_bigsave.py                  # a 21 KB save, packed into the SmartEEPROM
```

Connect at 115200 8N1 with an xterm-compatible terminal of at least 56 columns
(the firmware asks the terminal for its size at boot and again each time you press
RETURN to start, so a terminal opened after boot is measured too; if it never
answers, 24x80 is assumed, and a taller window will show stale text below row 24). `V4_RAM` (default 32768)
is the RAM given to the story's dynamic memory; a story needs its dynamic
memory plus about 5 KB. A V4 story's full save can be over 20 KB (Bureaucracy), more
than the 16 KB SmartEEPROM, so the firmware stores it packed against the story in
flash: only the bytes the game has changed (`delta_pack` in `save_delta.c`). If the
changes still do not fit, the save fails cleanly and the previous save is kept.
Save and restore of Bureaucracy has been verified on the board.
`terminal_io.c` holds the interrupt-driven receive ring and key decoding that
the screen demo shares.

### The multi-game menu (versions 3 and 4)

`make multi` builds one firmware with several stories and a menu; `make multi-program` builds and programs it.
There is one firmware and one generator for the menu, whatever mix of versions: the interpreter core plays version 3 and
version 4 stories alike, and the version 4 player (the screen layer, the hardened save code) plays both.

**Which games.** The generator is `games/build_games.py same51` (`games/game_builder/same51.py`); `make multi` runs it.

```
make multi-program                                   # the games selected in games/registry.json
make multi-program MULTI_ARGS="--only bureaucracy,trinity,amfv,nordandbert,generic-v4"
make multi-program MULTI_ARGS="--story Restaurant=../../games/catalog/restaurant.z3 \
                               --story Hitchhiker=$HOME/infocom/hitchhiker.z3 \
                               --story Bureaucracy=$HOME/infocom/library/z4/bureaucracy/bureaucracy-r116-s870602.z4"
```
- `registry.json` is the catalog: each entry has an `id`, a `name`, a story file (in `games/catalog/`, or a `path` elsewhere,
  as the version 4 stories of a personal library are) and `exclude: true` for the ones that are not in the default selection.
  The default selection is packed into the flash budget in registry order; `--only id,id` picks exactly those, in that order,
  even excluded ones. `./game_manager.py` edits the selection. `--story [Menu name=]file` ignores the registry.
- The generator checks every story the way the firmware's loader does (a version 3 or 4 story within the core's size limit,
  header addresses inside the file) and writes `src/game_registry.c` (the stories and the table) and `src/game_registry.mk`
  (story buffer, game count), both carrying one generation id that the Makefile checks. Nothing is rewritten when nothing changed.
- Up to nine games (the menu takes one digit). The story buffer is sized for the largest.

**The TUI.** `games/game_manager.py` is an interactive way to choose the games (`pip install -r ../../games/requirements.txt`
gives the nicer menu; a plain text one is the fallback). It lists every registry game, version 3 and 4, with its size and whether
the story file is present. Type a number to tick or untick a game, `d` downloads missing ones, `b` writes the generated files for
a target, `q` quits. Notes:
- Your choices are saved into `games/registry.json` (the `exclude` flags), so they also set what a plain `make multi` builds.
- The budget line adds up everything ticked, V3 and V4 together, against the story budget (973,824 bytes, about 951 KB). If it is
  over, SAM E51 is not offered as a target: untick games first (the default V3 games are ticked until you change them).
- Quit with `q`: that saves your choices to `games/registry.json`. Then run `make multi-program`, which regenerates `src/game_registry.c`
  and `.mk` from the registry (so it uses what `q` saved; `b` writes those files at once, which is only needed to look at them) and compiles and programs.

**The menu.** Type a number to start a game. RETURN draws the menu again and measures the terminal (do that after
connecting). When a story ends, RETURN brings the menu back. **The board's button** (SW0) returns to the menu from any story,
including one waiting for a key or a line; `~reset~` restarts the story that is running. Games are played at the terminal's
size, measured at the start and at each restart.

**Saves** (`include/save_store.h`, `src/save_store.c`, tested on the host in `tests/host`). The 16 KB SmartEEPROM is divided
into *n* + 1 equal areas, in whole 512-byte pages: one per game and one shared scratch. A save is written to the scratch and read
back, then copied into the game's area and read back again; the board says it is saved only after that. At every start, a copy
that was interrupted (a reset, a power cut) is finished. So the previous save or the new one is always whole, in the game's
area or in the scratch. Every record has a sequence number, a CRC-32 of its data and of its header, and a tag of the story it
belongs to (a CRC of the story's dynamic memory, the bytes the save is packed against), so a save is never given to another
story or release. A save that does not fit its area fails with a message and changes nothing.

| games | save area | largest save |
|---|---|---|
| 1 (a single-game firmware) | 8,192 bytes | 8,164 |
| 3 | 4,096 | 4,068 |
| 5 | 2,560 | 2,532 |
| 9 | 1,536 | 1,508 |

Typical packed saves are 0.4 to 2 KB (1.7 KB for Bureaucracy after a long session). **Changing the menu moves the areas**: adding,
removing or reordering games changes where each game's save lives, so saves made under another menu are not found (they are
ignored, never misread). The first save after a reset can take 2 to 3 seconds; do not reset the board during a save (the store
survives it, but the save in progress is lost).

**Flash.** The SmartEEPROM takes the top of the 1 MB flash, so an image must stay under 999,424 bytes (`FLASH_LIMIT`, given to the
linker: a bigger image does not link). Measured on the board: an image of 998,983 bytes runs and one of 1,013,983 does not
program. The five Infocom version 4 stories (969 KB) and the 23 KB firmware make 992,244 bytes: 7 KB to spare.

**Build and tests.** Settings that change what is compiled (the story buffer, the mode, any flag) are written to
`build_*/.config`, which every object depends on, so changing them rebuilds what they affect.

```
python3 tests/test_multi.py            # the five version 4 stories: starts, quit, a save each, restore after a reset
python3 tests/test_multi_mixed.py      # version 3 and 4 together, Czech (the V3 conformance suite), exact states after restore
make -C tests/host test                # the save store against a model of the SmartEEPROM (power cut before every page commit)
python3 -m unittest discover -s ../../games/tests     # the generator
# with test hooks (EXTRA_CFLAGS=-DSAVE_TEST_HOOKS=1, never a release build): the board is made to stop at each point of a save,
# or to "press" its button on a timer
python3 tests/test_save_power_cut.py   # a power cut at each of five points of a save: the old or the new save comes back whole
python3 tests/test_button.py           # the button at a line prompt, at a key prompt, at the menu
```

## Hardware Requirements

### SAM E51 Curiosity Nano
- **Microcontroller**: ATSAME51J20A (or compatible SAM E51)
- **Flash Memory**: 1MB (sufficient for firmware + SmartEEPROM)
- **RAM**: 256KB (sufficient for Z-machine execution)

### Development Tools
- **XC32 Compiler**: Microchip toolchain (v4.60+ or v5.0+); see the macOS and Linux notes below
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

### Linux Setup

Tested on Ubuntu 24.04 with XC32 v4.60 and v6.25 installed under `/opt/microchip/xc32/`; the Makefile finds the
newest version there on its own (`make XC32_VERSION=v4.60` picks one). Build `edbg` as above (`sudo cp edbg
/usr/local/bin/`) and add a udev rule if `edbg -l` needs `sudo`. The board's serial port is `/dev/ttyACM0` (the
tests in `tests/` look for `/dev/ttyACM*` as well as `/dev/cu.usbmodem*`, or take the port as an argument). Other USB
serial adapters can come first: check `ls /dev/serial/by-id/`. The host tests (`tests/host`, the menu generator)
need only a C compiler and Python 3, and run with `scripts/ci-source-check.sh`.

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

### Build Modes
```bash
make multi                                  # the menu (generates src/game_registry.c and .mk first)
make STORY_FILE=mygame.z3                   # one version 3 game
make v4 STORY_FILE=mygame.z4                # one version 4 game (the full-screen player)
```
The mode is never guessed from the files present: a plain `make` is the single-game firmware, `make multi` the menu.

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
| Save/restore fails | Run the save tests (`tests/test_save_power_cut.py` for the menu firmware, `tests/test_uart.py` for the single-game one) |
| Build errors | Check XC32 compiler installation |
| Programming fails | Verify edbg and SAM E51 connection |
| **Multi-Game Issues** | |
| Menu doesn't appear | Press RETURN (the board draws it before a terminal is connected); check `make multi` found the stories |
| Game selection fails | Type the game's number (1-9); RETURN redraws the menu |
| Button doesn't return to menu | The button is SW0 on the Curiosity Nano (EIC_PIN_15) |
| "not from the same generation" | Run `make multi` again (the two generated files must come from one run) |
| Link error "Unable to find space" | The image is over `FLASH_LIMIT`: take a game out |
| A restore says "[The saved game is damaged]" / "[No saved game found]" | The message says what was found; a menu change moves the saves (see the menu section) |
| **Single-Game Issues** | |
| Custom game not found | Check `STORY_FILE` path is correct and file exists |

### Debug Information
- **UART messages**: All operations provide detailed status
- **LED feedback**: Blinks on activity, patterns for save/restore
- **Button reset**: Hardware reset for recovery

### Getting Help
1. **Check build output**: Error messages are usually descriptive
2. **Run tests**: the board tests in `tests/` (each file says how to build and flash for it); `make -C tests/host test` needs no board
3. **Check documentation**: `tests/smarteeprom/README_SMARTEEPROM.md` for details
4. **Verify connections**: UART at 115200 baud, USB connected

## Technical Details

### Memory Usage
- **Single-Game Mode**: ~155KB flash, ~25KB RAM (one game embedded)
- **Multi-Game Mode**: ~430KB flash, ~25KB RAM (4 games embedded)
- **SmartEEPROM**: 16KB allocated, 2-12KB used per save
- **Z-machine Games**: Variable RAM usage based on game complexity
- **Game Storage**: Each game 80-130KB in flash (stored as is, not compressed)
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

Bundled Czech story data retains its own license; see the root `NOTICE` and
`games/catalog/CZECH-NOTICE.txt`. Include that notice with any distribution
(firmware, flash image or generated array) that contains Czech.
