#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""
Version 4 test stories on the Arty S7-50: keys, lines, split screen, save and restore.

For each story this builds the V4 firmware and flash image, writes the user flash region
(offset 0x100000: the bitstream is not touched), resets the FPGA so it boots from flash,
and drives the game over the UART, checking a terminal emulator's screen (pyte).

Needs: riscv64-unknown-elf-gcc and openFPGALoader on PATH, pyserial and pyte, the board
connected with a bitstream whose SoC RAM is at least what the story needs (these stories need
32 KB). Run from this directory:

    python3 test_v4_stories.py [--port /dev/ttyUSB1] [--only keys,lines,screen,save,identity]
    ARTY_BITSTREAM=../fpga/build/servant_zvibe_arty_s7_50.bit python3 test_v4_stories.py ...   # a build not yet in flash

The stories are target/console/tests/data/v4*.z4 (sources beside them).
"""
import argparse
import os
import re
import subprocess
import sys
import time

import pyte
import serial
import serial.tools.list_ports

HERE = os.path.dirname(os.path.abspath(__file__))
SW = os.path.join(HERE, "..", "sw")
DATA = os.path.abspath(os.path.join(HERE, "..", "..", "..", "..", "..", "console", "tests", "data"))
USER_REGION = "0x100000"
# firmware + game + the four 64 KB save slots end before 0x50000 into the user region
IMAGE_BYTES = 0x50000
SAVES_OFFSET = 0x10000       # the save slots start here in the user region (flash_layout: 0x110000)
FTDI = (0x0403, 0x6010)

results = []


def check(name, cond, detail=""):
    results.append((name, bool(cond)))
    print(("PASS  " if cond else "FAIL  ") + name + (f"   [{detail}]" if detail and not cond else ""))
    return bool(cond)


def detect_port():
    ports = sorted(p.device for p in serial.tools.list_ports.comports() if (p.vid, p.pid) == FTDI)
    if not ports:
        sys.exit("no FT2232H found: is the Arty connected?")
    return ports[-1]


class Board:
    def __init__(self, port, rows=24, cols=80):
        self.port, self.rows, self.cols = port, rows, cols
        self.ser = None
        self.reply_delay = 0.0      # seconds before the terminal answers the size question
        self.reply_size = (rows, cols)
        self.reset_terminal()

    def reset_terminal(self):
        self.screen = pyte.Screen(self.cols, self.rows)
        self.stream = pyte.ByteStream(self.screen)
        self.text = ""               # everything printed, escape sequences removed
        self.raw = b""
        self.probes = 0
        self.answered = 0

    # --- flashing ---
    def flash(self, story, extra_make=()):
        subprocess.run(["make", "-C", SW, "flash-test", "V4=1", f"STORY={story}", *extra_make],
                       check=True, stdout=subprocess.DEVNULL)
        image = os.path.join(SW, "arty_flash_test.bin")
        trimmed = os.path.join(SW, "arty_flash_trimmed.bin")
        with open(image, "rb") as f, open(trimmed, "wb") as g:
            g.write(f.read(IMAGE_BYTES))
        if self.ser:
            self.ser.close(); self.ser = None
        subprocess.run(["openFPGALoader", "-b", "arty_s7_50", "-f", "--offset", USER_REGION, trimmed],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.power_cycle()

    def flash_program_only(self, story, extra_make=()):
        """Write only the firmware and story (the first 64 KB sector run), not the save slots."""
        subprocess.run(["make", "-C", SW, "flash-test", "V4=1", f"STORY={story}", *extra_make],
                       check=True, stdout=subprocess.DEVNULL)
        with open(os.path.join(SW, "arty_flash_test.bin"), "rb") as f:
            head = f.read(SAVES_OFFSET)
        trimmed = os.path.join(SW, "arty_flash_program.bin")
        with open(trimmed, "wb") as g:
            g.write(head)
        if self.ser:
            self.ser.close(); self.ser = None
        subprocess.run(["openFPGALoader", "-b", "arty_s7_50", "-f", "--offset", USER_REGION, trimmed],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.power_cycle()

    def power_cycle(self):
        """Reload the FPGA from flash: the same as a power-up (RAM is cleared, the game restarts)."""
        if self.ser:
            self.ser.close(); self.ser = None
        # ARTY_BITSTREAM=<file.bit>: load that bitstream into the FPGA (what a power-up with it in flash would
        # do) instead of reconfiguring from the one stored in flash; for testing a build before it is stored
        bit = os.environ.get("ARTY_BITSTREAM")
        cmd = ["openFPGALoader", "-b", "arty_s7_50"] + ([bit] if bit else ["-r"])
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(2.5)
        self.ser = serial.Serial(self.port, 115200, timeout=0.05)
        self.ser.reset_input_buffer()
        self.reset_terminal()

    # --- terminal ---
    def pump(self, seconds=0.3):
        end = time.time() + seconds
        while time.time() < end:
            data = self.ser.read(4096)
            if not data:
                continue
            self.raw += data
            self.stream.feed(data)
            self.text = re.sub(r"\x1b\[[0-9;?]*[A-Za-z]|\x1b[()][A-Z0-9]", "",
                               self.raw.decode("latin1"))
            self.probes = self.raw.count(b"\x1b[6n")
            while self.answered < self.probes:
                if self.reply_delay:
                    time.sleep(self.reply_delay)
                self.ser.write(f"\x1b[{self.reply_size[0]};{self.reply_size[1]}R".encode())
                self.answered += 1

    def wait_for(self, s, timeout=10):
        end = time.time() + timeout
        while time.time() < end:
            self.pump(0.2)
            if s in self.text:
                return True
        return False

    def send(self, data, gap=0.0):
        if isinstance(data, str):
            data = data.encode("latin1")
        if gap:
            for b in data:
                self.ser.write(bytes([b])); time.sleep(gap)
        else:
            self.ser.write(data)

    def rows_text(self):
        return [line.rstrip() for line in self.screen.display]

    def screen_has(self, s):
        return any(s in line for line in self.screen.display)

    def start_game(self):
        """The firmware waits for RETURN (its prompt was printed before the port was open, and it
        drops anything typed before the prompt), then probes the terminal and starts the story.
        Press RETURN once a second until the probe shows the story has started."""
        for _ in range(12):
            self.send("\r")
            self.pump(1.0)
            if self.probes >= 1:
                return True
        return False

    def count_after(self, marker, s):
        i = self.text.rfind(marker)
        return self.text[i:].count(s) if i >= 0 else 0


# ---------------------------------------------------------------- scenarios

def story(name):
    return os.path.join(DATA, name)


def scenario_keys(b):
    print("== v4keys: read_char keys and a line")
    b.flash(story("v4keys.z4"))
    check("boots to RETURN prompt and starts", b.start_game())
    check("story starts", b.wait_for("keys ready", 8), b.text[-200:])
    check("the player asked for the terminal size", b.probes >= 1)
    for label, data, want in [
        ("Return is key 13", "\r", "key=13"),
        ("arrow up is 129", "\x1b[A", "key=129"),
        ("arrow down is 130", "\x1b[B", "key=130"),
        ("arrow right is 132", "\x1b[C", "key=132"),
        ("arrow left is 131", "\x1b[D", "key=131"),
        ("Delete (DEL) is 8", "\x7f", "key=8"),
        ("a letter is its ASCII code", "a", "key=97"),
    ]:
        n = b.text.count(want)
        b.send(data)
        ok = b.wait_for(want, 4) and b.text.count(want) > n
        check(label, ok, b.text[-80:])
    # a lone Escape (the byte after it arrives much later than an arrow key's tail)
    n27 = b.text.count("key=27")
    b.send("\x1b"); time.sleep(0.6); b.send("x")
    b.wait_for("key=120", 4)
    check("a lone Escape is 27 and the next key is its own", b.text.count("key=27") > n27 and "key=120" in b.text, b.text[-80:])
    # CR LF is one key
    n13 = b.text.count("key=13")
    b.send("\r\n")
    b.wait_for("key=13", 4); b.pump(0.5)
    check("CR LF is a single Return", b.text.count("key=13") == n13 + 1, f"{b.text.count('key=13') - n13} keys")
    b.send("q")
    check("q ends the keys and a line is read", b.wait_for("line? ", 5))
    b.send("hello\r")
    check("the line comes back", b.wait_for("line=[hello]", 5), b.text[-80:])


def scenario_lines(b):
    print("== v4lines: line input")
    b.flash(story("v4lines.z4"))
    check("starts", b.start_game() and b.wait_for("lines ready", 8), b.text[-200:])
    b.send("look around\r")
    check("a typed line", b.wait_for("you said: look around", 5), b.text[-100:])
    b.send("abc\x7f\x7fd\r")
    check("backspace edits the line", b.wait_for("you said: ad", 5), b.text[-100:])
    b.send("abcdefghijklmnopqrstuvwxyz" + "abcdefghijklmnopqrstuvwxyz" + "\r", gap=0.003)
    b.wait_for("you said: abcdefghijklmnopqrstuvwxyzabcdefghijklm", 5)
    check("a line is cut at the story's buffer (39 characters)",
          "you said: abcdefghijklmnopqrstuvwxyzabcdefghijklm" in b.text and
          "you said: abcdefghijklmnopqrstuvwxyzabcdefghijklmn" not in b.text)
    b.send("x\r\n")
    check("CR LF ends a line once", b.wait_for("you said: x", 5))
    check("the screen shows the prompt on the last row", b.screen_has("> "))


def scenario_screen(b):
    print("== v4screen: split window, reverse-video bar, scrolling")
    b.flash(story("v4screen.z4"))
    check("starts", b.start_game() and b.wait_for("press a key", 10), b.text[-200:])
    b.pump(0.5)
    rows = b.screen.display
    check("status bar text", "ZVIBE V4" in rows[0] and "Score: 7" in rows[0], rows[0])
    check("status bar is reverse video",
          all(b.screen.buffer[0][c].reverse for c in range(1, 78)),
          "".join("R" if b.screen.buffer[0][c].reverse else "." for c in range(80)))
    check("row two", rows[1].startswith("Row two"), rows[1])
    check("bold word on row three", rows[2].startswith("    bold") and all(b.screen.buffer[2][c].bold for c in range(4, 8)), rows[2])
    check("the story saw an 80 x 24 screen", "size 24x80" in b.text, b.text[-200:])
    b.send("x")
    check("a key read", b.wait_for("key=120", 5))
    b.send("hi\r")
    check("a line read", b.wait_for("got: hi", 5))
    check("cursor query", b.wait_for("cursor ", 5))
    check("the story finished", b.wait_for("done", 5))


def scenario_save(b):
    print("== v4save: save, power cycle, restore")
    b.flash(story("v4save.z4"))
    check("starts", b.start_game() and b.wait_for("v4save ready", 8), b.text[-200:])
    for n in (1, 2, 3):
        b.send("i")
        check(f"count {n}", b.wait_for(f"counter={n}", 5))
    b.send("s")
    check("save stores 1", b.wait_for("save result 1", 20), b.text[-120:])
    b.send("i"); b.wait_for("counter=4", 5)
    b.power_cycle()
    check("restarts after a power cycle", b.start_game() and b.wait_for("v4save ready", 8))
    b.send("p")
    check("a fresh game starts at zero", b.wait_for("counter=0", 5))
    b.send("r")
    check("restore resumes at the save with 2", b.wait_for("save result 2", 20), b.text[-120:])
    b.send("p")
    n = b.text.count("counter=3")
    check("the counter is back to 3", b.wait_for("counter=3", 5))
    b.send("i")
    check("the game goes on", b.wait_for("counter=4", 5))
    # a second save on top, then restore after another power cycle (the newest wins)
    b.send("s")
    check("a second save", b.wait_for("save result 1", 20) and b.text.count("save result 1") >= 1)
    b.power_cycle()
    b.start_game(); b.wait_for("v4save ready", 8)
    b.send("r")
    check("the newer save is restored", b.wait_for("save result 2", 20))
    b.send("p")
    check("with counter 4", b.wait_for("counter=4", 5))


def scenario_identity(b):
    print("== identity: another release of the story must not accept v4save's save")
    # v4bigsave would be the natural other story, but it needs 64 KB of SoC RAM. A copy of v4save
    # with a different release number is "another release of the same story", the case that
    # matters: a packed save leaves out every byte equal to the story, so the player compares
    # the story's release, serial and checksum itself.
    import tempfile
    with open(story("v4save.z4"), "rb") as f:
        data = bytearray(f.read())
    # first make sure v4save has a save in flash (the previous scenario left one, but do not rely on it)
    b.flash(story("v4save.z4"))
    check("v4save starts", b.start_game() and b.wait_for("v4save ready", 8))
    b.send("iis")
    check("a save is stored", b.wait_for("save result 1", 20))
    for label, offset in (("release", 3), ("serial number", 0x12), ("checksum", 0x1D)):
        other = bytearray(data)
        other[offset] ^= 1
        with tempfile.NamedTemporaryFile(suffix=".z4", delete=False) as t:
            t.write(other)
        # write only the program part, leaving the save slots as they are: a save made by v4save stays
        b.flash_program_only(t.name)
        os.unlink(t.name)
        check(f"a story with another {label} starts", b.start_game() and b.wait_for("v4save ready", 8), b.text[-100:])
        b.send("r")
        check(f"its restore refuses the save (different {label})", b.wait_for("different story or release", 20), b.text[-160:])
        check(f"and the game goes on with result 0 ({label})", b.wait_for("restore result 0", 5))
    # the right story still restores it
    b.flash_program_only(story("v4save.z4"))
    check("the original story starts again", b.start_game() and b.wait_for("v4save ready", 8))
    b.send("r")
    check("and restores its save", b.wait_for("save result 2", 20))
    b.send("p")
    check("with its counter", b.wait_for("counter=2", 5))


def scenario_timing(b):
    print("== timing: escape sequences spread out, and a slow answer to the size question")
    b.flash(story("v4keys.z4"))
    check("starts", b.start_game() and b.wait_for("keys ready", 8))
    # an arrow key whose three bytes arrive 5 ms apart (a slow link or terminal program)
    n = b.text.count("key=129")
    b.send("\x1b[A", gap=0.005)
    check("an arrow key spread over 10 ms is still one key (129)", b.wait_for("key=129", 4) and b.text.count("key=129") == n + 1 and "key=27" not in b.text, b.text[-80:])
    n = b.text.count("key=131")
    b.send("\x1b[D", gap=0.008)
    check("an arrow key with 8 ms between bytes is key 131", b.wait_for("key=131", 4) and b.text.count("key=131") == n + 1 and "key=27" not in b.text, b.text[-80:])
    # a lone Escape still comes out as Escape (the wait for a tail ends)
    b.send("\x1b"); b.pump(0.3)
    check("a lone Escape is delivered as 27 within the wait", "key=27" in b.text, b.text[-60:])
    b.send("q")
    # the terminal answers the size question 300 ms late, with a width that is not the default
    b.flash(story("v4screen.z4"))
    b.reply_delay = 0.3
    b.reply_size = (24, 60)
    check("v4screen starts", b.start_game() and b.wait_for("press a key", 10), b.text[-100:])
    check("a size answer 300 ms late is still used (24 x 60)", "size 24x60" in b.text, b.text[-200:])
    b.reply_delay = 0.0
    b.reply_size = (24, 80)


SCENARIOS = {"keys": scenario_keys, "lines": scenario_lines, "screen": scenario_screen,
             "save": scenario_save, "identity": scenario_identity, "timing": scenario_timing}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port")
    ap.add_argument("--only", default=",".join(SCENARIOS))
    args = ap.parse_args()
    b = Board(args.port or detect_port())
    print(f"[ok] UART {b.port}")
    for name in args.only.split(","):
        SCENARIOS[name](b)
    bad = [n for n, ok in results if not ok]
    print(f"\n{len(results) - len(bad)}/{len(results)} checks passed")
    for n in bad:
        print("  FAILED:", n)
    return 0 if not bad else 1


if __name__ == "__main__":
    sys.exit(main())
