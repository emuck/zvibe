#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""
Arty S7-50 — ZVibe UART game test

Usage:
    python3 test_uart.py                                    # restaurant (default)
    python3 test_uart.py --game plunderedhearts             # Plundered Hearts
    python3 test_uart.py --game seastalker                  # Seastalker
    python3 test_uart.py --port /dev/ttyUSB1                # override port
    python3 test_uart.py --auto-program                     # build + flash, then test
    python3 test_uart.py --auto-program --game seastalker
    python3 test_uart.py --save-test                        # SAVE/RESTORE profiling
"""

import argparse
import os
import subprocess
import sys

import serial
import serial.tools.list_ports

TESTS_DIR = os.path.dirname(os.path.abspath(__file__))
BOARD_DIR = os.path.normpath(os.path.join(TESTS_DIR, ".."))
SW_DIR = os.path.join(BOARD_DIR, "sw")
FPGA_DIR = os.path.join(BOARD_DIR, "fpga")

sys.path.insert(0, os.path.join(BOARD_DIR, "../../../../../games/scripts"))
from uart_test_harness import BAUD_RATE, get_game_config, run_game_test


def detect_port():
    """Detect FTDI FT2232H UART channel (Arty S7-50 VID:PID 0403:6010).

    The FT2232H has two channels: A (JTAG) and B (UART).
    UART is the higher-numbered ttyUSB port.
    """
    FTDI_VID_PID = [(0x0403, 0x6010), (0x0403, 0x6014)]
    detected = []
    for port in serial.tools.list_ports.comports():
        if port.vid and port.pid and (port.vid, port.pid) in FTDI_VID_PID:
            detected.append(port.device)
    if detected:
        uart_port = sorted(detected)[-1]
        print(f"[ok] Detected Arty S7-50 FTDI at: {uart_port}")
        return uart_port
    print("[warn] FTDI FT2232H not detected by VID:PID.")
    print("[warn] Try: ls /dev/ttyUSB*  and pass --port explicitly")
    return "/dev/ttyUSB1"


def program_flash(game_name):
    """Build flash image and program Arty (user flash + SRAM reload)."""
    print(f"\n[program] Building flash image: GAME={game_name}")
    result = subprocess.run(
        ["make", "flash-test", f"GAME={game_name}"], cwd=SW_DIR)
    if result.returncode != 0:
        print(f"[ERROR] 'make flash-test GAME={game_name}' failed")
        sys.exit(1)

    print("[program] Programming user flash region...")
    result = subprocess.run(["make", "program-flash"], cwd=FPGA_DIR)
    if result.returncode != 0:
        print("[ERROR] 'make program-flash' failed")
        sys.exit(1)

    print("[program] Reloading ZVibe bitstream into FPGA SRAM...")
    result = subprocess.run(["make", "program-sram"], cwd=FPGA_DIR)
    if result.returncode != 0:
        print("[ERROR] 'make program-sram' failed")
        sys.exit(1)

    print("[program] Done — waiting 2s for board to reboot...\n")
    import time
    time.sleep(2)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Arty S7-50 ZVibe UART game test")
    parser.add_argument("--port", default=None, help="Serial port (auto-detected if omitted)")
    parser.add_argument("--game", default=None,
                        help="Game: restaurant (default), plunderedhearts, seastalker")
    parser.add_argument("--save-test", action="store_true", help="Run save-size profiling")
    parser.add_argument("--auto-program", action="store_true",
                        help="Build flash image and program before testing")
    args = parser.parse_args()

    game_config = get_game_config(args.game or "restaurant")
    game_name = game_config["name"]

    print("=== Arty S7-50 — ZVibe UART Test ===")
    print(f"Game: {game_name}")
    if not args.auto_program:
        print("Make sure the FPGA is programmed before running this script.")
        print(f"  cd ../fpga && make program-complete GAME={game_name}")
        print("  (or run with --auto-program to flash automatically)")
    print()

    if args.auto_program:
        program_flash(game_name)

    port = args.port or detect_port()
    ser = serial.Serial(port, BAUD_RATE, timeout=0.1)
    try:
        passed, _ = run_game_test(ser, game_config, TESTS_DIR,
                                  save_test=args.save_test)
    finally:
        ser.close()
    sys.exit(0 if passed else 1)
