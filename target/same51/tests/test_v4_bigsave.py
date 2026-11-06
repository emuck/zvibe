#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""Hardware test of a save larger than the SmartEEPROM (V4 story, SAM E51).

Plays target/console/tests/data/v4bigsave.z4, a story with 21 KB of dynamic memory (a
full save is over 21 KB, the SmartEEPROM holds 16 KB, as with Bureaucracy). The save
is stored packed against the story in flash. Keys: f fill 1000 more bytes, c checksum,
s save, r restore, q quit. Checks the save and restore results, that the restored
game has exactly the saved contents (by checksum, against values computed here), and
that the save survives a reset.

Setup:   pip install pyserial pyte
Flash:   cd target/same51 && make v4-program STORY_FILE=../console/tests/data/v4bigsave.z4
Run:     python3 tests/test_v4_bigsave.py [serial-port]
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
    check("story started", wait_for("ready"))


def count_of(line_prefix):
    for l in reversed(screen.display):
        if l.startswith(line_prefix):
            return l.rstrip()
    return ""


SUMS = {0: 0, 1: 26116, 2: 22006, 3: 17704, 4: 13233, 5: 8616}   # checksum after n fills (see v4bigsave.inf)


def fill(n):
    return press(b"f", f"filled {n}")


def checksum_is(n):
    ser.write(b"c")
    return wait_for(f"sum={SUMS[n]} filled={n}")


reset_board()
for n in (1, 2, 3):
    check(f"fill {n}", fill(n))
check("checksum after 3 fills", checksum_is(3))
check("save stores 1", press(b"s", "save result 1"))
for n in (4, 5):
    check(f"fill {n}", fill(n))
check("checksum after 5 fills", checksum_is(5))
ser.write(b"r")
check("restore resumes at the save with result 2", wait_for("save result 2"))
check("restored game has the 3-fill contents", checksum_is(3))

reset_board()
check("fresh game is empty", checksum_is(0))
ser.write(b"r")
check("restore after reset resumes with result 2", wait_for("save result 2"))
check("contents back after reset", checksum_is(3))

# a game whose changes do not fit the SmartEEPROM: the save fails (0) and the old save survives
for n in range(1, 18):
    fill(n)
check("17 fills in", wait_for("filled 17"))
check("oversized save stores 0", press(b"s", "save result 0"))
check("oversized save said why", wait_for("do not fit"))
reset_board()
ser.write(b"r")
check("previous save still restores", wait_for("save result 2"))
check("previous save intact", checksum_is(3))
check("quit", press(b"q", "bye"))

print("\nFAILED:" if fails else "\nALL PASSED", fails if fails else "")
sys.exit(1 if fails else 0)
