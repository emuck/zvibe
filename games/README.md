# ZVibe Game Management

Game catalog, download tools, and build system for all ZVibe targets.

All games are Z-machine version 3 (`.z3`) files and work on all targets.

## Quick Reference

| Command | Purpose |
|---------|---------|
| `./game_manager.py` | Interactive TUI — select games, see flash budgets, build |
| `./get_games.py list` | Show registry games and flash budgets |
| `./get_games.py browse` | Interactive Infocom catalog browser (download only) |
| `./get_games.py sync` | Regenerate registry from eblong.com catalog |
| `./get_games.py fetch` | Download all games with URLs from registry |
| `./get_games.py scan` | Add unregistered local .z3 files to registry |
| `./build_games.py <target>` | Generate multi-game data for a target |

## Setup

```bash
cd games
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt
```

The venv is already in `.gitignore`. Activate it before running any scripts:
```bash
source venv/bin/activate    # run once per terminal session
```

## Directory Structure

```
games/
├── catalog/          # Bundled test assets plus downloaded .z3 files
│   ├── czech.z3      # Public domain conformance test suite
│   └── restaurant.z3 # Original work, ships with the repo
├── scripts/          # Recorded command scripts for automated testing
├── game_builder/     # Cross-target build library (Python)
├── registry.json     # Game catalog and target flash budgets
├── game_manager.py   # Interactive TUI for game selection and building
├── get_games.py      # Game download and registry management
└── build_games.py    # CLI build system (for scripting/CI)
```

## Game Manager TUI

The recommended way to manage games:

```bash
./game_manager.py
```

Shows all games with selection state and live flash budgets for every target.
Toggle games on/off with space, download with `d`, build with `b`, sync with `s`.

Games marked `[X]` are selected for builds. Deselected games get an `exclude`
flag in `registry.json` that all build tools respect.

## Getting Games

```bash
# Option 1: Use the game manager TUI (recommended)
./game_manager.py

# Option 2: Interactive browser (fetches full Infocom catalog from eblong.com)
./get_games.py browse

# Option 3: Download games already in the registry
./get_games.py fetch

# Import unregistered .z3 files from catalog/ to registry
./get_games.py scan

# Check what fits on each target
./get_games.py list
```

`browse` mode smart-filters to one version per game (Masterpieces edition first,
then final-dev, then latest release), shows file sizes and running totals, and
downloads selected games to `catalog/`.

## Building for Targets

### RISC-V (MAX10, Arty S7-50)

For RISC-V boards, use the board-specific firmware build system which calls
`build_flash.py` internally:

```bash
# MAX10 — build firmware + flash image + program in one step
cd ../target/riscv/serv_zvibe/boards/max10_08_eval/fpga
make program-complete GAME=hitchhiker    # builds and programs

# Arty S7-50
cd ../target/riscv/serv_zvibe/boards/arty_s7_50/sw
make flash-test GAME=zork1
```

### SAM E51

```bash
./build_games.py same51
cd ../target/same51 && make all && make program
```

Generates per-game C arrays and a game registry in `target/same51/src/`. All
selected games compile into a single firmware image (~768KB available).

### Console

```bash
cd ../target/console && make all
./build/bin/zvibe_console ../games/catalog/restaurant.z3
```

No size limit — games are loaded from disk at runtime.

## Registry Format

`registry.json` defines game metadata without including game data.

```json
{
  "id": "zork1",
  "name": "Zork I: The Great Underground Empire",
  "filename": "zork.z3",
  "size": 84876,
  "priority": 1,
  "url": "https://example.com/my-game.z3"
}
```

Optional fields:
- `default` — marks the default game for single-game builds
- `url` — enables `./get_games.py fetch`
- `exclude` — set by game manager to deselect from builds

## Adding Games

```bash
# 1. Browse and download
./get_games.py browse

# 2. Import to registry
./get_games.py scan -y

# 3. Verify
./get_games.py list
```

To add a custom game manually, copy the `.z3` file to `catalog/` and add an
entry to `registry.json` without a `url` field.

## Copyright

Most game files are not included in the repository. Infocom games retain their
copyright and must be obtained from legitimate sources. The Zork trilogy source
was open-sourced by Activision/Xbox under MIT License in November 2025 — see
`docs/STORY_FILES.md` for details.

The ZVibe interpreter is BSD-3-Clause. Game files are separate works.
