#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""
SAM E51 — ZVibe UART game test

Usage:
    python3 test_uart.py                              # restaurant (default)
    python3 test_uart.py --game plunderedhearts       # Plundered Hearts
    python3 test_uart.py --game seastalker            # Seastalker
    python3 test_uart.py --auto-program               # build, program, then test
    python3 test_uart.py --port /dev/cu.usbmodem14102
"""

import argparse
import os
import platform
import subprocess
import sys
import urllib.request
import urllib.error

import serial
import serial.tools.list_ports

TESTS_DIR = os.path.dirname(os.path.abspath(__file__))
TARGET_DIR = os.path.abspath(os.path.join(TESTS_DIR, ".."))

sys.path.insert(0, os.path.join(TARGET_DIR, "../../games/scripts"))
from uart_test_harness import BAUD_RATE, REPO_ROOT, get_game_config, run_game_test


def detect_port():
    """Auto-detect SAM E51 Curiosity Nano serial port by VID:PID."""
    sam_e51_ids = {(0x03eb, 0x2111), (0x03eb, 0x2175)}
    for p in serial.tools.list_ports.comports():
        if p.vid and p.pid and (p.vid, p.pid) in sam_e51_ids:
            print(f"[ok] Detected SAM E51 at {p.device}")
            return p.device
    defaults = {"darwin": "/dev/cu.usbmodem14201",
                "linux":  "/dev/ttyACM0",
                "windows": "COM3"}
    default = defaults.get(platform.system().lower(), "/dev/ttyACM0")
    print(f"[warn] SAM E51 not auto-detected, using default: {default}")
    return default


def build_and_program(game_name, game_config):
    """Clean-build and program the SAM E51 with the specified game."""
    game_file = game_config.get("file", f"{game_name}.z3")
    game_path = os.path.join(REPO_ROOT, "games/catalog", game_file)

    if not os.path.exists(game_path) and "url" in game_config:
        print(f"Downloading {game_file}...")
        os.makedirs(os.path.dirname(game_path), exist_ok=True)
        try:
            urllib.request.urlretrieve(game_config["url"], game_path)
            print("[ok] Downloaded")
        except urllib.error.URLError as e:
            print(f"[ERROR] Download failed: {e}")
            sys.exit(1)

    if not os.path.exists(game_path):
        print(f"[ERROR] Game file not found: {game_path}")
        sys.exit(1)

    story_rel = os.path.relpath(game_path, TARGET_DIR)

    print("Cleaning previous build...")
    subprocess.run(["make", "clean"], cwd=TARGET_DIR,
                   capture_output=True, check=False)

    print(f"Building and programming ({game_name})...")
    result = subprocess.run(
        ["make", f"STORY_FILE={story_rel}", "program"],
        cwd=TARGET_DIR, capture_output=True, text=True, timeout=300,
    )
    if result.returncode != 0:
        print("[ERROR] Build/program failed")
        if result.stderr:
            print(result.stderr[-3000:])
        sys.exit(1)
    print("[ok] Device programmed successfully")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="SAM E51 ZVibe UART test")
    parser.add_argument("--auto-program", action="store_true",
                        help="Clean-build and program the device before testing")
    parser.add_argument("--port", help="Serial port override (auto-detected if omitted)")
    parser.add_argument("--game", default=None,
                        help="Game: restaurant (default), plunderedhearts, seastalker")
    args = parser.parse_args()

    game_config = get_game_config(args.game or "restaurant")
    game_name = game_config["name"]

    print(f"=== SAM E51 ZVibe UART Test — {game_name} ===")
    print(f"Platform: {platform.system()}")

    port = args.port or detect_port()
    print(f"Serial port: {port}")

    ser = serial.Serial(port, BAUD_RATE, timeout=0.1)

    if args.auto_program:
        build_and_program(game_name, game_config)

    try:
        passed, _ = run_game_test(ser, game_config, TESTS_DIR,
                                  pre_reset_wait_s=15)
    finally:
        ser.close()
    sys.exit(0 if passed else 1)
