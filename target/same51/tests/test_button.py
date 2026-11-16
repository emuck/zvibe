#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""Hardware test: the board's button takes the player back to the menu from a story that is waiting for input.

The button cannot be pressed from here, so the test build (SAVE_TEST_HOOKS, never a release build) can "press" it on a
timer: the command ~abort 3~ at a story's line prompt, or the key A at the menu (before choosing the story), arms a
press 3 or 4 seconds later. It sets the same flag as the button's interrupt (terminal_io.c, term_request_abort()).

    cd target/same51 && make multi-program EXTRA_CFLAGS=-DSAVE_TEST_HOOKS=1 MULTI_ARGS="\\
        --story Restaurant=../../games/catalog/restaurant.z3 --story Hitchhiker=$HOME/infocom/hitchhiker.z3 \\
        --story Czech=../../games/catalog/czech.z3 \\
        --story Bureaucracy=$HOME/infocom/library/z4/bureaucracy/bureaucracy-r116-s870602.z4 \\
        --story Nord-and-Bert=$HOME/infocom/library/z4/nordandbert/nordandbert-r19-s870722.z4"
    python3 tests/test_button.py [serial-port]
"""
import sys, time
from multi_common import *


def menu_back(seconds):
    end = time.time() + seconds
    while time.time() < end:
        pump(0.2)
        if "Type a number" in text():
            return True
    return False


def main():
    print(f"[port {PORT}]")
    check("board starts at the menu", reboot())

    # a story waiting for a line
    start_game("2")
    check("Hitchhiker is at its line prompt", "pitch black" in text())
    ser.write(b"~abort 3\r"); pump(0.5)
    check("the button at a line prompt returns straight to the menu (no RETURN asked for)", menu_back(8), tail(200))
    check("it did not ask for RETURN first", "Press RETURN for the menu" not in text())
    start_game("1")
    check("the next story starts normally (no stale press left over)", "On the Ramp" in text(), tail(200))
    ser.write(b"look\r"); pump(2)
    check("and takes input (the command is echoed and answered)", "> look" in text() and "satchel" in text(), tail(160))

    # a story waiting for one key (Nord and Bert's "Press any key to begin")
    check("reboot", reboot())
    ser.write(b"A"); pump(0.3)                      # arm a press for 4 s from now
    start_game("5")
    check("Nord and Bert waits for a key", "Press any key to begin" in text(), tail(160))
    check("the button at a key prompt returns to the menu", menu_back(10), tail(200))
    start_game("5")
    any_key()
    check("Nord and Bert starts afterwards", "Go to the Shopping Bizarre" in text() or "Shopping Bizarre" in text(), tail(160))

    # the button at the menu only redraws it
    check("reboot", reboot())
    ser.write(b"A"); pump(6)
    check("a press at the menu leaves the menu in place", "Type a number" in text(), tail(160))
    start_game("2")
    check("and the next story starts", "pitch black" in text(), tail(160))
    return finish()


if __name__ == "__main__":
    sys.exit(main())
