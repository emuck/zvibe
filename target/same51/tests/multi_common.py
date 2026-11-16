#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""Shared helpers for the multi-game menu hardware tests (test_multi.py, test_multi_mixed.py):
the serial port, a terminal emulator (pyte) that answers the firmware's terminal-size question,
resetting the board, and typing at the games."""
import glob, os, re, subprocess, sys, time
import serial, pyte

PORT = sys.argv[1] if len(sys.argv) > 1 else (sorted(glob.glob("/dev/cu.usbmodem*")) + sorted(glob.glob("/dev/ttyACM*")) + [None])[0]
if not PORT:
    sys.exit("no serial port found; pass one as the first argument")
ROWS, COLS = 24, 80

ser = serial.Serial(PORT, 115200, timeout=0.05)
screen = pyte.Screen(COLS, ROWS)
stream = pyte.ByteStream(screen)
raw = b""
answered = 0
fails = []


def reset_term():
    global screen, stream, raw, answered
    screen = pyte.Screen(COLS, ROWS)
    stream = pyte.ByteStream(screen)
    raw = b""
    answered = 0


def pump(seconds=0.3):
    global raw, answered
    end = time.time() + seconds
    while time.time() < end:
        data = ser.read(4096)
        if data:
            raw += data
            stream.feed(data)
            while answered < raw.count(b"\x1b[6n"):
                ser.write(f"\x1b[{ROWS};{COLS}R".encode())
                answered += 1


def text():
    return "\n".join(line.rstrip() for line in screen.display)


def wait_for(s, timeout=15):
    end = time.time() + timeout
    while time.time() < end:
        pump(0.15)
        if s in text():
            return True
    return False


def check(name, cond, detail=""):
    print(("PASS " if cond else "FAIL ") + name + (f"  [{detail}]" if detail and not cond else ""))
    if not cond:
        fails.append(name)
    return cond


def reboot():
    """Reset the board (edbg -x) and wait for the menu. The board asks the terminal for its size at
    start; pump() answers."""
    reset_term()
    ser.reset_input_buffer()
    p = subprocess.Popen(["edbg", "-x", "100", "-t", "same51"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    while p.poll() is None:
        pump(0.05)
    pump(1.0)
    ser.write(b"\r")                    # draws the menu if the terminal was not there at start
    return wait_for("Type a number", 15)


def type_line(s, wait=2.5):
    ser.write(s.encode() + b"\r")
    pump(wait)


def start_game(digit):
    ser.write(digit.encode())
    pump(3.5)


def any_key():
    ser.write(b"x")
    pump(2.5)


FORM = ["Smith", "John", "Q", "M", "42", "Main Street", "Springfield", "CA", "90210", "5551212", "Acme",
        "Blue", "Pat", "Chris", "Jones", "Elm St", "Boston", "red", "Lee", "X", "12", "Z"]


def tail(n=160):
    return " ".join(l for l in text().split("\n") if l.strip())[-n:]


def last_line():
    """The last non-empty row of the screen: the prompt the story is waiting at."""
    rows = [l for l in text().split("\n") if l.strip()]
    return rows[-1] if rows else ""


def quit_to_menu():
    """Quit whatever story is running and get back to the menu. The stories confirm in their own ways
    (a line or one key, "Hit RETURN" first in some): answer the prompt on the last line until the menu is back."""
    type_line("quit", 2.5)
    for _ in range(8):
        if "Type a number" in text():
            return True
        prompt = last_line()
        if "Press RETURN for the menu" in prompt or "RETURN or ENTER" in prompt:
            ser.write(b"\r")
        else:
            ser.write(b"y\r")        # a line (y, RETURN) or one key (y, then an ignored RETURN)
        pump(2.5)
    return "Type a number" in text()


def save_game(done, timeout=90):
    """Type `save` and wait until the story says it is done (`done`) or the firmware says the save failed. A save
    takes 0.1-0.3 s, the first one after a reset 2-3 s and, rarely, longer: never reset the board before it is
    over (that is an interrupted write, which is a test of its own). Returns the text it ended with, or None."""
    before = text().count(done)
    ser.write(b"save\r")
    end = time.time() + timeout
    while time.time() < end:
        pump(0.15)
        if text().count(done) > before or "[Save failed" in text():
            pump(1.0)
            return last_line() if "[Save failed" not in text() else text()
    return None


def restore_game(done, timeout=30):
    """Type `restore` and wait for the story's `done` message or the firmware's reason it could not."""
    before = text().count(done)
    ser.write(b"restore\r")
    end = time.time() + timeout
    while time.time() < end:
        pump(0.15)
        if text().count(done) > before:
            pump(1.0)
            return True
        if "[No saved game" in text() or "[The saved game" in text() or "[That save" in text() or "[Restore failed" in text():
            return False
    return False


def turns():
    """The number of turns a V3 story says have passed: `score` first asks to "Hit RETURN" (the peril-sensitive
    sunglasses), then prints "... in N turns", and takes no turn itself. None if it does not answer."""
    import re
    before = len(re.findall(r"in (\d+) turns?", text()))
    ser.write(b"score\r")
    end = time.time() + 20
    while time.time() < end:
        pump(0.4)
        if "RETURN or ENTER" in last_line():
            ser.write(b"\r")
            pump(1.0)
        m = re.findall(r"in (\d+) turns?", text())
        if len(m) > before:
            pump(0.5)
            return int(m[-1])
    return None


def vary():
    """Make the game's state different from every earlier run's (a random number of turns pass), so a save
    always has new content: an identical re-save of a slot that an earlier interrupted write damaged
    does not repair it (observed on the board), and a test must not depend on what an older run left."""
    import random
    for _ in range(random.randint(1, 4)):
        type_line("look", 1.5)


def finish():
    print(f"\n{'ALL PASSED' if not fails else 'FAILED: ' + ', '.join(fails)}")
    return 1 if fails else 0
