#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""Build Arty S7-50 flash image — ZVIF-based user region.

The Arty S7-50 QSPI flash (16MB S25FL128S) is shared between the FPGA
bitstream and user code.  The bitstream occupies physical 0x000000 onward;
the user region starts at physical 0x100000 (XIP virtual 0x80100000).

Layout within the user region (all offsets are absolute from QSPI physical 0):

  Physical    Virtual        Size      Component
  ─────────────────────────────────────────────────────────
  0x100000    0x80100000     256 B     ZVIF metadata header
  0x100100    0x80100100     ~28 KB    Firmware (reset_pc entry)
  toc_off     0x80000000+toc_off  varies  ZVGM TOC (header + N entries)
  data_off    0x80000000+data_off  varies  Game data (concatenated)
  saves_off   0x80000000+saves_off  64KB×N  Save slots (64KB sectors)
  ─────────────────────────────────────────────────────────
  Total user region: 15MB (0x100000..0xFFFFFF)

The output binary (arty_flash.bin) is the user region only.
Program it to QSPI at physical offset 0x100000, after the bitstream.

Usage:
  python3 build_flash.py --firmware <fw.bin> --game <game.z3> \\
                         --game-id <id> --game-name <name> --output <out.bin>
  python3 build_flash.py --firmware <fw.bin> --games-from-registry \\
                         --registry <registry.json> --catalog-dir <dir> \\
                         --output <out.bin>
  python3 build_flash.py --firmware <fw.bin> --layout-only
