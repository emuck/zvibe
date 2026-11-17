#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""
The large version 4 stories on the Arty S7-50 with 128 KB of SoC RAM: Trinity (r15) and A Mind Forever Voyaging (r77).
Each is started, played a little (a move to another place), saved, moved back, restored (the place is back) and restored
again after a power cycle.

For each story, the firmware for it and the 128 KB design:

    cd ../sw && make flash-test V4=1 GAME=<name> STORY=<story.z4>
    openFPGALoader -b arty_s7_50 -f --offset 0x100000 arty_flash_test.bin       # the user region only
    ARTY_RAM_KB=128 make -C ../fpga build-nonproject
    python3 test_v4_big_stories.py trinity --story-name trinity --bit ../fpga/build/servant_zvibe_arty_s7_50.bit

    python3 test_v4_big_stories.py {trinity,amfv} [--port /dev/ttyUSB2] [--bit file.bit]

It overwrites the first save slot of the user flash region.
"""
import argparse
import re
import sys

from probe_bureaucracy import T, detect_port
from test_v4_bureaucracy import check, reply, power_cycle, similar, fails
import test_v4_bureaucracy as common
import subprocess
import time

# begin: what starts a new game; there: a move to another place and back; title: how the place's name is recognised
STORIES = {
    "trinity": dict(begin=b"x", out="n", back="s", where="Palace Gate", moved="Broad Walk"),
    # A Mind Forever Voyaging starts in Communications Mode: an outlet code takes the player to a place
    "amfv": dict(begin=b"x", pre="ppcc", out="rcro", back="ppcc", where="Project Control Center",
                 moved="Research Center Rooftop"),
}


def start(t, story):
    ok = t.start()
    t.pump(3)
    t.ser.write(STORIES[story]["begin"])
    t.pump(6)
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("story", choices=sorted(STORIES))
    ap.add_argument("--port")
    ap.add_argument("--bit")
    args = ap.parse_args()
    st = STORIES[args.story]
    port = args.port or detect_port()
    if args.bit:
        subprocess.run(["openFPGALoader", "-b", "arty_s7_50", args.bit], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(2.5)
    t = T(port)
    check(f"{args.story}: starts and asks for the terminal size", start(t, args.story))
    if st.get("pre"):
        t.cmd(st["pre"], 4)
    here = reply(t, "look")
    check(f"{args.story}: look answers", len(here) > 20, here)
    if st["out"]:
        check("the first place is " + st["where"], st["where"] in here, here)
        there = reply(t, st["out"])
        look2 = reply(t, "look")
        check("a move changes the place", st["moved"] in look2 and similar(look2, here) < 0.9, f"{look2!r}")
        t.cmd("save", 8)
        check("SAVE is accepted", "completed" in common_tail(t) or "saved" in common_tail(t), common_tail(t))
        reply(t, st["back"])
        back = reply(t, "look")
        check("moved back to the first place", st["where"] in back and similar(back, look2) < 0.9, back)
        t.cmd("restore", 9)
        after = reply(t, "look")
        check("RESTORE brings back the place at the save", similar(after, look2) >= 0.85, f"{after!r} vs {look2!r}")
        t = power_cycle(t, port, args.bit)
        check("starts again after a power cycle", start(t, args.story))
        t.cmd("restore", 9)
        after2 = reply(t, "look")
        check("RESTORE after a power cycle brings back the place", similar(after2, look2) >= 0.85, f"{after2!r}")
    print("\nFAILED:" if fails else "\nALL PASSED", fails if fails else "")
    return 1 if fails else 0


def common_tail(t):
    return t.text()[-300:]


if __name__ == "__main__":
    sys.exit(main())
