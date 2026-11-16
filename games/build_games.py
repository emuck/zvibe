#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""
ZVibe Unified Game Build System

Builds games for all embedded targets using a common game registry.
Supports: SAM E51, RISC-V Arty S7-50, Console

Usage:
    ./build_games.py same51                    # Build the multi-game menu (versions 3 and 4) for SAM E51
    ./build_games.py arty_s7_50                # Build multi-game for RISC-V
    ./build_games.py arty_s7_50 --game zork1   # Build single game (no menu)
"""

import argparse
import sys
from pathlib import Path

# Add game_builder to path
sys.path.insert(0, str(Path(__file__).parent))

from game_builder import common, same51


# Map target names to output directories (relative to PROJECT_ROOT)
TARGET_DIRS = {
    'same51':        'target/same51/src',
    'arty_s7_50':    'target/riscv/serv_zvibe/boards/arty_s7_50/sw',
}


def build_for_same51(args):
    """Build the multi-game firmware's game table for SAM E51 (versions 3 and 4)"""
    registry = common.load_registry()
    output_dir = Path(args.out) if args.out else common.PROJECT_ROOT / TARGET_DIRS['same51']

    if not output_dir.exists():
        print(f"Error: Output directory not found: {output_dir}")
        return 1

    try:
        games = same51.games_from_files(args.story) if args.story else None
        only = args.only.split(',') if args.only else None
        same51.build_same51(registry, output_dir, games, only)
        return 0
    except Exception as e:
        print(f"\nError: Build failed: {e}")
        return 1


def main():
    registry = common.load_registry()
    targets = list(registry.get('targets', {}).keys())

    parser = argparse.ArgumentParser(
        description='ZVibe Unified Game Build System',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  ./build_games.py same51                 # Build the SAM E51 menu from the registry selection
  ./build_games.py same51 --story a.z3 --story Bureaucracy=b.z4   # or from files (versions 3 and 4)
  ./build_games.py arty_s7_50            # Build games for Arty S7-50

Or use the interactive game manager:
  ./game_manager.py                      # TUI for selection + building
        """
    )

    parser.add_argument('target', choices=targets,
                        help='Target platform to build for')
    parser.add_argument('--story', action='append', metavar='[NAME=]FILE',
                        help='same51: use these story files (.z3 or .z4) instead of the registry selection; '
                             'repeat for each game, NAME= sets the menu name')
    parser.add_argument('--only', metavar='ID,ID',
                        help='same51: exactly these registry games, in this order (even ones excluded by default)')
    parser.add_argument('--out', metavar='DIR',
                        help='same51: write the generated files here (default: target/same51/src)')

    args = parser.parse_args()

    if args.target == 'same51':
        return build_for_same51(args)
    else:
        # Generic build: just select games that fit the target's flash budget
        try:
            games = common.get_downloaded_games(args.target, registry)
            print(f"\n{len(games)} games selected for {args.target}:")
            for g in games:
                print(f"  {g['id']:<20} {g['size'] // 1024:>4}KB  {g['name']}")
        except Exception as e:
            print(f"\nError: {e}")
            return 1
        return 0


if __name__ == '__main__':
    sys.exit(main())
