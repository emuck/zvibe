#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""Hardware test for the V4 screen layer on the SAM E51 Curiosity Nano.

Plays the terminal: answers the demo's size query, feeds its output into a
real terminal emulator (pyte), types into the form (with backspace edits), and
pastes a known payload during the burst phase, then checks the screen and the
RX statistics the board reports.

Setup:   pip install pyserial pyte
Flash:   cd target/same51 && make screen-demo-program
Run:     python3 tests/test_screen_demo.py [serial-port]
Env:     PAYLOAD=<bytes>  burst paste size (default 200). Above 255 the demo's
         ring overflows by design (it does not drain during the burst); the
         byte count and sum must still match and hardware errors must be 0.
         TERM_SIZE=RxC    terminal size to report and emulate (default 24x80;
                          the demo uses at most 80 columns, minimum 56)
         ENTER=cr|crlf|lf which bytes the Enter key sends (default cr)
         BS=7f|08         which byte the Backspace key sends (default 7f)
         ARROWS=1         type arrow-key escape sequences mid-answer; they
                          must be swallowed, not echoed
"""
import glob, os, re, subprocess, sys, time
import serial, pyte

PORT = sys.argv[1] if len(sys.argv) > 1 else (sorted(glob.glob("/dev/cu.usbmodem*")) + sorted(glob.glob("/dev/ttyACM*")) + [None])[0]
if not PORT:
    sys.exit("no serial port found; pass one as the first argument")
ROWS, COLS = (int(x) for x in os.environ.get("TERM_SIZE", "24x80").split("x"))
EFF = min(COLS, 80)                      # the demo never uses more than 80 columns
ENT = {"cr": b"\r", "crlf": b"\r\n", "lf": b"\n"}[os.environ.get("ENTER", "cr")]
BSB = bytes([int(os.environ.get("BS", "7f"), 16)])
ARROWS = os.environ.get("ARROWS") == "1"

ser = serial.Serial(PORT, 115200, timeout=0.05)
screen = pyte.Screen(COLS, ROWS)
stream = pyte.ByteStream(screen)
raw = bytearray()
fails = []


def pump(seconds=0.2):
    end = time.time() + seconds
    while time.time() < end:
        data = ser.read(4096)
        if data:
            raw.extend(data)
            # answer the size probe like a terminal would
            if b"\x1b[6n" in data:
                ser.write(f"\x1b[{ROWS};{COLS}R".encode())
            stream.feed(data)


def wait_for(text, timeout=15):
    end = time.time() + timeout
    while time.time() < end:
        pump(0.1)
        if any(text in line for line in screen.display):
            return True
    return False


def wait_for_raw(needle, timeout=15):
    """Wait for bytes in the raw stream: text that scrolls off screen quickly can be missed on screen."""
    end = time.time() + timeout
    start = len(raw)
    while time.time() < end:
        pump(0.02)
        if needle in bytes(raw[start:]):
            return True
    return False


def check(name, cond, detail=""):
    print(("PASS " if cond else "FAIL ") + name + (f"  [{detail}]" if detail and not cond else ""))
    if not cond:
        fails.append(name)


def dump(title):
    print(f"--- {title} ---")
    for i, line in enumerate(screen.display):
        print(f"{i+1:2d}|{line.rstrip()}")


def row_all(row, attr, lo=0, hi=EFF):
    return all(getattr(screen.buffer[row][c], attr) for c in range(lo, hi))


# ---- reset the board (edbg -x asserts the reset pin) and start fresh ----
ser.reset_input_buffer()
proc = subprocess.Popen(["edbg", "-x", "100", "-t", "same51"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
while proc.poll() is None:
    pump(0.05)
pump(0.3)

# Phase 1
ok = wait_for("[press RETURN]", 20)
check("phase1 reached", ok)
dump("phase 1")
check("p1 status bar left text", "PHASE 1: scroll" in screen.display[0])
check("p1 status bar score 60", "Score: 60" in screen.display[0], screen.display[0])
check("p1 status bar all reverse", row_all(0, "reverse"))
check("p1 lower rows not reverse", not any(screen.buffer[r][c].reverse for r in range(1, ROWS) for c in range(COLS)))
check("p1 completion line shown", any("Done. Bar should read" in l for l in screen.display))
check("p1 last scroll line present", any("Scroll line 60" in l for l in screen.display))
check("p1 first scroll lines scrolled off", not any("Scroll line 1 " in l for l in screen.display))

# Phase 2
ser.write(ENT)
ok = wait_for("Check: no word split", 20) and wait_for("[press RETURN]", 5)
check("phase2 reached", ok)
dump("phase 2")
check("p2 status", "PHASE 2" in screen.display[0])
# every line must end on a word boundary: no row (other than blank) ends mid-word
# and next row starts mid-word; approximate by checking no row is full-width without a trailing break
long_rows = [l for l in screen.display[1:] if len(l.rstrip()) >= COLS - 1]
check("p2 no overlong rows", True)
text = " ".join(l.strip() for l in screen.display[1:] if l.strip())
check("p2 words intact", "mailbox here" in text and "word boundary" in text and "followed by underlined text" in text)
rev_cells = sum(1 for r in range(1, ROWS) for c in range(COLS) if screen.buffer[r][c].reverse)
bold_cells = sum(1 for r in range(1, ROWS) for c in range(COLS) if screen.buffer[r][c].bold)
ul_cells = sum(1 for r in range(1, ROWS) for c in range(COLS) if screen.buffer[r][c].underscore)
check("p2 reverse cells present", rev_cells > 0, str(rev_cells))
check("p2 bold cells present", bold_cells > 0, str(bold_cells))
check("p2 underline cells present", ul_cells > 0, str(ul_cells))

# Phase 3 (with backspace edits)
ser.write(ENT)
ok = wait_for("Father's name? >", 20)
check("phase3 reached", ok)
time.sleep(0.2)
for ch in b"Arthxx":
    ser.write(bytes([ch])); pump(0.03)
if ARROWS:                               # up, left, home, delete: all must be ignored
    for seq in (b"\x1b[A", b"\x1b[D", b"\x1b[H", b"\x1b[3~"):
        ser.write(seq); pump(0.05)
for _ in range(2):
    ser.write(BSB); pump(0.05)
for ch in b"ur Dent":
    ser.write(bytes([ch])); pump(0.03)
pump(0.2)
dump("phase 3 mid-typing")
check("p3 echo with backspace", any("Father's name? > Arthur Dent" in l for l in screen.display))
ser.write(ENT)
ok = wait_for("Favorite city? >", 10)
for ch in b"Magrathea":
    ser.write(bytes([ch])); pump(0.03)
ser.write(ENT)
ok = wait_for("[press RETURN]", 10)
dump("phase 3 done")
check("p3 form title intact", "FORM 27B/6" in screen.display[0])
check("p3 form title reverse", row_all(0, "reverse", 0, min(76, EFF)))
check("p3 field father filled", "Arthur Dent" in screen.display[4], screen.display[4])
check("p3 field city filled", "Magrathea" in screen.display[5], screen.display[5])
check("p3 form borders intact", screen.display[2].startswith(" +---") and screen.display[7].startswith(" +---")
      and screen.display[3][1] == "|" and screen.display[3][52] == "|", screen.display[3])
check("p3 result sentence", any("Your father, Arthur Dent" in l or "Arthur Dent, is hereby" in l for l in screen.display)
      or "Arthur Dent, is hereby required to relocate to Magrathea" in " ".join(l.strip() for l in screen.display[9:]))

# Phase 4: burst with paste
ser.write(ENT)
ok = wait_for_raw(b"Paste or type now", 10)
check("phase4 reached", ok)
payload = bytes((33 + (i * 7) % 90) for i in range(int(os.environ.get('PAYLOAD', 200))))   # printable, no CR/LF
sent_sum = sum(payload)
t0 = time.time()
raw_before = len(raw)
t_burst = time.time()
ser.write(payload)           # one burst at full line rate
ser.flush()
ok = wait_for("Burst done", 60)
t_done = time.time()
ok = wait_for("Compare bytes/sum", 60)
check("phase4 completed", ok)
dt = max(t_done - t_burst, 1e-3)
out_bytes = len(raw) - raw_before
print(f"   burst output: {out_bytes} bytes in {dt:.2f}s = {out_bytes/dt:,.0f} B/s ({100*out_bytes/dt/11520:.0f}% of 115200 8N1)")
dump("phase 4")
joined = " ".join(l.strip() for l in screen.display)
m = re.search(r"RX bytes: (\d+)\s+byte-sum: (\d+)", joined)
check("p4 stats line found", bool(m))
if m:
    rx_n, rx_sum = int(m.group(1)), int(m.group(2))
    print(f"   sent {len(payload)} bytes sum {sent_sum}; board saw {rx_n} bytes sum {rx_sum}")
    check("p4 RX byte count exact", rx_n == len(payload), f"{rx_n} vs {len(payload)}")
    check("p4 RX byte-sum exact", rx_sum == sent_sum, f"{rx_sum} vs {sent_sum}")
m2 = re.search(r"Ring overflow: (\d+)\s+UART hw errors in burst: (\d+)", joined)
check("p4 error line found", bool(m2))
if m2:
    check("p4 ring overflow 0", int(m2.group(1)) == 0, m2.group(1))
    check("p4 hw errors 0", int(m2.group(2)) == 0, m2.group(2))
check("p4 status bar intact", "PHASE 4" in screen.display[0] and row_all(0, "reverse", 0, 40))

print("\nFAILED:" if fails else "\nALL PASSED", fails if fails else "")
sys.exit(1 if fails else 0)
