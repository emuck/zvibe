#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""
Shared UART test harness for ZVibe hardware targets.

Board-specific test scripts import this module and provide:
  - port detection
  - auto-programming
  - board name / game config overrides

This module provides:
  - Script parsing with @capture / {var} directives
  - Serial prompt detection and @sread pause handling
  - Game replay loop with latency profiling
  - Completion marker verification
"""

import os
import re
import serial
import statistics
import sys
import time
from datetime import datetime

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------
BAUD_RATE = 115200
CHAR_DELAY_MS = 5
INACTIVITY_TIMEOUT_S = 20
CONTINUE_PROMPT = "[Press RETURN or ENTER to continue.]"

REPO_ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), "../.."))
SCRIPTS_DIR = os.path.dirname(os.path.abspath(__file__))

# ---------------------------------------------------------------------------
# Game registry
# ---------------------------------------------------------------------------
GAMES = {
    "restaurant": {
        "name": "restaurant",
        "script": os.path.join(SCRIPTS_DIR, "restaurant-script.txt"),
        "completion_markers": ["*** You have won ***"],
    },
    "plunderedhearts": {
        "name": "plunderedhearts",
        "file": "plunderedhearts-r26-s870730.z3",
        "script": os.path.join(SCRIPTS_DIR, "plunderedhearts-script.txt"),
        "completion_markers": [
            "you have achieved a score of 25 out of 25 points",
            "Happily Ever After",
        ],
    },
    "seastalker": {
        "name": "seastalker",
        "file": "seastalker-mac-r15-s840522.z3",
        "script": os.path.join(SCRIPTS_DIR, "seastalker-script.txt"),
        "completion_markers": [
            "your score is 100 points out of 100",
        ],
    },
}


def get_game_config(name):
    if name not in GAMES:
        print(f"[ERROR] Unknown game '{name}'. Known: {', '.join(GAMES)}")
        sys.exit(1)
    return GAMES[name]


# ---------------------------------------------------------------------------
# Script parsing
# ---------------------------------------------------------------------------
def parse_script(path):
    """Parse a test script into a list of steps.

    Returns list of:
      {"type": "cmd", "text": "..."}
      {"type": "capture", "name": "...", "pattern": "..."}
    """
    steps = []
    with open(path) as f:
        for line in f:
            stripped = line.strip()
            if not stripped or stripped.startswith("#"):
                continue
            m = re.match(r"@capture\s+(\w+)\s+/(.+)/\s*$", stripped)
            if m:
                steps.append({"type": "capture", "name": m.group(1), "pattern": m.group(2)})
            else:
                steps.append({"type": "cmd", "text": stripped})
    return steps


# ---------------------------------------------------------------------------
# Serial helpers
# ---------------------------------------------------------------------------
def wait_for_prompt(ser, timeout_s=INACTIVITY_TIMEOUT_S):
    """Read until '>' prompt or timeout. Returns (text, got_prompt)."""
    response = ""
    last_data_time = time.time()
    last_char = ""

    while True:
        if ser.in_waiting:
            ch = ser.read(1).decode("utf-8", errors="ignore")
            response += ch
            last_data_time = time.time()

            if ch == ">" and last_char in ("\n", "\r", " ", ""):
                return response, True

            if ch != "\r":
                last_char = ch

            if CONTINUE_PROMPT in response:
                ser.write(b"\r")
                response = response.replace(CONTINUE_PROMPT, "[AUTO-CONTINUE]\n")
                time.sleep(0.5)
                last_data_time = time.time()
                last_char = "\n"
        else:
            if (time.time() - last_data_time) > timeout_s:
                break
            time.sleep(0.01)

    cleaned = response.replace("\r", "")
    if "\n>" in cleaned:
        return cleaned, True
    return response.replace("\r", ""), False


# ---------------------------------------------------------------------------
# Latency reporting
# ---------------------------------------------------------------------------
def percentile(values, p):
    if not values:
        return 0.0
    s = sorted(values)
    n = len(s)
    if n == 1:
        return s[0]
    rank = (p / 100.0) * (n - 1)
    lo, hi = int(rank), min(int(rank) + 1, n - 1)
    frac = rank - lo
    return s[lo] + frac * (s[hi] - s[lo])


def print_latency_summary(latencies_s, game_name, output_dir):
    if not latencies_s:
        print("No latency samples captured.")
        return

    n = len(latencies_s)
    print("\nLatency Summary (seconds)")
    print(f"  n          = {n}")
    print(f"  min        = {min(latencies_s):.3f}")
    print(f"  median     = {statistics.median(latencies_s):.3f}")
    print(f"  mean       = {statistics.mean(latencies_s):.3f}")
    print(f"  p95        = {percentile(latencies_s, 95):.3f}")
    print(f"  max        = {max(latencies_s):.3f}")
    print(f"  stdev      = {statistics.stdev(latencies_s) if n > 1 else 0.0:.3f}")
    print(f"  >2 s       = {sum(1 for x in latencies_s if x > 2.0) / n * 100:.1f}%")
    print(f"  >5 s       = {sum(1 for x in latencies_s if x > 5.0) / n * 100:.1f}%")

    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    csv_path = os.path.join(output_dir, f"{game_name}_latencies_{ts}.csv")
    with open(csv_path, "w") as f:
        f.write("latency_s\n")
        for lat in latencies_s:
            f.write(f"{lat:.3f}\n")
    print(f"  CSV        -> {csv_path}")


# ---------------------------------------------------------------------------
# Progress display
# ---------------------------------------------------------------------------
def print_progress(current, total):
    pct = current / total
    bar = "█" * int(40 * pct) + "-" * (40 - int(40 * pct))
    sys.stdout.write(f"\rProgress: |{bar}| {pct:.0%} ({current}/{total})")
    sys.stdout.flush()


# ---------------------------------------------------------------------------
# Core test runner
# ---------------------------------------------------------------------------
def run_game_test(ser, game_config, output_dir, save_test=False,
                  pre_reset_wait_s=0):
    """Replay a game script over serial. Returns (passed, latencies_s).

    Args:
        ser:              open pyserial port
        game_config:      dict from GAMES registry
        output_dir:       directory for log and CSV output
        save_test:        if True, collect [SAVE sz=...] data
        pre_reset_wait_s: seconds to wait for device boot before ~reset~
    """
    game_name = game_config["name"]
    script_path = game_config["script"]
    log_path = os.path.join(output_dir, f"{game_name}_uart_test_output.log")

    if not os.path.exists(script_path):
        print(f"[ERROR] Script not found: {script_path}")
        sys.exit(1)

    # Wait for device to be ready (SAM E51 needs this after programming)
    if pre_reset_wait_s > 0:
        print(f"Waiting for device ready (up to {pre_reset_wait_s} s)...")
        deadline = time.perf_counter() + pre_reset_wait_s
        while time.perf_counter() < deadline:
            if ser.in_waiting > 0:
                time.sleep(0.2)
                ser.read(ser.in_waiting)
                print("[ok] Device is up")
                break
            time.sleep(0.05)

    print("Flushing and sending ~reset~...")
    time.sleep(0.5)
    ser.read(ser.in_waiting)
    ser.write(b"~reset~\r")

    print("Waiting for game prompt after reset...")
    intro, got_prompt = wait_for_prompt(ser, timeout_s=60)
    if not got_prompt:
        print("[ERROR] No game prompt within 60 s of reset")
        if intro.strip():
            print(f"[debug] Received: {repr(intro[:300])}")
        sys.exit(1)
    print("[ok] Game reset complete, starting script")

    steps = parse_script(script_path)
    captures = {}
    last_response = ""
    cmd_count = sum(1 for s in steps if s["type"] == "cmd")

    latencies_s = []
    full_output = ""
    save_sizes = []

    with open(log_path, "w", encoding="utf-8") as log:
        log.write("=== After ~reset~ ===\n")
        log.write(intro.replace("\r", ""))
        log.write("\n=== Script commands ===\n\n")

        cmd_idx = 0
        for step in steps:
            if step["type"] == "capture":
                m = re.search(step["pattern"], last_response)
                if m:
                    captures[step["name"]] = m.group(1)
                continue

            command = step["text"]
            for var, val in captures.items():
                command = command.replace("{" + var + "}", val)

            cmd_idx += 1
            print_progress(cmd_idx, cmd_count)
            log.write(f"\n> {command}\n")

            cmd_start = time.perf_counter()

            for ch in command:
                ser.write(ch.encode("utf-8"))
                time.sleep(CHAR_DELAY_MS / 1000.0)
            ser.write(b"\r")

            if command.lower() == "quit":
                time.sleep(1)
                latencies_s.append(time.perf_counter() - cmd_start)
                break

            response, got_prompt = wait_for_prompt(ser)
            latencies_s.append(time.perf_counter() - cmd_start)

            cleaned = response.replace("\r", "")
            log.write(cleaned)
            full_output += cleaned
            last_response = cleaned

            if save_test and command.lower() == "save":
                m = re.search(r"\[SAVE sz=0x([0-9A-Fa-f]+)\]", cleaned)
                if m:
                    save_sizes.append((cmd_idx, int(m.group(1), 16)))
                elif "[SAVE overflow" in cleaned:
                    save_sizes.append((cmd_idx, None))

            if not got_prompt:
                ser.write(b"\r")
                more, got_prompt = wait_for_prompt(ser, timeout_s=10)
                full_output += more.replace("\r", "")
                last_response += more.replace("\r", "")
                if got_prompt:
                    continue
                for marker in game_config["completion_markers"]:
                    if marker.lower() in full_output.lower():
                        print_progress(cmd_count, cmd_count)
                        break
                else:
                    print(f"\n[ERROR] No prompt after '{command}' (inactivity timeout)")
                    sys.exit(1)
                break

    print()

    if save_test and save_sizes:
        print("\nDelta Save Sizes:")
        print(f"{'Turn':>6}  {'Bytes':>6}  Bar")
        print("-" * 50)
        for turn, sz in save_sizes:
            if sz is None:
                print(f"{turn:>6}  {'OVERFLOW':>6}  !!!")
            else:
                bar = "█" * int(30 * sz / 2048)
                pct = sz / 2048 * 100
                print(f"{turn:>6}  {sz:>6}  {bar}  {pct:.0f}% of 2KB slot")
        valid = [s for _, s in save_sizes if s is not None]
        if valid:
            print(f"\nMax save size: {max(valid)} bytes / 2048 bytes slot")

    print_latency_summary(latencies_s, game_name, output_dir)

    lower = full_output.lower()
    passed = any(m.lower() in lower for m in game_config["completion_markers"])

    if passed:
        print(f"\n\033[0;32mPASSED: {game_name} completed successfully!\033[0m")
    else:
        print(f"\n\033[0;31mFAILED: {game_name} did not complete.\033[0m")
    print(f"Log: {log_path}")

    return passed, latencies_s
