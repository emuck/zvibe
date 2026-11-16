#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""Hardware test of the multi-game menu firmware with the five Infocom version 4 stories.

Menu order: 1 Bureaucracy, 2 Trinity, 3 A Mind Forever Voyaging, 4 Nord and Bert, 5 Generic. Checks
that every story starts from its number, that quitting returns to the menu, that each story has a
save slot of its own and that a save survives a reset of the board.

Setup:   pip install pyserial pyte
Flash:   cd target/same51 && make multi-program \\
             MULTI_ARGS="--only bureaucracy,trinity,amfv,nordandbert,generic-v4"
Run:     python3 tests/test_multi.py [serial-port]
(The registry's V4 entries point at ~/infocom/library/z4; see games/registry.json.)
"""
import sys
from multi_common import *

GAMES = [  # menu number, name, text the story shows when it starts
    ("1", "Bureaucracy", "Copyright (C) 1987 Infocom"),
    ("2", "Trinity", "Press any key to begin"),
    ("3", "A Mind Forever Voyaging", "PART I"),
    ("4", "Nord and Bert", "Press any key to begin"),
    ("5", "Generic", "GENERIC"),
]

# (menu number, name, commands that make the state recognisable, message of a completed save,
#  text the restored screen shows, message of a completed restore)
SAVES = [
    ("2", "Trinity", [], "SAVE completed", "Palace Gate", "RESTORE completed"),
    ("3", "A Mind Forever Voyaging", ["ppcc"], "Ok, saved", "PRISM Project Control Center", "Ok, restored"),
    ("4", "Nord and Bert", [], "Save, completed", "Beginning", "estored"),
]


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
            check("Generic: ~reset~ restarts the story", "Release 6" in text() and "Okay, what do you want to do now?" in text(), text()[-300:])
            reboot()
            continue
        else:
            any_key()
        check(f"{name}: quit returns to the menu", quit_to_menu(), text()[-300:])

    print("== a save slot for each story, kept across a reset ==")
    reboot()
    for num, name, setup, saved, shows, restored in SAVES:
        start_game(num)
        any_key()
        for line in setup:
            type_line(line, 3)
        vary()
        check(f"{name}: save completes", save_game(saved) is not None, text()[-250:])
        check(f"{name}: back to the menu", reboot())
    for num, name, setup, saved, shows, restored in SAVES:
        start_game(num)
        any_key()
        okr = restore_game(restored)
        t = text()
        check(f"{name}: restores its own save after a reset", okr and shows in t, t[-300:])
        check(f"{name}: back to the menu", reboot())
    return finish()


if __name__ == "__main__":
    sys.exit(main())
