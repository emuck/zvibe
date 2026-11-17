#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""
Bureaucracy r116 (version 4, 64 KB SoC RAM) on the Arty S7-50: the licence form, a walk through the first part of the
game, then SAVE, a change of state, RESTORE (the state is back), and RESTORE again after a power cycle.

Needs the 64 KB design and the firmware for the story:

    cd ../sw && make flash-test V4=1 GAME=bureaucracy STORY=<bureaucracy-r116-s870602.z4>
    openFPGALoader -b arty_s7_50 -f --offset 0x100000 arty_flash_test.bin       # the user region only
    ARTY_RAM_KB=64 make -C ../fpga build-nonproject                             # or an existing 64 KB bitstream
    python3 test_v4_bureaucracy.py --bit ../fpga/build/servant_zvibe_arty_s7_50.bit [--port /dev/ttyUSB2]

--bit is loaded into the FPGA before the test and again for the power cycle (a bitstream not yet stored in flash).
Without it the FPGA is left as it is and the power cycle is done with `openFPGALoader -r` (reload from flash).
It overwrites the first save slot of the user flash region.
"""
import argparse
import re
import subprocess
import sys
import time

from probe_bureaucracy import T, FORM, WALK, answer_waitress, detect_port

fails = []


def check(name, ok, detail=""):
    print(("PASS  " if ok else "FAIL  ") + name + ("" if ok else f"   [{detail}]"))
    if not ok:
        fails.append(name)


def reply(t, line, wait=2.5):
    """The text the story printed in answer to one command (without the echoed command and the prompt)."""
    n = len(t.text())
    t.cmd(line, wait)
    return " ".join(t.text()[n:].replace(">>", " ").split())


def room(text):
    """A room description without the status bar's changing numbers (Bureaucracy shows blood pressure there)."""
    return " ".join(re.sub(r"\d+", "", text).split())


def similar(a, b):
    import difflib
    return difflib.SequenceMatcher(None, room(a), room(b)).ratio()


def power_cycle(t, port, bit):
    t.ser.close()
    cmd = ["openFPGALoader", "-b", "arty_s7_50"] + ([bit] if bit else ["-r"])
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(2.5)
    t2 = T(port)
    return t2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port")
    ap.add_argument("--bit")
    args = ap.parse_args()
    port = args.port or detect_port()
    if args.bit:
        subprocess.run(["openFPGALoader", "-b", "arty_s7_50", args.bit], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(2.5)
    t = T(port)
    check("the game starts and asks for the terminal size", t.start())
    t.pump(2)
    check("the title and the RESTORE prompt are shown", "Type RESTORE" in t.text(), t.text()[-200:])
    t.cmd("", 3)                                  # a new game
    t.pump(1)
    t.ser.write(b"x"); t.pump(1)                  # any key
    for f in FORM:
        t.cmd(f, 1.2)
    t.pump(2)
    for c in WALK:
        t.cmd(c, 2.0)
        answer_waitress(t)
    t.pump(1)
    where = reply(t, "look")
    inv = reply(t, "inventory")
    check("the walk reached a room (look answers)", len(where) > 20, where)
    check("inventory answers", len(inv) > 5, inv)

    t.cmd("save", 5)
    check("SAVE is accepted", "[SAVE completed.]" in t.text()[-300:], t.text()[-200:])
    changed = reply(t, "drop all")
    inv2 = reply(t, "inventory")
    check("the state changed after the save (dropping everything empties the inventory)", inv2 != inv, inv2)
    t.cmd("restore", 6)
    inv3 = reply(t, "inventory")
    check("RESTORE brings back the saved state", inv3 == inv, f"{inv3!r} vs {inv!r}")
    again = reply(t, "look")
    check("and the room is the same", similar(again, where) >= 0.85, f"{similar(again, where):.2f}: {again!r} vs {where!r}")

    t = power_cycle(t, port, args.bit)
    check("the game starts again after a power cycle", t.start())
    t.pump(2)
    t.cmd("restore", 7)
    inv4 = reply(t, "inventory")
    check("RESTORE after a power cycle brings back the saved state", inv4 == inv, f"{inv4!r} vs {inv!r}")
    again = reply(t, "look")
    check("and the room is the same after the power cycle", similar(again, where) >= 0.85,
          f"{similar(again, where):.2f}: {again!r} vs {where!r}")

    print("\nFAILED:" if fails else "\nALL PASSED", fails if fails else "")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
