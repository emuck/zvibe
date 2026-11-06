#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""A save made by one release of a story is refused by another release of it.

A packed save leaves out every byte equal to the flashed story, including the story's
own release, serial and checksum, and unpacking fills them in from the story flashed
now; the firmware therefore stores the identity next to the packed data and compares it
first. This test makes release 2 of data/v4save.inf (same size, different release and
serial; needs `inform` 6.44 on the PATH), saves a game on release 1, flashes release 2
and tries to restore: the restore must be refused and the game must carry on from the start.

Setup:   pip install pyserial pyte
Run:     python3 tests/test_v4_identity.py [serial-port]     (flashes the board twice)
Note:    it overwrites the board's saved game (the SmartEEPROM slot) and leaves release 2 flashed.
"""
import glob, os, shutil, subprocess, sys, tempfile, time
import serial, pyte

HERE = os.path.dirname(os.path.abspath(__file__))
TARGET = os.path.dirname(HERE)
DATA = os.path.join(TARGET, "..", "console", "tests", "data")
PORT = sys.argv[1] if len(sys.argv) > 1 else (sorted(glob.glob("/dev/cu.usbmodem*")) + sorted(glob.glob("/dev/ttyACM*")) + [None])[0]
if not PORT:
    sys.exit("no serial port found; pass one as the first argument")
if not shutil.which("inform"):
    sys.exit("inform (6.44) is needed to build release 2 of the test story")
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


work = tempfile.mkdtemp()
src = open(os.path.join(DATA, "v4save.inf")).read()
open(os.path.join(work, "r2.inf"), "w").write(src.replace("Global counter = 0;", 'Release 2;\nSerial "261102";\nGlobal counter = 0;', 1))
build = subprocess.run(["inform", "-v4", "r2.inf", "r2.z4"], cwd=work, capture_output=True, text=True)
if not os.path.exists(os.path.join(work, "r2.z4")):
    sys.exit("could not build release 2:\n" + build.stdout[-500:])
a = os.path.abspath(os.path.join(DATA, "v4save.z4"))
b = os.path.join(work, "r2.z4")
check("release 2 differs from release 1 in release and serial", open(a, "rb").read()[2:4] != open(b, "rb").read()[2:4])
check("and has the same size", len(open(a, "rb").read()) == len(open(b, "rb").read()))

flash(a)
start()
for n in (1, 2, 3):
    check(f"increment {n} (release 1)", press(b"i", f"counter={n}"))
check("save on release 1", press(b"s", "save result 1"))

flash(b)
start()
ser.write(b"r")
check("release 2 refuses the save", wait_for("different story or release"))
check("the game goes on from the start", press(b"p", "counter=0"), text().splitlines()[-1] if text() else "")
check("no restore happened", "save result 2" not in text())

flash(a)
start()
ser.write(b"r")
check("release 1 still restores its own save", wait_for("save result 2"))
check("with its counter", press(b"p", "counter=3"))

shutil.rmtree(work, ignore_errors=True)
print("ALL PASSED" if not fails else f"{len(fails)} FAILED: {fails}")
sys.exit(1 if fails else 0)
