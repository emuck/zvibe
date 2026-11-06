#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""Hardware test for the V4 game firmware on the SAM E51 Curiosity Nano.

Runs target/console/tests/data/v4screen.z4 (a small V4 story that uses the split
window, styles, cursor addressing, output stream 3, read_char, line input,
get_cursor, scan_table and erase_line) and checks the resulting terminal screen,
which is rendered by a real terminal emulator (pyte).

Setup:   pip install pyserial pyte
Flash:   cd target/same51 && make v4-program STORY_FILE=../console/tests/data/v4screen.z4
Run:     python3 tests/test_v4_game.py [serial-port]
Env:     TERM_SIZE=RxC  terminal size to report and emulate (default 24x80). The story
                        prints its header screen size, so it must match (columns are capped at 80).
         KEY=arrow      answer read_char with the Up-arrow escape sequence (expects
                        ZSCII 129) instead of the letter q (expects 113)
"""
import glob, os, re, subprocess, sys, time
import serial, pyte

PORT = sys.argv[1] if len(sys.argv) > 1 else (sorted(glob.glob("/dev/cu.usbmodem*")) + sorted(glob.glob("/dev/ttyACM*")) + [None])[0]
if not PORT:
    sys.exit("no serial port found; pass one as the first argument")
ROWS, COLS = (int(x) for x in os.environ.get("TERM_SIZE", "24x80").split("x"))
ARROW = os.environ.get("KEY") == "arrow"

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


def wait_for(text, timeout=20):
    end = time.time() + timeout
    while time.time() < end:
        pump(0.1)
        if any(text in line for line in screen.display):
            return True
    return False


def check(name, cond, detail=""):
    print(("PASS " if cond else "FAIL ") + name + (f"  [{detail}]" if detail and not cond else ""))
    if not cond:
        fails.append(name)


def dump(title):
    print(f"--- {title} ---")
    for i, line in enumerate(screen.display):
        print(f"{i+1:2d}|{line.rstrip()}")


def cell(row, col):          # 1-based
    return screen.buffer[row - 1][col - 1]


ser.reset_input_buffer()
proc = subprocess.Popen(["edbg", "-x", "100", "-t", "same51"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
while proc.poll() is None:
    pump(0.05)
pump(0.3)

check("start prompt", wait_for("Press RETURN to start"))
ser.write(b"\r")

check("story reached read_char", wait_for("press a key:"))
dump("before the key")
check("status bar text", "ZVIBE V4" in screen.display[0] and "Score: 7" in screen.display[0], screen.display[0])
check("status bar is reverse video", all(cell(1, c).reverse for c in range(1, min(COLS, 80))))
check("row 2 text", screen.display[1].startswith("Row two"), screen.display[1])
check("row 3 word is bold", screen.display[2][4:8] == "bold" and all(cell(3, c).bold for c in range(5, 9)), screen.display[2])
check("lower window scrolled", any("Lower line 30" in l for l in screen.display))
check("status bar survived the scrolling", "ZVIBE V4" in screen.display[0])
check("long paragraph wrapped on a word boundary", any(l.rstrip().endswith("by the") for l in screen.display)
      or "by the interpreter" in " ".join(l.strip() for l in screen.display))
check("header screen size", any(f"size {ROWS}x{min(COLS, 80)}" in l for l in screen.display))
check("stream 3 capture", any("len=8 [captured]" in l for l in screen.display))

ser.write(b"\x1b[A" if ARROW else b"q")
want = "key=129" if ARROW else "key=113"
check("read_char value", wait_for(want), "expected " + want)

check("line prompt", wait_for("> "))
for ch in b"helxx":
    ser.write(bytes([ch])); pump(0.03)
for _ in range(2):
    ser.write(b"\x7f"); pump(0.05)
for ch in b"lo":
    ser.write(bytes([ch])); pump(0.03)
pump(0.2)
check("line echo with backspace", any(l.rstrip().endswith("> hello") for l in screen.display))
ser.write(b"\r")
check("story got the line", wait_for("got: hello"))
check("finished", wait_for("done"))
pump(0.3)
dump("after the story")
check("get_cursor reports row, column 1", any(re.search(r"cursor \d+,1\b", l) for l in screen.display))
check("scan_table found the key", any("scan: found" in l for l in screen.display))
check("status bar score updated", "Score: 8" in screen.display[0] and "Score: 7" not in screen.display[0], screen.display[0])
check("erase_line kept the head", "tail-to-" in screen.display[2] and "erase" not in screen.display[2], screen.display[2])
check("restart prompt after the game", wait_for("Game finished") and wait_for("Press RETURN to start"))
ser.write(b"\r")
check("game restarts after RETURN", wait_for("press a key:"))

print("\nFAILED:" if fails else "\nALL PASSED", fails if fails else "")
sys.exit(1 if fails else 0)
