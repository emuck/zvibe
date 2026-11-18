#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""
MAX10 08 Eval board: save and restore on the on-chip UFM, across a power cycle.

Plays The Restaurant at the End of the Universe: saves on the Ramp, walks down, restores (back on the Ramp), saves again
down there, then reloads the .sof through JTAG (the UFM keeps its contents, the CPU restarts: a power cycle for the
saves) and restores: the newest save must come back.

    cd ../fpga && make program-complete GAME=restaurant
    python3 test_save_restore.py [--port /dev/serial/by-id/...CP2102...]

Needs pyserial and quartus_pgm in PATH. The UART adapter is found by its USB id (CP210x); ttyUSB numbers change.
"""
import argparse
import glob
import os
import subprocess
import sys
import time

import serial

SOF = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "fpga", "output_files",
                   "servant_zvibe_max10_08_eval_xip.sof")


def find_port():
    found = sorted(glob.glob("/dev/serial/by-id/*CP210*")) + sorted(glob.glob("/dev/ttyUSB*"))
    if not found:
        sys.exit("no UART adapter found: use --port")
    return found[0]


class Board:
    def __init__(self, port):
        self.port = port
        self.ser = serial.Serial(port, 115200, timeout=0.2)

    def read(self, sec):
        out, end, last = b"", time.time() + sec, time.time()
        while time.time() < end:
            d = self.ser.read(512)
            if d:
                out += d
                last = time.time()
            elif out.rstrip().endswith(b">") and time.time() - last > 1.5:
                break
        return out.decode("latin1")

    def cmd(self, line, wait=40):
        self.ser.reset_input_buffer()
        self.ser.write(line.encode() + b"\r")
        return self.read(wait)


results = []


def check(name, ok, detail=""):
    results.append(ok)
    print(("PASS " if ok else "FAIL ") + name + ("" if ok else "  -> " + detail[-260:].replace("\r\n", " | ")))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port")
    ap.add_argument("--sof", default=SOF)
    args = ap.parse_args()
    port = args.port or find_port()
    print(f"[port {port}]")
    b = Board(port)
    b.ser.write(b"~reset~\r")
    time.sleep(3)
    b.read(10)
    r = b.cmd("look")
    check("starts on the Ramp", "On the Ramp" in r, r)
    r = b.cmd("save")
    check("save accepted", "Ok" in r and "fail" not in r.lower() and "error" not in r.lower(), r)
    r = b.cmd("down")
    check("walked down (the location changed)", "can't go" not in r.lower() and "On the Ramp" not in r.split("\n", 3)[-1][:60], r)
    r = b.cmd("restore")
    check("restore accepted", "[Game restored]" in r, r)
    r = b.cmd("look")
    check("restore: back on the Ramp", "On the Ramp" in r, r)
    b.cmd("down")
    r = b.cmd("save")
    check("second save (down there) accepted", "Ok" in r and "fail" not in r.lower() and "error" not in r.lower(), r)
    b.cmd("up")                         # the state after the save differs from the save
    b.ser.close()
    p = subprocess.run(["quartus_pgm", "-m", "jtag", "-o", "p;" + args.sof], capture_output=True, text=True)
    check("reconfigured (a power cycle for the saves)", "Configuration succeeded" in p.stdout, p.stdout[-200:])
    b = Board(port)
    time.sleep(2)
    b.ser.write(b"\r")
    b.read(15)
    r = b.cmd("restore")
    check("restore after the cycle accepted", "[Game restored]" in r, r)
    r = b.cmd("look")
    check("the newest save is back (not on the Ramp)", "On the Ramp" not in r[:80] and len(r) > 40, r)
    print("ALL PASSED" if all(results) else "SOME FAILED")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
