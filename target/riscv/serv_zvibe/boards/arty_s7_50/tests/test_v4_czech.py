#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""
Czech V4 conformance gate on the Arty S7-50 over its UART.

Needs the V4 firmware with games/catalog/czech.z4 flashed:

    cd ../sw && make flash-test V4=1 GAME=czech
    cd ../fpga && make program-flash        # then reset or power cycle the board

The game waits for RETURN at boot. This script answers the player's terminal-size
probe (ESC [ 6 n) like a terminal of ROWS x COLS, presses RETURN, and reads the
story's output until it says it is done. The expected result is the one frotz and the
console give (target/console/tests/test_czech_v4.sh): 386 tests performed, 367
passed, 0 failed, 19 print tests.

    python3 test_v4_czech.py [--port /dev/ttyUSB1] [--timeout 300] [--rows 24 --cols 80]
"""
import argparse
import re
import sys
import time

import serial
import serial.tools.list_ports

EXPECT_PERFORMED = 386
EXPECT_PASSED = 367
EXPECT_FAILED = 0
EXPECT_PRINT = 19
FTDI = (0x0403, 0x6010)


def detect_port():
    ports = sorted(p.device for p in serial.tools.list_ports.comports()
                   if (p.vid, p.pid) == FTDI)
    if not ports:
        sys.exit("no FT2232H found: is the Arty connected? (ls /dev/ttyUSB*)")
    return ports[-1]           # channel B is the UART


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port")
    ap.add_argument("--timeout", type=float, default=300)
    ap.add_argument("--rows", type=int, default=24)
    ap.add_argument("--cols", type=int, default=80)
    args = ap.parse_args()

    port = args.port or detect_port()
    ser = serial.Serial(port, 115200, timeout=0.2)
    ser.reset_input_buffer()
    print(f"[ok] UART {port}")

    reply = f"\x1b[{args.rows};{args.cols}R".encode()
    raw = b""
    answered = 0
    sent_return = False
    start = time.time()
    last_rx = start
    # RETURN first (the game is waiting for it, or ~reset~ restarts it), then the story's output
    ser.write(b"~reset~\r")
    while time.time() - start < args.timeout:
        data = ser.read(4096)
        if data:
            raw += data
            last_rx = time.time()
        # one reply per probe
        probes = raw.count(b"\x1b[6n")
        while answered < probes:
            ser.write(reply)
            answered += 1
        text = re.sub(rb"\x1b\[[0-9;?]*[A-Za-z]|\x1b[()][A-Z0-9]", b"", raw).decode("latin1")
        if "Last test:" in text:
            break
        if not sent_return and not data and time.time() - last_rx > 3 and answered == 0:
            ser.write(b"\r")                 # RETURN may have been missed during a reset
            sent_return = True
    elapsed = time.time() - start
    text = re.sub(rb"\x1b\[[0-9;?]*[A-Za-z]|\x1b[()][A-Z0-9]", b"", raw).decode("latin1")

    ok = True

    def check(name, cond, detail=""):
        nonlocal ok
        print(("PASS  " if cond else "FAIL  ") + name + (f"  ({detail})" if detail and not cond else ""))
        ok = ok and cond

    check("player asked the terminal for its size", answered >= 1, f"{answered} probes")
    m = re.search(r"Performed (\d+) tests\.\s+Passed: (\d+), Failed: (\d+), Print tests: (\d+)", text)
    check("Czech printed its summary", m is not None, f"after {elapsed:.0f} s")
    if m:
        got = tuple(int(x) for x in m.groups())
        want = (EXPECT_PERFORMED, EXPECT_PASSED, EXPECT_FAILED, EXPECT_PRINT)
        check(f"386 performed, 367 passed, 0 failed, 19 print tests", got == want, f"got {got}")
    check("it did not crash", "Didn't crash: hooray!" in text)
    check("it ended with quit", "Last test: quit!" in text)
    print(f"[{elapsed:.0f} s, {len(raw)} bytes]")
    if not ok:
        print("--- last 1200 characters of output ---")
        print(text[-1200:])
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
