#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""
Measurements on a PROBE=1 build of the Arty V4 firmware with Bureaucracy r116 (see sw/Makefile, PROBE):

  stack   play through the licence form and the first part of the game (the walkthrough that exercises
          the most text, parsing, abbreviations and save/restore), then ask for the stack high-water mark
  spin    time the player's busy-wait loop (rv_rx_get) to calibrate RV_SPIN_PER_MS

    python3 probe_bureaucracy.py stack [--port /dev/ttyUSB1]
    python3 probe_bureaucracy.py spin  [--port /dev/ttyUSB1]

The SoC must run the 64 KB design with the probe image flashed (cd ../sw && make flash-test V4=1 PROBE=1
STORY=<bureaucracy-r116>.z4, then program-flash and reload the bitstream).
"""
import argparse
import re
import sys
import time

import pyte
import serial
import serial.tools.list_ports

FTDI = (0x0403, 0x6010)
FORM = ["Smith", "John", "Q", "M", "42", "Main Street", "Springfield", "CA", "90210", "5551212", "Acme",
        "Blue", "Pat", "Chris", "Jones", "Elm St", "Boston", "red", "Lee", "X", "12", "Z"]
WALK = ["w", "take all", "e", "open door", "give us excess card to man", "give beezer card to man", "take bag",
        "w", "press button", "e", "open door", "e", "open mailbox", "take leaflet", "read leaflet", "n", "w",
        "examine software", "ask clerk about software", "yes", "examine cart", "open case", "take adventure",
        "give adventure to clerk", "put recipe cart in case", "e", "drop bag", "e", "wait"]


def detect_port():
    ports = sorted(p.device for p in serial.tools.list_ports.comports() if (p.vid, p.pid) == FTDI)
    if not ports:
        sys.exit("no FT2232H found")
    return ports[-1]


class T:
    def __init__(self, port):
        self.ser = serial.Serial(port, 115200, timeout=0.05)
        self.scr = pyte.Screen(80, 24)
        self.st = pyte.ByteStream(self.scr)
        self.raw = b""
        self.ans = 0

    def pump(self, secs):
        end = time.time() + secs
        while time.time() < end:
            d = self.ser.read(4096)
            if d:
                self.raw += d
                self.st.feed(d)
                while self.ans < self.raw.count(b"\x1b[6n"):
                    self.ser.write(b"\x1b[24;80R")
                    self.ans += 1

    def text(self):
        return re.sub(r"\x1b\[[0-9;?]*[A-Za-z]|\x1b[()][A-Z0-9]", "", self.raw.decode("latin1"))

    def cmd(self, line, wait=1.5):
        self.ser.write(line.encode() + b"\r")
        self.pump(wait)

    def start(self):
        for _ in range(12):
            self.ser.write(b"\r")
            self.pump(1.0)
            if self.ans:
                return True
        return False


def answer_waitress(t):
    """The restaurant asks a run of questions: answer with the first option offered, as the host replay did."""
    for _ in range(40):
        tail = " ".join(t.text()[-300:].lower().split())
        if not tail.rstrip().endswith(">>"):
            return
        if "yes or no" in tail:
            a = "yes"
        else:
            q = tail.rsplit("sir. ", 1)[-1] if "sir. " in tail else tail
            q = re.sub(r'[?"]', "", q).strip(" >")
            opts = [o for o in re.split(r",\s*|\s+or\s+", q) if o.strip()]
            a = opts[0].split()[-1] if (" or " in q or "," in q) and opts else "yes"
        t.cmd(a, 1.2)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("what", choices=["stack", "spin"])
    ap.add_argument("--port")
    ap.add_argument("--n", type=int, default=2000, help="thousand iterations for spin")
    args = ap.parse_args()
    t = T(args.port or detect_port())
    if not t.start():
        sys.exit("the game did not start")
    t.pump(2)
    if args.what == "spin":
        # the prompt for the first line is the "RESTORE or start anew" one: a command prompt, so ~spin~ works there
        for n in (args.n // 10, args.n, args.n):
            t.raw = b""
            t.ser.write(f"~spin~ {n}".encode() + b"\r")
            t0 = None
            end = time.time() + 600
            while time.time() < end:
                t.pump(0.02)
                s = t.text()
                if t0 is None and "[spin start]" in s:
                    t0 = time.time()
                if "[spin done" in s:
                    dt = time.time() - t0
                    print(f"{n * 1000} iterations in {dt:.3f} s: {n * 1000 / dt / 1000:.1f} iterations per millisecond")
                    break
            else:
                sys.exit("no answer")
        return
    t.cmd("", 3)                     # new game
    t.pump(1)
    t.ser.write(b"x"); t.pump(1)     # any key
    for f in FORM:
        t.cmd(f, 1.2)
    t.pump(2)
    for c in WALK:
        t.cmd(c, 2.0)
        answer_waitress(t)
    t.cmd("save", 4)
    t.cmd("look", 2)
    t.cmd("restore", 5)
    t.cmd("~stack~", 1.5)
    s = t.text()
    m = re.findall(r"\[stack: high-water (\d+) bytes of (\d+) free\]", s)
    print(m[-1] if m else "no stack report; tail:\n" + s[-300:])


if __name__ == "__main__":
    main()
