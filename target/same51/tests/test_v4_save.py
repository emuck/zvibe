#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""Hardware test of save and restore with a V4 story on the SAM E51.

Plays target/console/tests/data/v4save.z4 (keys: i add one, p show, s save,
r restore, q quit). Checks that a V4 save stores 1, that a restore resumes after the save
instruction with 2 (the Z-machine V4 results), that the restored game has the
saved counter, and that the save survives a reset.

Setup:   pip install pyserial pyte
Flash:   cd target/same51 && make v4-program STORY_FILE=../console/tests/data/v4save.z4
Run:     python3 tests/test_v4_save.py [serial-port]
Note:    it overwrites the board's saved game (the SmartEEPROM slot).
"""
import glob, os, subprocess, sys, time
import serial, pyte

PORT = sys.argv[1] if len(sys.argv) > 1 else (sorted(glob.glob("/dev/cu.usbmodem*")) + sorted(glob.glob("/dev/ttyACM*")) + [None])[0]
if not PORT:
    sys.exit("no serial port found; pass one as the first argument")
ROWS, COLS = 24, 80
ser = serial.Serial(PORT, 115200, timeout=0.05)
screen = pyte.Screen(COLS, ROWS)
stream = pyte.ByteStream(screen)
fails = []


def pump(seconds=0.2):
    end = time.time() + seconds
    while time.time() < end:
        data = ser.read(4096)
        if data:
            if b"\x1b[6n" in data:
                ser.write(f"\x1b[{ROWS};{COLS}R".encode())
            stream.feed(data)


def text():
    return "\n".join(l.rstrip() for l in screen.display)


def wait_for(needle, timeout=15):
    end = time.time() + timeout
    while time.time() < end:
        pump(0.1)
        if needle in text():
            return True
    return False


def check(name, cond, detail=""):
    print(("PASS " if cond else "FAIL ") + name + (f"  [{detail}]" if detail and not cond else ""))
    if not cond:
        fails.append(name)


def press(key, expect):
    ser.write(key)
    ok = wait_for(expect)
    return ok


def reset_board():
    ser.reset_input_buffer()
    p = subprocess.Popen(["edbg", "-x", "100", "-t", "same51"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    while p.poll() is None:
        pump(0.05)
    pump(0.3)
    check("boot prompt", wait_for("Press RETURN to start"))
    ser.write(b"\r")
    check("story started", wait_for("v4save ready"))


def count_of(line_prefix):
    for l in reversed(screen.display):
        if l.startswith(line_prefix):
            return l.rstrip()
    return ""


reset_board()

for n in (1, 2, 3):
    check(f"increment {n}", press(b"i", f"counter={n}"))
check("save stores 1", press(b"s", "save result 1"))
for n in (4, 5):
    check(f"increment {n}", press(b"i", f"counter={n}"))
check("counter is 5 before restore", wait_for("counter=5"))
ser.write(b"r")
# a successful restore resumes the game just after the SAVE, which now reports 2 (V4)
check("restore resumes at the save with result 2", wait_for("save result 2"))
check("restored counter is 3", press(b"p", "counter=3"), text().splitlines()[-1] if text() else "")
check("game keeps running after restore", press(b"i", "counter=4"))

# the save survives a reset of the board
reset_board()
ser.write(b"p")
check("fresh game starts at 0", wait_for("counter=0"))
ser.write(b"r")
check("restore after reset resumes with result 2", wait_for("save result 2"))
check("counter back to 3 after reset", press(b"p", "counter=3"))
check("quit", press(b"q", "bye"))

print("\nFAILED:" if fails else "\nALL PASSED", fails if fails else "")
sys.exit(1 if fails else 0)
