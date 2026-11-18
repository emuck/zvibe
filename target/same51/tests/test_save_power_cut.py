#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""Hardware test: a power cut during a save leaves the game's previous save or the new one, whole.

Needs the menu firmware built with the test hooks (never a release build):
    cd target/same51 && make multi-program EXTRA_CFLAGS=-DSAVE_TEST_HOOKS=1 MULTI_ARGS="\\
        --story Restaurant=../../games/catalog/restaurant.z3 --story Hitchhiker=$HOME/infocom/hitchhiker.z3"

For each point of a save (save_store.h: 1 before anything is written ... 5 area committed, not read back) and for each
of two games: save a state, play on, ask for a power cut at that point (the hidden command "~fault N~"), save again (the
board stops dead), reset the board and restore. The turn count the game reports shows which save came back: the old one
for a cut before anything is written (1), the new one once the scratch is committed (3, 4, 5: recovered at the start
when the game's own area was not finished), either at 2, and never anything else.
    python3 tests/test_save_power_cut.py [serial-port]
"""
import re, subprocess, sys, time
from multi_common import *

GAMES = [("1", "Restaurant", "Ok."), ("2", "Hitchhiker", "Ok.")]
# What may come back after a cut at each point. At 2 the scratch's last page is only in the page buffer: the device may
# commit it anyway (seen on the board: the new save came back), so either whole save is right there.
EXPECT = {1: ("old",), 2: ("old", "new"), 3: ("new",), 4: ("new",), 5: ("new",)}


def main():
    print(f"[port {PORT}]")
    check("board starts at the menu", reboot())
    for num, name, ok in GAMES:
        for point in (1, 2, 3, 4, 5):
            reboot(); start_game(num)
            vary()
            old = turns()
            check(f"{name} cut {point}: the first save completes", save_game(ok) is not None, tail(200))
            for _ in range(3):
                type_line("look", 1.5)
            new = turns()
            ser.write(f"~fault {point}\r".encode()); pump(1.0)
            ser.write(b"save\r"); pump(4.0)                     # the board stops inside this save
            stopped = "Ok." not in last_line()
            check(f"{name} cut {point}: the second save did not complete (the board stopped)", stopped, tail(160))
            reboot(); start_game(num)
            got = restore_game(ok)
            t = turns() if got else None
            allowed = {"old": old, "new": new}
            which = [k for k in EXPECT[point] if allowed[k] == t]
            check(f"{name} cut {point}: restore gives a whole save, the {' or '.join(EXPECT[point])} one",
                  got and bool(which), f"restored={got} turns={t} old={old} new={new}")
    return finish()


if __name__ == "__main__":
    sys.exit(main())
