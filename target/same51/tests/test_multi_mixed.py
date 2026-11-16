#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""Hardware test of the multi-game menu firmware with version 3 and version 4 games together.

Menu order: 1 Restaurant, 2 Hitchhiker's Guide (both V3), 3 Czech (the V3 conformance suite),
4 Bureaucracy, 5 Nord and Bert (V4). Checks that each starts from its number, that Czech passes in
the menu firmware, that a V3 line longer than its buffer is handled, that quitting returns to the menu
and that saves in V3 and V4 games survive a reset, each in its own slot.

Setup:   pip install pyserial pyte
Flash:   cd target/same51 && make multi-program MULTI_ARGS="\\
             --story Restaurant=../../games/catalog/restaurant.z3 --story Hitchhiker=$HOME/infocom/hitchhiker.z3 \\
             --story Czech=../../games/catalog/czech.z3 \\
             --story Bureaucracy=$HOME/infocom/library/z4/bureaucracy/bureaucracy-r116-s870602.z4 \\
             --story Nord-and-Bert=$HOME/infocom/library/z4/nordandbert/nordandbert-r19-s870722.z4"
Run:     python3 tests/test_multi_mixed.py [serial-port]
"""
import sys
from multi_common import *


def main():
    print(f"[port {PORT}]")
    check("board starts at the menu", reboot(), text()[-200:])
    for num, name in (("1", "Restaurant"), ("2", "Hitchhiker"), ("3", "Czech"), ("4", "Bureaucracy"), ("5", "Nord-and-Bert")):
        check(f"menu lists {num}. {name}", f"{num}. {name}" in text())

    print("== version 3 games ==")
    start_game("1")
    check("Restaurant starts", "THE RESTAURANT AT THE END OF THE UNIVERSE" in text() and "On the Ramp" in text(), text()[-300:])
    check("Restaurant: quit returns to the menu", quit_to_menu(), tail(300))
    start_game("2")
    check("Hitchhiker starts", "THE HITCHHIKER'S GUIDE TO THE GALAXY" in text() and "pitch black" in text(), text()[-300:])
    type_line("a" * 120, 3)
    check("Hitchhiker: a line longer than the buffer is taken, not fatal", "don't know the word" in text(), tail(300))
    type_line("look", 2.5)
    check("Hitchhiker: still alive after it", text().count("It is pitch black") >= 2, tail(200))
    check("Hitchhiker: quit returns to the menu", quit_to_menu(), tail(300))

    print("== Czech, the version 3 conformance suite, in the menu firmware ==")
    start_game("3")
    ok = wait_for("Last test:", 20)
    t = text()
    check("Czech: runs to its end", ok, t[-300:])
    check("Czech: 368 tests, 0 failed, 19 print tests", "Performed 368 tests." in t and "Passed: 349, Failed: 0, Print tests: 19" in t, t[-400:])
    check("Czech: did not crash", "Didn't crash: hooray!" in t)
    check("Czech: the game ends and RETURN gives the menu", wait_for("Press RETURN for the menu", 8), t[-200:])
    ser.write(b"\r"); pump(2)
    check("Czech: menu again", "Type a number" in text())

    print("== version 4 games beside them ==")
    start_game("4")
    check("Bureaucracy starts", "Copyright (C) 1987 Infocom" in text(), text()[-300:])
    type_line("", 2)
    any_key()
    for f in FORM:
        type_line(f, 1.0)
    pump(2)
    check("Bureaucracy: quit returns to the menu", quit_to_menu(), tail(300))
    start_game("5")
    check("Nord and Bert starts", "Press any key to begin" in text(), text()[-300:])
    any_key()
    check("Nord and Bert: quit returns to the menu", quit_to_menu(), tail(300))

    print("== saves: a slot each, V3 and V4, across a reset ==")
    reboot()
    start_game("2")
    type_line("turn on light", 3)
    vary()
    type_line("save", 5)
    check("Hitchhiker: save completes", "Ok." in text(), tail(200))
    check("Hitchhiker: reboot", reboot())
    start_game("1")
    vary()
    type_line("save", 5)
    check("Restaurant: save completes", "Ok." in text(), tail(200))
    check("Restaurant: reboot", reboot())
    start_game("5")
    any_key()
    vary()
    type_line("save", 4)
    check("Nord and Bert: save completes", "Save, completed" in text(), tail(200))
    check("Nord and Bert: reboot", reboot())

    start_game("2")
    type_line("look", 3)
    check("Hitchhiker: a fresh game is in the dark", "pitch black" in text()[-300:], tail(200))
    type_line("restore", 6)
    type_line("look", 3)
    check("Hitchhiker: restores its own save (the light is on)", "Near the exit leading south is a phone" in text(), tail(300))
    check("Hitchhiker: back to the menu", reboot())
    start_game("1")
    type_line("restore", 6)
    check("Restaurant: restores its own save", "Ok." in text()[-200:] and "failed" not in text().lower(), tail(200))
    check("Restaurant: back to the menu", reboot())
    start_game("5")
    any_key()
    type_line("restore", 5)
    check("Nord and Bert: restores its own save", "estored" in text() and "Beginning" in text(), tail(300))
    return finish()


if __name__ == "__main__":
    sys.exit(main())
