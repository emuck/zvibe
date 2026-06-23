#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""Shared ZVIF/ZVGM serialization for ZVibe flash image builders.

Constants and serialization functions for the two on-flash metadata
structures that the shared firmware (zvibe_riscv_multi.c) reads at boot:

  - ZVIF header (256 bytes @ flash offset 0)
    Platform metadata + offsets for firmware, games, and saves.
    Validated by flash_metadata.c : flash_metadata_init().

  - ZVGM TOC (16 + 80*N bytes)
    Game table of contents: offset, size, id, name per game.
    Read by game_registry_riscv.c : load_game_registry_from_flash().

Board-specific builders (boards/*/build_flash.py) import this module
and supply their own layout-calculation logic.
"""

import struct

# ---------------------------------------------------------------------------
# Magic numbers — must match C definitions
# ---------------------------------------------------------------------------
# flash_metadata.h : ZVIF_MAGIC
ZVIF_MAGIC = 0x46495A56   # b'ZVIF' little-endian

# game_registry_riscv.h : FLASH_TOC_MAGIC
ZVGM_MAGIC = 0x4D47565A   # b'ZVGM' little-endian

# ZVIF header is always exactly 256 bytes (remainder is reserved/padding)
METADATA_SIZE = 256


def create_metadata(layout, games_size=0, platform='unknown'):
    """Serialize a ZVIF header — layout matches zvif_header_t exactly.

    layout keys required:
        total_size, firmware_offset, firmware_size, firmware_vaddr,
        firmware_reserved, toc_offset, toc_size, games_data_offset,
        saves_offset, saves_size, save_slot_size, save_slot_count

    Args:
        layout:      Layout dict (produced by the board builder).
        games_size:  Total bytes of game payload (goes into games_data_size).
        platform:    Short tag written into platform[16] (e.g. 'max10').

    Returns:
        bytes of exactly METADATA_SIZE length.
    """
    header = bytearray(METADATA_SIZE)

    # Header block (16 bytes)
    struct.pack_into('<I', header,  0, ZVIF_MAGIC)
    struct.pack_into('<I', header,  4, 1)                         # version
    struct.pack_into('<I', header,  8, layout['total_size'])      # flash_size
    struct.pack_into('<I', header, 12, METADATA_SIZE)             # header_size

    # Firmware region (16 bytes)
    struct.pack_into('<I', header, 16, layout['firmware_offset'])
    struct.pack_into('<I', header, 20, layout['firmware_size'])
    struct.pack_into('<I', header, 24, layout['firmware_vaddr'])
    struct.pack_into('<I', header, 28, layout['firmware_reserved'])

    # Games region (16 bytes)
    struct.pack_into('<I', header, 32, layout['toc_offset'])
    struct.pack_into('<I', header, 36, layout['toc_size'])
    struct.pack_into('<I', header, 40, layout['games_data_offset'])
    struct.pack_into('<I', header, 44, games_size)

    # Save slots region (16 bytes)
    struct.pack_into('<I', header, 48, layout['saves_offset'])
    struct.pack_into('<I', header, 52, layout['saves_size'])
    struct.pack_into('<I', header, 56, layout['save_slot_size'])
    struct.pack_into('<I', header, 60, layout['save_slot_count'])

    # Platform tag (16 bytes @ offset 64)
    tag = platform.encode('utf-8')[:15]
    header[64:64 + len(tag)] = tag

    return bytes(header)


def create_toc(layout, games):
    """Serialize a ZVGM TOC — layout matches flash_toc_t exactly.

    layout keys required:
        games_data_vaddr, toc_size

    Args:
        layout: Layout dict (produced by the board builder).
        games:  List of dicts, each containing:
                  'data'  – raw game bytes
                  'id'    – short ASCII id  (max 15 chars)
                  'name'  – display name    (max 47 chars)

    Returns:
        bytearray of exactly layout['toc_size'] bytes.
    """
    toc = bytearray(layout['toc_size'])

    # Header (16 bytes)
    struct.pack_into('<I', toc, 0, ZVGM_MAGIC)
    struct.pack_into('<I', toc, 4, 2)                  # version
    struct.pack_into('<I', toc, 8, len(games))         # game_count
    struct.pack_into('<I', toc, 12, 0)                 # reserved

    # Entries (80 bytes each — v2)
    data_vaddr = layout['games_data_vaddr']
    for i, game in enumerate(games):
        off = 16 + i * 80

        struct.pack_into('<I', toc, off + 0, data_vaddr)        # vaddr
        struct.pack_into('<I', toc, off + 4, len(game['data'])) # size

        # id[16] @ +8
        gid = game['id'].encode('utf-8')[:15]
        toc[off + 8:off + 8 + len(gid)] = gid

        # name[48] @ +24
        name = game['name'].encode('utf-8')[:47]
        toc[off + 24:off + 24 + len(name)] = name

        # saves_offset[4] @ +72, save_slot_count[4] @ +76
        struct.pack_into('<I', toc, off + 72, game.get('saves_offset', 0))
        struct.pack_into('<I', toc, off + 76, game.get('save_slot_count', 0))

        data_vaddr += len(game['data'])

    return toc
