#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""The POSIX server's game list: many long names must not overflow its buffer, and names with
quotes and backslashes must come through as valid JSON.

Usage: test_server_games.py <server binary>   (built with -fsanitize=address to see an overflow)
"""
import base64, json, os, socket, struct, subprocess, sys, tempfile, time

server = sys.argv[1]
here = os.path.dirname(os.path.abspath(__file__))
www = os.path.join(here, "..", "platform", "posix", "www", "zvibe.html")
cart = os.path.join(here, "..", "cartographer.js")
zterm = os.path.join(here, "..", "zterm.js")
checks = failed = 0


def check(cond, what):
    global checks, failed
    checks += 1
    if not cond:
        failed += 1
        print("FAIL:", what)


def first_frame(port):
    s = socket.create_connection(("127.0.0.1", port), timeout=5)
    s.sendall(b"GET /zvibe HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
              b"Sec-WebSocket-Key: " + base64.b64encode(os.urandom(16)) + b"\r\nSec-WebSocket-Version: 13\r\n\r\n")
    buf = b""
    while b"\r\n\r\n" not in buf:
        buf += s.recv(4096)
    buf = buf.split(b"\r\n\r\n", 1)[1]

    def rd(n):
        nonlocal buf
        while len(buf) < n:
            buf += s.recv(65536)
        out, buf = buf[:n], buf[n:]
        return out

    h = rd(2)
    n = h[1] & 127
    if n == 126:
        n = struct.unpack(">H", rd(2))[0]
    elif n == 127:
        n = struct.unpack(">Q", rd(8))[0]
    data = rd(n)
    s.close()
    return data


def run(names, port):
    d = tempfile.mkdtemp()
    for n in names:
        open(os.path.join(d, n), "w").close()
    p = subprocess.Popen([server, "-p", str(port), "-w", www, "-c", cart, "-z", zterm, "-s", os.path.join(d, "x.sav"), d],
                         stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0"))
    time.sleep(1.0)
    try:
        frame = first_frame(port)
    except OSError as e:                  # the server died (an overflow without a sanitizer, say)
        frame = b""
        print("server did not answer:", e)
    finally:
        p.terminate()
        err = p.stderr.read().decode(errors="replace")
        p.wait()
        for n in os.listdir(d):
            os.unlink(os.path.join(d, n))
        os.rmdir(d)
    return frame, err


# 1. thirty 250-character names do not fit the frame: the list is cut, never overrun
long_names = [("x%02d" % i) + "a" * 240 + ".z3" for i in range(30)]
frame, err = run(long_names, 18101)
try:
    j = json.loads(frame)
    ok = j.get("type") == "games" and isinstance(j.get("list"), list)
except ValueError:
    ok, j = False, {"list": []}
check(ok, "a crowded directory still gives valid JSON")
check(0 < len(j["list"]) < 30, f"the list is cut to what fits ({len(j['list'])} of 30)")
check(len(frame) <= 4096, f"the frame stays inside the buffer ({len(frame)} bytes)")
check("AddressSanitizer" not in err and "runtime error" not in err, "no sanitizer report: " + err[:200])

# 2. awkward but legal names survive the trip exactly
names = ['say"hi.z3', "back\\slash.z3", "plain.z3", "tab\there.z3"]
frame, err = run(names, 18102)
try:
    got = json.loads(frame)["list"]
except ValueError:
    got = []
check(sorted(got) == sorted(names), f"names with quotes, backslashes and control characters round-trip, got {got}")

# 3. a name that is too long once escaped (a hundred tabs) is left out, never published cut short
names = ["tab" + "\t" * 100 + ".z3", "ok.z3"]
frame, err = run(names, 18103)
try:
    got = json.loads(frame)["list"]
except ValueError:
    got = []
check("ok.z3" in got, f"the other games are listed, got {got}")
check(all(n in names for n in got), f"no name is published that is not a real file name, got {[g[:12] for g in got]}")

print(f"{checks - failed}/{checks} checks passed")
sys.exit(1 if failed else 0)