"""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'common' / 'sw'))
from zvibe_flash_lib import create_metadata, create_toc, METADATA_SIZE  # noqa: E402

# ---------------------------------------------------------------------------
# Arty-specific constants
# ---------------------------------------------------------------------------
QSPI_TOTAL      = 16 * 1024 * 1024   # 16MB total QSPI flash
USER_START      = 0x100000            # Physical offset where user region begins
USER_SIZE       = QSPI_TOTAL - USER_START   # 15MB user region
XIP_BASE        = 0x80000000          # CPU XIP base (covers all QSPI from physical 0)
FW_ENTRY_OFFSET = 0x100               # Firmware starts 256 B after user region start
                                      # (ZVIF header occupies 0x100000..0x1000FF)
SLOT_SIZE       = 65536               # 64KB — matches S25FL128S erase granularity


def calculate_layout(firmware_size, games_size=0, num_games=1):
    """Compute Arty layout from actual binary sizes.

    All offsets are absolute from QSPI physical 0 (matching XIP convention).
    """
    layout = {}
    layout['total_size'] = QSPI_TOTAL
    layout['xip_base']   = XIP_BASE

    # ZVIF metadata: at user region start (absolute physical)
    layout['metadata_offset'] = USER_START
    layout['metadata_size']   = METADATA_SIZE

    # Firmware: immediately after ZVIF header
    fw_start    = USER_START + FW_ENTRY_OFFSET
    fw_reserved = ((firmware_size + 255) // 256) * 256   # round up to 256-B

    layout['firmware_offset']   = fw_start
    layout['firmware_size']     = firmware_size
    layout['firmware_reserved'] = fw_reserved
    layout['firmware_vaddr']    = XIP_BASE + fw_start    # 0x80100100

    # TOC immediately after firmware (absolute)
    toc_start = fw_start + fw_reserved
    toc_size  = 16 + num_games * 80

    layout['toc_offset'] = toc_start
    layout['toc_size']   = toc_size

    # Game data immediately after TOC (absolute)
    gd_start = toc_start + toc_size
    layout['games_data_offset'] = gd_start
    layout['games_data_vaddr']  = XIP_BASE + gd_start

    # Save slots at next 64KB-aligned address after game data (absolute)
    # Per-game allocation: SAVES_PER_GAME slots per game, stored in TOC entries
    SAVES_PER_GAME = 4
    saves_start = ((gd_start + games_size + SLOT_SIZE - 1) // SLOT_SIZE) * SLOT_SIZE
    total_save_size = num_games * SAVES_PER_GAME * SLOT_SIZE

    # Global ZVIF saves fields are superseded by per-entry values
    layout['saves_offset']    = 0
    layout['saves_size']      = 0
    layout['save_slot_size']  = SLOT_SIZE
    layout['save_slot_count'] = 0
    layout['games_available'] = QSPI_TOTAL - gd_start - total_save_size

    # Per-game save info used when populating TOC entries
    layout['per_game_saves_start'] = saves_start
    layout['per_game_saves_count'] = SAVES_PER_GAME

    return layout


def print_layout(layout, fw_size, gd_size, games):
    """Pretty-print the computed layout."""
    print()
    print('=' * 70)
    print('ARTY S7-50 FLASH LAYOUT')
    print('=' * 70)
    print(f"  Total QSPI:   {QSPI_TOTAL // 1024 // 1024} MB")
    print(f"  User region:  {USER_SIZE // 1024 // 1024} MB  (physical 0x{USER_START:06X} onward)")
    print()
    print(f"  Metadata:     {METADATA_SIZE:7,} B  @ phys 0x{layout['metadata_offset']:07X}"
          f"  (XIP 0x{XIP_BASE + layout['metadata_offset']:08X})")
    print(f"  Firmware:     {fw_size:7,} B  @ phys 0x{layout['firmware_offset']:07X}"
          f"  (XIP 0x{layout['firmware_vaddr']:08X})")
    if games:
        print(f"  TOC:          {layout['toc_size']:7,} B  @ phys 0x{layout['toc_offset']:07X}"
              f"  ({len(games)} game(s))")
        print(f"  Games:        {gd_size:7,} B  @ phys 0x{layout['games_data_offset']:07X}"
              f"  (XIP 0x{layout['games_data_vaddr']:08X})")
        for g in games:
            print(f"                  \"{g['name']}\"  ({len(g['data']):,} B)")
    else:
        print(f"  Games:        {layout['games_available']:7,} B  available")
    saves_count = layout.get('per_game_saves_count', 0)
    saves_start = layout.get('per_game_saves_start', 0)
    if saves_count > 0 and saves_start > 0:
        total_slots = saves_count * max(1, len(games))
        total_kb    = total_slots * layout['save_slot_size'] // 1024
        print(f"  Saves:        {saves_count:7,} slots/game ({total_slots} total)"
              f" @ phys 0x{saves_start:07X}"
              f"  ({total_kb:,} KB, per-game)")
    else:
        print(f"  Saves:        none (no remaining space)")
    print('=' * 70)
    print(f"  Output binary programs to QSPI at physical offset 0x{USER_START:06X}")
    print()


def generate_flash_layout_h(layout, output_dir):
    """Write flash_layout.h consumed by firmware builds."""
    (output_dir / 'flash_layout.h').write_text(
        f"/* Auto-generated by boards/arty_s7_50/build_flash.py  DO NOT EDIT */\n"
        f"\n"
        f"#ifndef FLASH_LAYOUT_H\n"
        f"#define FLASH_LAYOUT_H\n"
        f"\n"
        f"/* Platform: Arty S7-50   Total QSPI: {QSPI_TOTAL // 1024 // 1024} MB */\n"
        f"/* User region starts at physical 0x{USER_START:06X} */\n"
        f"\n"
        f"/* Physical offsets (absolute from QSPI 0) */\n"
        f"#define FLASH_METADATA_OFFSET  0x{layout['metadata_offset']:08X}\n"
        f"#define FLASH_FIRMWARE_OFFSET  0x{layout['firmware_offset']:08X}\n"
        f"#define FLASH_GAMES_OFFSET     0x{layout['games_data_offset']:08X}\n"
        f"\n"
        f"/* XIP virtual addresses (CPU view) */\n"
        f"#define XIP_BASE               0x{XIP_BASE:08X}\n"
        f"#define XIP_FIRMWARE_ADDR      0x{layout['firmware_vaddr']:08X}\n"
        f"#define XIP_GAMES_ADDR         0x{layout['games_data_vaddr']:08X}\n"
        f"\n"
        f"/* Capacity */\n"
        f"#define FIRMWARE_RESERVED      {layout['firmware_reserved']}\n"
        f"#define GAMES_AVAILABLE        {layout['games_available']}\n"
        f"\n"
        f"/* Save Region (per-game; offsets stored in TOC entries, not globally) */\n"
        f"#define FLASH_SAVES_OFFSET     0x{layout.get('per_game_saves_start', 0):08X}\n"
        f"#define FLASH_SAVES_SIZE       0\n"
        f"#define FLASH_SAVE_SLOT_SIZE   {layout['save_slot_size']}\n"
        f"#define FLASH_SAVE_SLOT_COUNT  {layout.get('per_game_saves_count', 0)}\n"
        f"\n"
        f"#endif /* FLASH_LAYOUT_H */\n")
    print(f"Generated: {output_dir / 'flash_layout.h'}")


def load_games_from_registry(registry_path, catalog_dir):
    """Return list of {'data', 'id', 'name'} for all downloaded games in registry."""
    with open(registry_path) as f:
        registry = json.load(f)

    catalog = Path(catalog_dir)
    games = []
    for g in registry['games']:
        if g.get('exclude', False):
            continue
        z3_path = catalog / g['filename']
        if not z3_path.exists():
            continue
        data = z3_path.read_bytes()
        games.append({'data': data, 'id': g['id'], 'name': g['name']})
        print(f"  + {g['id']:30}  {len(data):7,} B  ({len(data)//1024} KB)")

    if not games:
        print("ERROR: No game files found in catalog")
        print("  Run: python3 games/get_games.py fetch")
        sys.exit(1)

    return games


def main():
    parser = argparse.ArgumentParser(
        description='Build Arty S7-50 ZVIF flash image (user region, programs at 0x100000)')
    parser.add_argument('--firmware', required=True,
                        help='Firmware binary (linked @ 0x80100100)')
    # Single-game mode
    parser.add_argument('--game',
                        help='Raw game file (.z3)')
    parser.add_argument('--game-id',   default='game',
                        help='Game ID for TOC entry (single-game mode)')
    parser.add_argument('--game-name', default='Game',
                        help='Display name for TOC entry (single-game mode)')
    # Multi-game mode
    parser.add_argument('--games-from-registry', action='store_true',
                        help='Include all downloaded games from registry.json')
    parser.add_argument('--registry',
                        help='Path to registry.json (default: auto-detect from script location)')
    parser.add_argument('--catalog-dir',
                        help='Path to games/catalog/ directory (default: auto-detect)')
    # Common
    parser.add_argument('--output',
                        help='Output user-region binary (required unless --layout-only)')
    parser.add_argument('--layout-only', action='store_true',
                        help='Print layout and exit; do not write image')
    args = parser.parse_args()

    firmware = Path(args.firmware)
    if not firmware.exists():
        print(f"ERROR: Firmware not found: {firmware}")
        sys.exit(1)

    # Resolve registry and catalog paths (default: relative to this script)
    script_dir = Path(__file__).resolve().parent
    repo_root  = script_dir.parents[4]   # boards/arty_s7_50 → serv_zvibe → riscv → target → repo
    default_registry = repo_root / 'games' / 'registry.json'
    default_catalog  = repo_root / 'games' / 'catalog'

    registry_path = Path(args.registry) if args.registry else default_registry
    catalog_dir   = Path(args.catalog_dir) if args.catalog_dir else default_catalog

    # Build game list
    if args.games_from_registry:
        if not registry_path.exists():
            print(f"ERROR: Registry not found: {registry_path}")
            sys.exit(1)
        print(f"\nLoading games from registry: {registry_path}")
        games_list = load_games_from_registry(registry_path, catalog_dir)
    elif args.game:
        game = Path(args.game)
        if not game.exists():
            print(f"WARNING: Game not found: {game}  (continuing without game)")
            game = None
        games_list = [{'data': game.read_bytes(), 'id': args.game_id, 'name': args.game_name}] if game else []
    else:
        games_list = []

    if args.layout_only:
        fw_data  = firmware.read_bytes()
        total_gd = sum(len(g['data']) for g in games_list)
        layout   = calculate_layout(len(fw_data), total_gd, num_games=max(1, len(games_list)))
        print_layout(layout, len(fw_data), total_gd, games_list)
        return

    if not args.output:
        print("ERROR: --output is required (use --layout-only to just print layout)")
        sys.exit(1)

    _build_flash_image_multi(firmware, games_list, Path(args.output))


def _build_flash_image_multi(firmware_path, games, output_path):
    """Assemble multi-game flash image from a pre-loaded games list."""
    print(f"\nReading firmware: {firmware_path}")
    fw_data = firmware_path.read_bytes()
    print(f"  {len(fw_data):,} bytes ({len(fw_data) / 1024:.1f} KB)")

    total_gd = sum(len(g['data']) for g in games)
    layout   = calculate_layout(len(fw_data), total_gd, num_games=max(1, len(games)))

    if len(fw_data) > layout['firmware_reserved']:
        print(f"ERROR: Firmware ({len(fw_data):,} B) > reserved ({layout['firmware_reserved']:,} B)")
        sys.exit(1)

    if total_gd > layout['games_available']:
        print(f"ERROR: Games ({total_gd:,} B) > available ({layout['games_available']:,} B)")
        sys.exit(1)

    # Attach per-game save info to each game dict for create_toc()
    saves_per_game = layout['per_game_saves_count']
    game_saves_start = layout['per_game_saves_start']
    for i, game in enumerate(games):
        game['saves_offset']    = game_saves_start + i * saves_per_game * SLOT_SIZE
        game['save_slot_count'] = saves_per_game

    image_size = QSPI_TOTAL - USER_START
    image = bytearray(b'\xff' * image_size)

    def poff(absolute_physical):
        return absolute_physical - USER_START

    image[poff(layout['metadata_offset']):poff(layout['metadata_offset']) + METADATA_SIZE] = \
        create_metadata(layout, total_gd, platform='arty')
    print(f"  Metadata @ phys 0x{layout['metadata_offset']:07X}")

    off = poff(layout['firmware_offset'])
    image[off:off + len(fw_data)] = fw_data
    print(f"  Firmware @ phys 0x{layout['firmware_offset']:07X}")

    toc  = create_toc(layout, games)
    toff = poff(layout['toc_offset'])
    image[toff:toff + len(toc)] = toc
    print(f"  TOC      @ phys 0x{layout['toc_offset']:07X}  ({len(games)} games)")

    goff = poff(layout['games_data_offset'])
    for g in games:
        image[goff:goff + len(g['data'])] = g['data']
        goff += len(g['data'])

    output_path.write_bytes(image)
    print_layout(layout, len(fw_data), total_gd, games)
    print(f"Wrote: {output_path}")
    print(f"  → Program to QSPI at physical offset 0x{USER_START:06X}")

    layout_json = output_path.parent / 'arty_layout.json'
    with open(layout_json, 'w') as f:
        json.dump(layout, f, indent=2)
    print(f"Layout JSON: {layout_json}")

    generate_flash_layout_h(layout, output_path.parent)


if __name__ == '__main__':
    main()
