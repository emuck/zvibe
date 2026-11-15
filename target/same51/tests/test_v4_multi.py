#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""Hardware test for the multi-game V4 firmware on the SAM E51 Curiosity Nano.

Plays the menu build (make v4-multi) with the five Infocom version 4 stories in this order:
1 Bureaucracy, 2 Trinity, 3 A Mind Forever Voyaging, 4 Nord and Bert, 5 Generic. Checks that
every story starts from its menu number, that quitting returns to the menu, that each story
has a save slot of its own (a save in one is not seen by another, and survives a reset), and
that a story can be restored after the board has been reset.

Setup:   pip install pyserial pyte
Flash:   cd target/same51 && make v4-multi-program
Run:     python3 tests/test_v4_multi.py [serial-port]
"""
import glob, os, re, subprocess, sys, time
import serial, pyte

PORT = sys.argv[1] if len(sys.argv) > 1 else (sorted(glob.glob("/dev/cu.usbmodem*")) + sorted(glob.glob("/dev/ttyACM*")) + [None])[0]
if not PORT:
    sys.exit("no serial port found; pass one as the first argument")
ROWS, COLS = 24, 80

GAMES = [  # menu number, name, text the story shows when it starts
    ("1", "Bureaucracy", "Copyright (C) 1987 Infocom"),
    ("2", "Trinity", "Press any key to begin"),
    ("3", "A Mind Forever Voyaging", "PART I"),
    ("4", "Nord and Bert", "Press any key to begin"),
    ("5", "Generic", "GENERIC"),
]

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


def quit_to_menu():
    """Quit the story. Each asks to confirm in its own way (a line, or one key); then RETURN for the menu."""
    type_line("quit", 2.5)
    ser.write(b"y")                      # Bureaucracy, Trinity and Nord and Bert read a line, A Mind Forever
    pump(0.3)                            # Voyaging one key: a CR after the y is harmless (it ends the line, or
    ser.write(b"\r")                     # is the RETURN the board waits for, or is ignored by the menu)
    pump(2.5)
    if "Press RETURN for the menu" in text():
        ser.write(b"\r")
        pump(2)
    return "Type a number" in text()


def save_and_reset(num, setup, save_ok):
    """Save game `num`, then reset the board, restore, and return the screen text after the restore."""
    start_game(num)
    any_key()
    for line in setup:
        type_line(line, 3)
    type_line("save", 4)
    ok = save_ok in text()
    return ok


def main():
    print(f"[port {PORT}]")
    check("board starts at the menu", reboot(), text()[-200:])
    for num, name, _ in GAMES:
        check(f"menu lists {num}. {name}", f"{num}. {name}" in text())

    print("== every story starts, and quitting returns to the menu ==")
    for num, name, hello in GAMES:
        start_game(num)
        check(f"{name} starts", hello in text(), text()[-300:])
        if name == "Bureaucracy":            # a licence form first: RETURN starts a new game, a key begins the form
            type_line("", 2)
            any_key()
            for f in FORM:
                type_line(f, 1.0)
            pump(2)
        elif name == "Generic":              # no quit command: ~reset~ restarts it, the board's button returns to the menu
            type_line("~reset~", 2)
            check("Generic: ~reset~ restarts the story", text().count("Release 6") >= 1 and "Okay, what do you want to do now?" in text(), text()[-300:])
            reboot()
            continue
        else:
            any_key()
        check(f"{name}: quit returns to the menu", quit_to_menu(), text()[-300:])

    print("== a save slot for each story, kept across a reset ==")
    # (story, setup commands that make its state recognisable, message of a completed save,
    #  text its restored screen shows, message of a completed restore)
    SAVES = [
        ("2", "Trinity", [], "SAVE completed", "Palace Gate", "RESTORE completed"),
        ("3", "A Mind Forever Voyaging", ["ppcc"], "Ok, saved", "PRISM Project Control Center", "Ok, restored"),
        ("4", "Nord and Bert", [], "Save, completed", "Beginning", "estored"),
    ]
    reboot()
    for num, name, setup, saved, shows, restored in SAVES:
        start_game(num)
        any_key()
        for line in setup:
            type_line(line, 3)
        type_line("save", 4)
        check(f"{name}: save completes", saved in text(), text()[-250:])
        check(f"{name}: back to the menu", reboot())
    for num, name, setup, saved, shows, restored in SAVES:
        start_game(num)
        any_key()
        type_line("restore", 5)
        t = text()
        check(f"{name}: restores its own save after a reset", restored in t and shows in t and "failed" not in t.lower(), t[-300:])
        check(f"{name}: back to the menu", reboot())

    print(f"\n{'ALL PASSED' if not fails else 'FAILED: ' + ', '.join(fails)}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
