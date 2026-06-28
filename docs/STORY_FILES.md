# Story Files

Policy and guidance on Z-machine game files.

## Overview

ZVibe interprets Z-machine version 3 story files (`.z3` extension). These files contain the game data for interactive fiction titles.

## Why Most Story Files Are Not Included

Most story files are not included in this repository. Users provide their own
game files except for the bundled test assets listed below.

## Included Test Assets

The repository includes these story files for testing:

**Czech Z-machine Test Suite**
- Location: `games/catalog/czech.z3`
- License: Public domain
- Purpose: Z-machine conformance testing

This file tests interpreter correctness and is safe to distribute.

**Restaurant**
- Location: `games/catalog/restaurant.z3`
- Purpose: Integrated interpreter regression testing

Restaurant is used as a real-game workload for parser, object-tree,
save/restore, randomized state, and long-session behavior. See
[Restaurant as a ZVibe Test Engine](RESTAURANT_TEST_ENGINE.md).

## Obtaining Story Files

### Zork I, II, and III (Open Source)

In November 2025, Microsoft, Xbox, and Activision [open-sourced the Zork trilogy](https://opensource.microsoft.com/blog/2025/11/20/preserving-code-that-shaped-generations-zork-i-ii-and-iii-go-open-source) under the MIT License. The ZIL source code is available on GitHub:

- [Zork I](https://github.com/historicalsource/zork1)
- [Zork II](https://github.com/historicalsource/zork2)
- [Zork III](https://github.com/historicalsource/zork3)

To play these games, you can:
1. Compile the ZIL source using [ZILF](https://github.com/zilf-heritage/zilf) to produce `.z3` files
2. Purchase ready-to-play versions from [The Zork Anthology on GOG](https://www.gog.com/game/zork_anthology)

### Other Sources

- Original media (floppy disks, CD-ROMs)
- Digital purchases (e.g., GOG, Steam)
- Public domain or freely licensed works
- [Interactive Fiction Archive](https://www.ifarchive.org/)

## Using the Game Tools

The `games/` directory contains tools for managing story files:

```bash
cd games
./get_games.py --help
```

These tools can download files from various sources (see registry.json for configured URLs).

### Registry

`games/registry.json` defines game metadata without including game data.
Downloaded non-bundled games are stored in `games/catalog/` and are not tracked
in git.

### Building for Targets

```bash
cd games
./build_games.py arty_s7_50  # Build for RISC-V (Arty S7-50)
./build_games.py same51   # Build for SAM E51
```

The build system reads from the registry and packages available games.

## Accepted Format

ZVibe supports Z-machine version 3 only.

| Format | Extension | Supported |
|--------|-----------|-----------|
| Z-machine v3 | `.z3` | Yes |
| Z-machine v4+ | `.z4`, `.z5`, etc. | No |
| Blorb | `.zblorb` | No |

## File Locations

| Purpose | Location |
|---------|----------|
| Test files | `games/catalog/` |
| Downloaded games | `games/catalog/` |
| Embedded in firmware | Built from registry |

## Note

This project provides Z-machine interpreter tools. Except for bundled test
assets, game files must be obtained separately.
