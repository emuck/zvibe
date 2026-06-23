#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""
ZVibe Unified Game Build System

Builds games for all embedded targets using a common game registry.
Supports: SAM E51, RISC-V Arty S7-50, Console

Usage:
    ./build_games.py same51                    # Build multi-game for SAM E51
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
    """Build multi-game system for SAM E51"""
    registry = common.load_registry()
    output_dir = common.PROJECT_ROOT / TARGET_DIRS['same51']

    if not output_dir.exists():
        print(f"Error: Output directory not found: {output_dir}")
        return 1

    try:
        same51.build_same51(registry, output_dir)
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
  ./build_games.py same51                 # Build all games for SAM E51
  ./build_games.py arty_s7_50            # Build games for Arty S7-50

Or use the interactive game manager:
  ./game_manager.py                      # TUI for selection + building
        """
    )

    parser.add_argument('target', choices=targets,
                        help='Target platform to build for')

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
