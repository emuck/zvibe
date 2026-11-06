#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""A small save written over a large one survives a reset.

The SmartEEPROM does not persist single-byte stores, nor zero words at the end of a write over
older non-zero data. A save whose last bytes were zero therefore came back, after a reset, with
those bytes as an earlier, larger save had left them (and the checksum, rightly, rejected it).
This test stores a large save (data/v4bigsave.z4), flashes data/v4save.z4, saves a small game and
restores it after a reset.

Setup:   pip install pyserial pyte
Run:     python3 tests/test_v4_save_over_big.py [serial-port]     (flashes the board twice)
Note:    it overwrites the board's saved game (the SmartEEPROM slot).
"""
import glob, os, subprocess, sys, time
import serial, pyte

HERE = os.path.dirname(os.path.abspath(__file__))
TARGET = os.path.dirname(HERE)
DATA = os.path.join(TARGET, "..", "console", "tests", "data")
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
    return wait_for(expect)


def flash(story):
    r = subprocess.run(["make", "v4-program", f"STORY_FILE={story}"], cwd=TARGET, capture_output=True, text=True)
    subprocess.run(["rm", "-f", os.path.join(TARGET, "src", "story_data.c")])
    if r.returncode != 0:
        sys.exit("flashing failed:\n" + r.stdout[-800:] + r.stderr[-800:])
    ser.reset_input_buffer()
    time.sleep(2)


def start():
    screen.reset()
    ser.reset_input_buffer()
    p = subprocess.Popen(["edbg", "-x", "100", "-t", "same51"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    while p.poll() is None:
        pump(0.05)
    pump(0.3)
    check("boot prompt", wait_for("Press RETURN to start"))
    ser.write(b"\r")
    check("story started", wait_for("ready"))



big = os.path.abspath(os.path.join(DATA, "v4bigsave.z4"))
small = os.path.abspath(os.path.join(DATA, "v4save.z4"))
flash(big)
start()
for _ in range(3):
    ser.write(b"f")
    wait_for("filled", 5)
ser.write(b"s")
check("a large save is stored", wait_for("save result 1", 10))

flash(small)
start()
for n in (1, 2, 3):
    check(f"increment {n}", press(b"i", f"counter={n}"))
check("a small save over the large one", press(b"s", "save result 1"))
for attempt in (1, 2):
    start()
    ser.write(b"r")
    check(f"restore after a reset ({attempt})", wait_for("save result 2", 8), text()[-100:])
    check(f"with the counter it had ({attempt})", press(b"p", "counter=3"))

print("ALL PASSED" if not fails else f"{len(fails)} FAILED: {fails}")
sys.exit(1 if fails else 0)
