#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""
MAX10 08 Eval Board — ZVibe UART game test

Usage:
    python3 test_uart.py                                    # restaurant (default)
    python3 test_uart.py --game plunderedhearts             # Plundered Hearts
    python3 test_uart.py --port /dev/ttyUSB0                # override port
    python3 test_uart.py --auto-program                     # build + flash, then test
    python3 test_uart.py --save-test                        # SAVE/RESTORE profiling
"""

import argparse
import glob
import os
import subprocess
import sys

import serial
import serial.tools.list_ports

TESTS_DIR = os.path.dirname(os.path.abspath(__file__))
BOARD_DIR = os.path.normpath(os.path.join(TESTS_DIR, ".."))
FPGA_DIR = os.path.join(BOARD_DIR, "fpga")

sys.path.insert(0, os.path.join(BOARD_DIR, "../../../../../games/scripts"))
from uart_test_harness import BAUD_RATE, REPO_ROOT, get_game_config, run_game_test


def detect_port():
    """Detect CP2102 USB-to-UART adapter (Silicon Labs 10C4:EA60)."""
    CP2102_VID_PID = (0x10C4, 0xEA60)
    for port in serial.tools.list_ports.comports():
        if port.vid == CP2102_VID_PID[0] and port.pid == CP2102_VID_PID[1]:
            print(f"[ok] Detected CP2102 at {port.device}")
            return port.device
    print("[warn] CP2102 not detected by VID:PID. Trying /dev/ttyUSB0")
    return "/dev/ttyUSB0"


def program_complete(game_name):
    """Build and program MAX10 (CFM + UFM)."""
    catalog = os.path.join(REPO_ROOT, "games", "catalog")
    if not glob.glob(os.path.join(catalog, f"{game_name}*.z3")):
        get_games = os.path.join(REPO_ROOT, "games", "get_games.py")
        print(f"[program] Downloading '{game_name}'...")
        result = subprocess.run([sys.executable, get_games, "download", game_name])
        if result.returncode != 0:
            print(f"[ERROR] Download of '{game_name}' failed")
            sys.exit(1)

    print(f"\n[program] Building + programming MAX10: GAME={game_name}", flush=True)
    result = subprocess.run(
        ["make", "program-complete", f"GAME={game_name}"], cwd=FPGA_DIR)
    if result.returncode != 0:
        print("[ERROR] 'make program-complete' failed")
        sys.exit(1)

    print("[program] Done — waiting 2s for board to reboot...\n", flush=True)
    import time
    time.sleep(2)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="MAX10 ZVibe UART game test")
    parser.add_argument("--port", default=None, help="Serial port (auto-detected if omitted)")
    parser.add_argument("--game", default=None,
                        help="Game: restaurant (default), plunderedhearts, seastalker")
    parser.add_argument("--save-test", action="store_true", help="Run save-size profiling")
    parser.add_argument("--auto-program", action="store_true",
                        help="Build and program MAX10 before testing")
    args = parser.parse_args()

    game_config = get_game_config(args.game or "restaurant")
    game_name = game_config["name"]

    print("=== MAX10 08 Eval — ZVibe UART Test ===")
    print(f"Game: {game_name}")
    if not args.auto_program:
        print("Make sure the FPGA is programmed before running this script.")
        print(f"  cd ../fpga && make program-complete GAME={game_name}")
        print("  (or run with --auto-program to build + flash automatically)")
    print()

    if args.auto_program:
        program_complete(game_name)

    port = args.port or detect_port()
    ser = serial.Serial(port, BAUD_RATE, timeout=0.1)
    try:
        passed, _ = run_game_test(ser, game_config, TESTS_DIR,
                                  save_test=args.save_test)
    finally:
        ser.close()
    sys.exit(0 if passed else 1)
