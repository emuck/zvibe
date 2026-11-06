#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""
The console's V4 terminal player (zvibe_v4term.c) on a real pseudo-terminal.

Plays data/v4keys.z4, a story that prints the ZSCII code of every key it reads and
then echoes one typed line, and checks what the player turns the terminal's bytes
into: Return, Delete, lone Escape, Escape followed by a key, cursor keys, escape
sequences the story has no key for, a line longer than the screen, and that Ctrl-C
leaves the terminal as it found it.

Needs a console binary built with ZVIBE_ENABLE_V4=1 (make test-v4term does that).
Usage: test_v4term_pty.py [path/to/zvibe_console]
"""
import fcntl
import os
import re
import select
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import termios
import time

HERE = os.path.dirname(os.path.abspath(__file__))
BIN = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "build_v4", "bin", "zvibe_console")
STORY = os.path.join(HERE, "data", "v4keys.z4")

checks = failed = 0


def check(cond, what):
    global checks, failed
    checks += 1
    if not cond:
        failed += 1
        print("FAIL:", what)


class Session:
    def __init__(self, rows=24, cols=80, story=STORY, cwd=HERE):
        self.master, self.slave = os.openpty()
        fcntl.ioctl(self.slave, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
        self.before = termios.tcgetattr(self.master)

        def child():
            os.setsid()
            fcntl.ioctl(0, termios.TIOCSCTTY, 0)

        self.proc = subprocess.Popen([BIN, story], stdin=self.slave, stdout=self.slave, stderr=self.slave,
                                     preexec_fn=child, cwd=cwd)
        self.out = b""

    def read(self, timeout=3.0, until=None):
        """Collect output until `until` (bytes/regex) appears or the time is up."""
        end = time.time() + timeout
        while time.time() < end:
            if until is not None and re.search(until, self.out):
                return True
            r, _, _ = select.select([self.master], [], [], 0.05)
            if r:
                try:
                    data = os.read(self.master, 4096)
                except OSError:
                    break
                if not data:
                    break
                self.out += data
        return until is None or bool(re.search(until, self.out))

    def send(self, data):
        os.write(self.master, data)

    def key(self, data, want, timeout=2.0):
        """Send bytes and wait for the story to report `want` as the next key code."""
        mark = len(self.out)
        self.send(data)
        end = time.time() + timeout
        pat = re.compile(rb"key=(\d+)")
        while time.time() < end:
            m = pat.search(self.out, mark)
            if m:
                return int(m.group(1)) == want, int(m.group(1))
            self.read(0.1)
        return False, None

    def wait_exit(self, timeout=3.0):
        """Wait for the player to exit, reading its output meanwhile (a tty drains before a process can exit)."""
        end = time.time() + timeout
        while time.time() < end:
            if self.proc.poll() is not None:
                return True
            self.read(0.1)
        return False

    def close(self):
        try:
            self.proc.kill()
        except OSError:
            pass
        self.proc.wait()
        os.close(self.master)
        os.close(self.slave)


def start(**kw):
    s = Session(**kw)
    check(s.read(until=rb"keys ready"), "the story starts")
    return s


def expect_key(s, data, want, what):
    ok, got = s.key(data, want)
    check(ok, f"{what}: wanted key {want}, got {got}")


def test_keys():
    s = start()
    expect_key(s, b"\r", 13, "Return")
    expect_key(s, b"\x7f", 8, "DEL is Delete (8)")
    expect_key(s, b"\x08", 8, "Ctrl-H is Delete (8)")
    expect_key(s, b"a", 97, "a letter")
    expect_key(s, b"\x1b[A", 129, "cursor up")
    expect_key(s, b"\x1b[B", 130, "cursor down")
    expect_key(s, b"\x1b[D", 131, "cursor left")
    expect_key(s, b"\x1b[C", 132, "cursor right")
    expect_key(s, b"\x1bOA", 129, "cursor up (application mode)")
    s.close()


def test_escape():
    s = start()
    # a lone Escape completes without waiting for a following key
    expect_key(s, b"\x1b", 27, "lone Escape")
    expect_key(s, b"x", 120, "the key after a lone Escape")
    # Escape and another key arriving together are two keys; neither is lost
    mark = len(s.out)
    s.send(b"\x1bz")
    s.read(1.0, until=rb"key=122")
    codes = [int(m) for m in re.findall(rb"key=(\d+)", s.out[mark:])]
    check(codes == [27, 122], f"Escape followed at once by z gives 27 then 122, got {codes}")
    # sequences the story has no key for (Page Up, Delete key, an unknown CSI) are dropped whole
    mark = len(s.out)
    s.send(b"\x1b[5~\x1b[3~\x1b[1;5Cq")
    s.read(1.0, until=rb"line\? ")
    codes = [int(m) for m in re.findall(rb"key=(\d+)", s.out[mark:])]
    check(codes == [113], f"unsupported escape sequences give no key, got {codes}")
    s.close()


def test_line():
    s = start()
    s.send(b"q")
    check(s.read(until=rb"line\? "), "the story asks for a line")
    s.send(b"ab\x7fc\r")                                     # a, b, Delete, c, Return
    check(s.read(until=rb"line=\[ac\]"), "typing with Delete edits the line")
    s.close()

    s = start()
    s.send(b"q")
    s.read(until=rb"line\? ")
    s.send(b"x" * 200)                                       # far more than fits on a row
    s.read(1.0)
    check(b"\x07" in s.out, "a full line rings the bell")
    s.send(b"\r")
    s.read(until=rb"line=\[[\r\nx]+\]")
    flat = s.out.replace(b"\r", b"").replace(b"\n", b"")      # the echo wraps at the screen's edge
    m = re.search(rb"line=\[(x+)\]", flat)
    # the row has 80 columns and the prompt "line? " takes 6
    check(m is not None and len(m.group(1)) == 74, f"the line stops at the edge of the screen, got {len(m.group(1)) if m else None} characters")
    s.close()


def test_incomplete_sequence():
    s = start()
    # ESC [ 1 and then nothing: the player must not wait for the rest, and must not eat the next key
    mark = len(s.out)
    s.send(b"\x1b[1")
    time.sleep(0.3)
    s.send(b"q")
    s.read(1.5, until=rb"line\? ")
    codes = [int(m) for m in re.findall(rb"key=(\d+)", s.out[mark:])]
    check(codes == [113], f"a truncated escape sequence does not swallow the next key, got {codes}")
    s.close()


def modes_restored(s, what):
    after = termios.tcgetattr(s.master)
    check(after[3] == s.before[3], f"{what}: the terminal modes are back as they were")
    check(b"\x1b[?7h" in s.out[s.mark:], f"{what}: auto-wrap switched on again")


def test_fatal_signals_restore_terminal():
    for sig, name in ((signal.SIGABRT, "abort"), (signal.SIGQUIT, "Ctrl-backslash"), (signal.SIGHUP, "hangup")):
        s = start()
        s.mark = len(s.out)
        os.kill(s.proc.pid, sig)
        if not s.wait_exit():
            check(False, f"{name} ends the player")
            s.close()
            continue
        s.read(0.2)
        if sig != signal.SIGHUP:            # after a hangup there may be no terminal left to ask
            modes_restored(s, name)
        s.close()


def test_stop_and_continue():
    # Ctrl-Z, then `fg`: the handler gives the terminal back before the stop and takes it again
    # afterwards, so keys still arrive one at a time. (Whether the kernel really stops this
    # orphaned process group varies; the SIGCONT makes the test the same either way.)
    s = start()
    s.mark = len(s.out)
    os.kill(s.proc.pid, signal.SIGTSTP)
    s.read(0.4)
    check(b"\x1b[?7h" in s.out[s.mark:], "Ctrl-Z gives the shell a sane terminal")
    os.kill(s.proc.pid, signal.SIGCONT)                 # `fg` (a no-op if the stop was discarded)
    s.read(0.4)
    during = termios.tcgetattr(s.master)
    check(not (during[3] & termios.ICANON) and not (during[3] & termios.ECHO), "raw mode is back after continuing")
    expect_key(s, b"x", 120, "a key right after Ctrl-Z")
    os.kill(s.proc.pid, signal.SIGTSTP)                 # and again
    s.read(0.4)
    os.kill(s.proc.pid, signal.SIGCONT)
    s.read(0.4)
    expect_key(s, b"y", 121, "a key after a second Ctrl-Z")
    s.close()


def test_save_over_64k():
    work = tempfile.mkdtemp()
    story = os.path.join(work, "v4huge.z4")          # the save file is written next to the story
    shutil.copy(os.path.join(HERE, "data", "v4huge.z4"), story)
    s = Session(story=story, cwd=work)
    check(s.read(until=rb"v4huge ready"), "the 65 KB story starts")
    s.send(b"i")
    check(s.read(until=rb"counter=1"), "counter 1")
    s.send(b"s")
    check(s.read(until=rb"save result 1"), "a save larger than 64 KB is written")
    saves = [f for f in os.listdir(work) if f.endswith(".sav")]
    size = os.path.getsize(os.path.join(work, saves[0])) if saves else 0
    check(size > 65536, f"the save file is over 64 KB ({size} bytes)")
    s.send(b"i")
    check(s.read(until=rb"counter=2"), "counter 2")
    s.send(b"r")
    check(s.read(until=rb"save result 2"), "the save is restored (the game resumes after its save, result 2)")
    s.send(b"p")
    s.out = s.out[max(s.out.rfind(b"save result 2"), 0):]
    check(s.read(until=rb"counter=1"), "with the counter it had at the save")
    s.close()
    shutil.rmtree(work, ignore_errors=True)


def test_ctrl_c_restores_terminal():
    s = start()
    during = termios.tcgetattr(s.master)
    check(not (during[3] & termios.ICANON) and not (during[3] & termios.ECHO), "the player puts the terminal in raw mode")
    s.mark = len(s.out)
    s.send(b"\x03")
    if not s.wait_exit():
        check(False, "Ctrl-C ends the player")
        s.close()
        return
    s.read(0.3)
    after = termios.tcgetattr(s.master)
    check(after[3] & termios.ICANON and after[3] & termios.ECHO, "Ctrl-C restores line mode and echo")
    check(after[0] == s.before[0] and after[3] == s.before[3], "Ctrl-C leaves the terminal modes as they were")
    check(b"\x1b[?7h" in s.out[s.mark:] and b"\x1b[r" in s.out[s.mark:], "Ctrl-C restores auto-wrap and the scroll region")
    s.close()


def test_normal_exit_restores_terminal():
    s = start()
    s.send(b"q")
    s.read(until=rb"line\? ")
    s.send(b"hi\r")
    s.read(until=rb"line=\[hi\]")
    if not s.wait_exit():
        check(False, "the story ends and the player exits")
        s.close()
        return
    check(s.proc.returncode == 0, "exit status 0")
    after = termios.tcgetattr(s.master)
    check(after[3] == s.before[3], "the terminal modes are restored at exit")
    s.close()


if __name__ == "__main__":
    if not os.path.exists(BIN):
        print("no V4 console at", BIN, "(build with: make BUILD_DIR=build_v4 EXTRA_CFLAGS=-DZVIBE_ENABLE_V4=1)")
        sys.exit(1)
    test_keys()
    test_escape()
    test_line()
    test_incomplete_sequence()
    test_fatal_signals_restore_terminal()
    test_stop_and_continue()
    test_save_over_64k()
    test_ctrl_c_restores_terminal()
    test_normal_exit_restores_terminal()
    print(f"{checks - failed}/{checks} checks passed")
    sys.exit(1 if failed else 0)
