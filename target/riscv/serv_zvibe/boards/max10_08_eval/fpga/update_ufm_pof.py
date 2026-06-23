#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""
MAX10 UFM POF Generator (Option B Workflow)

Automated workflow to update ONLY UFM user data (firmware + game) by regenerating
a .pof file via quartus_cpf - WITHOUT re-running Quartus compile/fitter.

This is the "Option B" workflow from Intel documentation:
- Uses existing compiled .sof (FPGA configuration)
- Generates new UFM initialization .hex (firmware + game data)
- Creates game-specific .cof conversion file
- Produces game-specific .pof for programming

Usage:
    # Auto-discover inputs and use default game (plunderedhearts)
    ./update_ufm_pof.py

    # Specify game by ID
    ./update_ufm_pof.py --game zork1

    # Specify custom paths
    ./update_ufm_pof.py --game hitchhiker --sof my_custom.sof

Outputs (game-specific naming):
    - ufm_data_<game>.hex    - UFM initialization file
    - pof_gen_<game>.cof     - Quartus conversion file
    - <game>.pof             - Final programming file

Author: Auto-generated for ZVibe MAX10 project
License: BSD-3-Clause
"""

import argparse
import json
import struct
import subprocess
import sys
from pathlib import Path
from typing import Dict, Optional, Tuple
import re


# Repository paths (relative to this script)
SCRIPT_DIR = Path(__file__).parent
COMMON_SW = SCRIPT_DIR / "../../../common/sw"
GAMES_DIR = SCRIPT_DIR / "../../../../../../games"
GAMES_REGISTRY = SCRIPT_DIR / "../../../../../../games/registry.json"
TESTS_GAMES_DIR = SCRIPT_DIR / "../../../../../../tests/games"

# MAX10 UFM configuration
UFM_TOTAL_SIZE = 172 * 1024  # 172KB
UFM_FILL_BYTE = 0xFF         # Erased flash state


def sanitize_filename(name: str) -> str:
    """Sanitize game name for use in filenames."""
    # Remove/replace invalid filename characters
    sanitized = re.sub(r'[^\w\-_]', '_', name.lower())
    # Collapse multiple underscores
    sanitized = re.sub(r'_+', '_', sanitized)
    # Remove leading/trailing underscores
    sanitized = sanitized.strip('_')
    return sanitized


def discover_sof_file() -> Optional[Path]:
    """
    Discover the existing .sof file from the build.

    Checks in order:
    1. build/servant_zvibe_max10_08_eval_xip.sof (make build output)
    2. output_files/servant_zvibe_max10_08_eval_xip.sof (quartus direct output)

    Returns:
        Path to .sof file or None
    """
    candidates = [
        SCRIPT_DIR / "build" / "servant_zvibe_max10_08_eval_xip.sof",
        SCRIPT_DIR / "output_files" / "servant_zvibe_max10_08_eval_xip.sof",
    ]

    for sof_path in candidates:
        if sof_path.exists():
            return sof_path

    return None


def load_game_registry() -> Dict:
    """Load and parse games registry JSON."""
    if not GAMES_REGISTRY.exists():
        print(f"ERROR: Games registry not found: {GAMES_REGISTRY}")
        sys.exit(1)

    with open(GAMES_REGISTRY, 'r') as f:
        return json.load(f)


def find_game_by_id(game_id: str, registry: Dict) -> Optional[Dict]:
    """Find game entry by ID in registry."""
    for game in registry.get('games', []):
        if game.get('id') == game_id:
            return game
    return None


def get_game_path(game: Dict) -> Optional[Path]:
    """
    Get path to game .z3 file.

    Checks:
    1. tests/games/<filename>
    2. games/<filename>
    3. tests/games/<game_id>.z3 (simplified name)
    4. games/<game_id>.z3 (simplified name)

    Returns:
        Path to game file or None
    """
    filename = game.get('filename')
    game_id = game.get('id')

    if not filename:
        return None

    candidates = [
        TESTS_GAMES_DIR / filename,
        GAMES_DIR / filename,
    ]

    # Also try simplified filename (<game_id>.z3)
    if game_id:
        candidates.extend([
            TESTS_GAMES_DIR / f"{game_id}.z3",
            GAMES_DIR / f"{game_id}.z3",
        ])

    for path in candidates:
        if path.exists():
            return path

    return None


def discover_firmware_binaries() -> Tuple[Optional[Path], Optional[Path]]:
    """
    Discover firmware binaries from common/sw.

    Returns:
        Tuple of (boot_stub_path, firmware_path)
    """
    boot_stub = COMMON_SW / "flash_boot_banner.bin"
    firmware = COMMON_SW / "zvibe_riscv_multi.bin"

    return (
        boot_stub if boot_stub.exists() else None,
        firmware if firmware.exists() else None
    )


def calculate_flash_layout(boot_stub_size: int, firmware_size: int) -> Dict:
    """
    Calculate MAX10 flash layout (simplified from calculate_flash_layout.py).

    Args:
        boot_stub_size: Size of boot stub in bytes
        firmware_size: Size of firmware in bytes

    Returns:
        Layout dictionary with offsets and sizes
    """
    layout = {}

    # Metadata (256 bytes)
    layout['metadata_offset'] = 0
    layout['metadata_size'] = 256

    # Boot stub (1KB reserved, 4KB aligned)
    layout['boot_stub_offset'] = 4096  # 4KB boundary
    layout['boot_stub_reserved'] = 1024

    # Firmware (38KB reserved, 4KB aligned)
    firmware_start = layout['boot_stub_offset'] + layout['boot_stub_reserved']
    firmware_start = ((firmware_start + 4095) // 4096) * 4096  # Align to 4KB
    layout['firmware_offset'] = firmware_start
    layout['firmware_reserved'] = 38 * 1024  # 38KB

    # Games (remaining space)
    games_start = firmware_start + layout['firmware_reserved']
    games_start = ((games_start + 255) // 256) * 256  # Align to 256 bytes
    layout['games_offset'] = games_start
    layout['games_available'] = UFM_TOTAL_SIZE - games_start

    # XIP virtual addresses
    layout['xip_base'] = 0x80000000
    layout['firmware_vaddr'] = layout['xip_base'] + layout['firmware_offset']
    layout['games_vaddr'] = layout['xip_base'] + layout['games_offset']

    return layout


def create_metadata_header(layout: Dict, games_size: int) -> bytes:
    """
    Create ZVIF metadata header.

    Format (64 bytes):
        uint32_t magic;              // 'ZVIF' = 0x5A564946
        uint32_t version;            // 1
        uint32_t boot_stub_offset;
        uint32_t boot_stub_size;
        uint32_t firmware_offset;
        uint32_t firmware_size;
        uint32_t games_offset;
        uint32_t games_size;
        uint32_t xip_base;
        uint32_t reserved[7];
    """
    MAGIC = 0x5A564946  # 'ZVIF'
    VERSION = 1

    header = struct.pack('<I', MAGIC)
    header += struct.pack('<I', VERSION)
    header += struct.pack('<I', layout['boot_stub_offset'])
    header += struct.pack('<I', 0)  # boot_stub_size (filled by caller)
    header += struct.pack('<I', layout['firmware_offset'])
    header += struct.pack('<I', 0)  # firmware_size (filled by caller)
    header += struct.pack('<I', layout['games_offset'])
    header += struct.pack('<I', games_size)
    header += struct.pack('<I', layout['xip_base'])
    header += struct.pack('<I', 0) * 7  # reserved

    # Pad to metadata_size
    if len(header) < layout['metadata_size']:
        header += bytes([UFM_FILL_BYTE] * (layout['metadata_size'] - len(header)))

    return header[:layout['metadata_size']]


def generate_ufm_hex(boot_stub_path: Path, firmware_path: Path,
                     game_path: Path, output_hex: Path) -> None:
    """
    Generate Intel HEX file for UFM initialization.

    Creates byte-addressed hex file with:
    - Metadata header
    - Boot stub
    - Firmware
    - Game data (single-game TOC format)
    - Fill unused bytes with 0xFF

    Args:
        boot_stub_path: Path to boot stub binary
        firmware_path: Path to firmware binary
        game_path: Path to game .z3 file
        output_hex: Path to output .hex file
    """
    # Read binaries
    print(f"Reading binaries...")
    with open(boot_stub_path, 'rb') as f:
        boot_stub_data = f.read()
    print(f"  Boot stub:  {len(boot_stub_data):6,} bytes - {boot_stub_path.name}")

    with open(firmware_path, 'rb') as f:
        firmware_data = f.read()
    print(f"  Firmware:   {len(firmware_data):6,} bytes - {firmware_path.name}")

    with open(game_path, 'rb') as f:
        game_z3_data = f.read()
    print(f"  Game:       {len(game_z3_data):6,} bytes - {game_path.name}")

    # Calculate layout
    layout = calculate_flash_layout(len(boot_stub_data), len(firmware_data))

    # Create single-game TOC (8-byte header + game data)
    # Format: uint32_t toc_size, uint32_t game_size, <game_data>
    games_toc = struct.pack('<I', 8)  # TOC size = 8 bytes
    games_toc += struct.pack('<I', len(game_z3_data))
    games_toc += game_z3_data

    # Validate sizes
    if len(boot_stub_data) > layout['boot_stub_reserved']:
        print(f"ERROR: Boot stub ({len(boot_stub_data)} bytes) exceeds "
              f"reserved space ({layout['boot_stub_reserved']} bytes)")
        sys.exit(1)

    if len(firmware_data) > layout['firmware_reserved']:
        print(f"ERROR: Firmware ({len(firmware_data)} bytes) exceeds "
              f"reserved space ({layout['firmware_reserved']} bytes)")
        sys.exit(1)

    if len(games_toc) > layout['games_available']:
        print(f"ERROR: Game data ({len(games_toc)} bytes) exceeds "
              f"available space ({layout['games_available']} bytes)")
        sys.exit(1)

    # Create UFM image
    print(f"\nBuilding UFM image ({UFM_TOTAL_SIZE:,} bytes)...")
    ufm_image = bytearray([UFM_FILL_BYTE] * UFM_TOTAL_SIZE)

    # Write metadata
    metadata = create_metadata_header(layout, len(games_toc))
    # Update actual sizes in metadata
    metadata = bytearray(metadata)
    struct.pack_into('<I', metadata, 12, len(boot_stub_data))   # boot_stub_size
    struct.pack_into('<I', metadata, 20, len(firmware_data))    # firmware_size
    metadata = bytes(metadata)

    ufm_image[layout['metadata_offset']:layout['metadata_offset'] + len(metadata)] = metadata
    print(f"  Metadata:   @ 0x{layout['metadata_offset']:06X} ({len(metadata)} bytes)")

    # Write boot stub
    boot_offset = layout['boot_stub_offset']
    ufm_image[boot_offset:boot_offset + len(boot_stub_data)] = boot_stub_data
    print(f"  Boot stub:  @ 0x{boot_offset:06X} ({len(boot_stub_data)} bytes)")

    # Write firmware
    fw_offset = layout['firmware_offset']
    ufm_image[fw_offset:fw_offset + len(firmware_data)] = firmware_data
    print(f"  Firmware:   @ 0x{fw_offset:06X} ({len(firmware_data)} bytes)")

    # Write games TOC
    games_offset = layout['games_offset']
    ufm_image[games_offset:games_offset + len(games_toc)] = games_toc
    print(f"  Games TOC:  @ 0x{games_offset:06X} ({len(games_toc)} bytes)")

    # Convert to Intel HEX format (byte-addressed)
    print(f"\nGenerating Intel HEX: {output_hex}")

    # Write Intel HEX file (with Extended Linear Address support)
    # Format: :LLAAAATT[DD...]CC
    # LL = byte count, AAAA = address, TT = record type, DD = data, CC = checksum
    with open(output_hex, 'w') as f:
        # Write Extended Linear Address record (required for >64KB addressing)
        # Format: :02 0000 04 AAAA CC
        # Sets upper 16 bits of address to 0x0000
        f.write(":020000040000FA\n")  # Extended Linear Address = 0x0000

        addr = 0
        current_segment = 0  # Track current 64KB segment

        while addr < len(ufm_image):
            # Check if we need a new Extended Linear Address record
            segment = addr >> 16  # Upper 16 bits
            if segment != current_segment:
                # Write Extended Linear Address record
                checksum = 2 + 0 + 0 + 4 + (segment >> 8) + (segment & 0xFF)
                checksum = (~checksum + 1) & 0xFF
                f.write(f":02000004{segment:04X}{checksum:02X}\n")
                current_segment = segment

            # Write in 16-byte chunks (standard Intel HEX)
            chunk_size = min(16, len(ufm_image) - addr)
            chunk_data = ufm_image[addr:addr + chunk_size]

            # Calculate checksum (lower 16 bits of address only)
            addr_low = addr & 0xFFFF
            checksum = chunk_size + (addr_low >> 8) + (addr_low & 0xFF) + 0x00  # record type = 0x00
            for byte in chunk_data:
                checksum += byte
            checksum = (~checksum + 1) & 0xFF  # Two's complement

            # Write data record (using lower 16 bits of address)
            record = f":{chunk_size:02X}{addr_low:04X}00"
            for byte in chunk_data:
                record += f"{byte:02X}"
            record += f"{checksum:02X}\n"
            f.write(record)

            addr += chunk_size

        # Write end-of-file record
        f.write(":00000001FF\n")

    print(f"  Size:       {len(ufm_image):,} bytes")
    print(f"  Format:     Intel HEX (byte-addressed)")


def create_cof_file(sof_path: Path, ufm_hex_path: Path, output_cof: Path,
                    output_pof: Path) -> None:
    """
    Create Quartus .cof conversion file for MAX10 UFM programming.

    Uses MAX10_device_options with ufm_source=2 and ufm_filepath
    to inject UFM initialization data during .sof → .pof conversion.

    Based on working ufm_hello_test.cof template.

    Args:
        sof_path: Path to .sof file
        ufm_hex_path: Path to UFM .hex file
        output_cof: Path to output .cof file
        output_pof: Path to output .pof file
    """
    # MAX10 UFM .cof structure - based on ufm_hello_test.cof (working template)
    # Key elements:
    # - mode=14 (not 7)
    # - MAX10_device_options with ufm_source=2, ufm_filepath
    # - Use absolute paths for reliability
    # Add generation timestamp
    from datetime import datetime
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    template = f"""<?xml version="1.0" encoding="US-ASCII" standalone="yes"?>
<!-- Generated by update_ufm_pof.py on {timestamp} -->
<!-- SOF: {sof_path.name} -->
<!-- UFM: {ufm_hex_path.name} -->
<cof>
\t<output_filename>{output_pof}</output_filename>
\t<n_pages>1</n_pages>
\t<width>1</width>
\t<mode>14</mode>
\t<sof_data>
\t\t<user_name>Page_0</user_name>
\t\t<page_flags>1</page_flags>
\t\t<bit0>
\t\t\t<sof_filename>{sof_path}<compress_bitstream>1</compress_bitstream></sof_filename>
\t\t</bit0>
\t</sof_data>
\t<version>10</version>
\t<create_cvp_file>0</create_cvp_file>
\t<create_hps_iocsr>0</create_hps_iocsr>
\t<auto_create_rpd>0</auto_create_rpd>
\t<rpd_little_endian>1</rpd_little_endian>
\t<options>
\t\t<map_file>1</map_file>
\t</options>
\t<MAX10_device_options>
\t\t<por>0</por>
\t\t<io_pullup>1</io_pullup>
\t\t<config_from_cfm0_only>1</config_from_cfm0_only>
\t\t<isp_source>0</isp_source>
\t\t<verify_protect>0</verify_protect>
\t\t<epof>0</epof>
\t\t<ufm_source>2</ufm_source>
\t\t<ufm_filepath>{ufm_hex_path}</ufm_filepath>
\t</MAX10_device_options>
\t<advanced_options>
\t\t<ignore_epcs_id_check>1</ignore_epcs_id_check>
\t\t<ignore_condone_check>2</ignore_condone_check>
\t\t<plc_adjustment>0</plc_adjustment>
\t\t<post_chain_bitstream_pad_bytes>-1</post_chain_bitstream_pad_bytes>
\t\t<post_device_bitstream_pad_bytes>-1</post_device_bitstream_pad_bytes>
\t\t<bitslice_pre_padding>1</bitslice_pre_padding>
\t</advanced_options>
</cof>
"""

    with open(output_cof, 'w') as f:
        f.write(template)

    print(f"Generated: {output_cof}")


def run_quartus_cpf(cof_path: Path) -> bool:
    """
    Run quartus_cpf to generate .pof file.

    Args:
        cof_path: Path to .cof conversion file

    Returns:
        True if successful, False otherwise
    """
    cmd = ['quartus_cpf', '-c', str(cof_path)]

    print(f"\nRunning: {' '.join(cmd)}")
    print("=" * 70)

    result = subprocess.run(cmd, capture_output=True, text=True)

    # Print output
    print(result.stdout)
    if result.stderr:
        print(result.stderr, file=sys.stderr)

    if result.returncode != 0:
        print("=" * 70)
        print(f"ERROR: quartus_cpf failed with exit code {result.returncode}")
        return False

    print("=" * 70)
    print("SUCCESS: POF file generated")
    return True


def main():
    parser = argparse.ArgumentParser(
        description='MAX10 UFM POF Generator (Option B Workflow)',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Auto-discover and use plunderedhearts (default)
  ./update_ufm_pof.py

  # Generate POF for Zork I
  ./update_ufm_pof.py --game zork1

  # Use custom SOF file
  ./update_ufm_pof.py --game hitchhiker --sof custom.sof

Output files (game-specific naming):
  ufm_data_<game>.hex    - UFM initialization file
  pof_gen_<game>.cof     - Quartus conversion file
  <game>.pof             - Final programming file ready for device
        """
    )

    parser.add_argument('--game', default='plunderedhearts',
                        help='Game ID from registry (default: plunderedhearts)')
    parser.add_argument('--sof', type=Path,
                        help='Path to .sof file (auto-discovered if not specified)')
    parser.add_argument('--boot-stub', type=Path,
                        help='Path to boot stub .bin (auto-discovered if not specified)')
    parser.add_argument('--firmware', type=Path,
                        help='Path to firmware .bin (auto-discovered if not specified)')
    parser.add_argument('--output-dir', type=Path, default=SCRIPT_DIR,
                        help='Output directory (default: current directory)')

    args = parser.parse_args()

    print("=" * 70)
    print("MAX10 UFM POF Generator (Option B)")
    print("=" * 70)
    print()

    # Load game registry
    print("Loading game registry...")
    registry = load_game_registry()

    # Find game
    game = find_game_by_id(args.game, registry)
    if not game:
        print(f"ERROR: Game '{args.game}' not found in registry")
        print(f"\nAvailable games:")
        for g in registry.get('games', []):
            print(f"  {g['id']:20s} - {g['name']}")
        sys.exit(1)

    print(f"Selected game: {game['name']} ({game['id']})")
    print(f"  Size: {game.get('size', 0):,} bytes")
    print()

    # Get game file path
    game_path = get_game_path(game)
    if not game_path:
        print(f"ERROR: Game file not found: {game.get('filename')}")
        print(f"  Searched in: {TESTS_GAMES_DIR}")
        print(f"  Searched in: {GAMES_DIR}")
        sys.exit(1)

    # Discover or use provided SOF
    sof_path = args.sof if args.sof else discover_sof_file()
    if not sof_path or not sof_path.exists():
        print(f"ERROR: .sof file not found")
        print(f"  Searched: build/servant_zvibe_max10_08_eval_xip.sof")
        print(f"  Searched: output_files/servant_zvibe_max10_08_eval_xip.sof")
        print(f"\nRun 'make build' in fpga/ directory first")
        sys.exit(1)

    print(f"Using SOF: {sof_path}")
    print()

    # Discover firmware binaries
    boot_stub_path, firmware_path = discover_firmware_binaries()

    if args.boot_stub:
        boot_stub_path = args.boot_stub
    if args.firmware:
        firmware_path = args.firmware

    if not boot_stub_path or not boot_stub_path.exists():
        print(f"ERROR: Boot stub not found")
        print(f"  Expected: {COMMON_SW}/flash_boot_banner.bin")
        print(f"\nRun 'make flash_boot_banner.bin' in common/sw/ first")
        sys.exit(1)

    if not firmware_path or not firmware_path.exists():
        print(f"ERROR: Firmware not found")
        print(f"  Expected: {COMMON_SW}/zvibe_riscv_multi.bin")
        print(f"\nRun 'make zvibe_riscv_multi.bin' in common/sw/ first")
        sys.exit(1)

    # Generate game-specific output names
    game_safe_name = sanitize_filename(game['id'])
    output_dir = args.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)

    ufm_hex_path = output_dir / f"ufm_data_{game_safe_name}.hex"
    cof_path = output_dir / f"pof_gen_{game_safe_name}.cof"
    pof_path = output_dir / f"{game_safe_name}.pof"

    print(f"Output files:")
    print(f"  UFM HEX: {ufm_hex_path.name}")
    print(f"  COF:     {cof_path.name}")
    print(f"  POF:     {pof_path.name}")
    print()

    # Generate UFM HEX file
    print("=" * 70)
    print("STEP 1: Generate UFM Initialization HEX")
    print("=" * 70)
    generate_ufm_hex(boot_stub_path, firmware_path, game_path, ufm_hex_path)
    print()

    # Create COF file
    print("=" * 70)
    print("STEP 2: Create Quartus Conversion File")
    print("=" * 70)
    create_cof_file(sof_path, ufm_hex_path, cof_path, pof_path)
    print()

    # Generate POF from SOF + UFM HEX
    print("=" * 70)
    print("STEP 3: Generate POF with UFM Data")
    print("=" * 70)
    success = run_quartus_cpf(cof_path)

    if not success:
        sys.exit(1)

    # Verify POF was created
    if not pof_path.exists():
        print(f"ERROR: POF file not created: {pof_path}")
        sys.exit(1)

    pof_size = pof_path.stat().st_size

    # Final summary
    print()
    print("=" * 70)
    print("SUCCESS - POF Generated with UFM Data")
    print("=" * 70)
    print(f"Game:        {game['name']}")
    print(f"Game ID:     {game['id']}")
    print(f"UFM HEX:     {ufm_hex_path.name}")
    print(f"Output POF:  {pof_path.name} ({pof_size:,} bytes)")
    print()
    print("=" * 70)
    print("PROGRAMMING INSTRUCTIONS")
    print("=" * 70)
    print()
    print("Program the device using Quartus Programmer:")
    print()
    print("  quartus_pgm -c 1 -m jtag -o \"p;{pof_path}\"")
    print()
    print("Or use Quartus Programmer GUI:")
    print("  1. Hardware Setup → USB-BlasterII")
    print("  2. Mode → JTAG → Auto Detect")
    print(f"  3. Add File → {pof_path.name}")
    print("  4. Check ☑ UFM in Program/Configure column")
    print("  5. Click Start")
    print()
    print("Then connect to UART to test:")
    print("  picocom -b 115200 /dev/ttyUSB1")
    print()
    print(f"Expected: Game '{game['name']}' auto-launches")
    print("=" * 70)


if __name__ == '__main__':
    main()
