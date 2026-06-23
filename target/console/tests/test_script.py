#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""
ZVibe Console Integration Test Runner

Usage:
  python test_script.py                   # restaurant: walkthrough + stress (both must pass)
  python test_script.py plunderedhearts   # download and test Plundered Hearts
  python test_script.py seastalker        # download and test Seastalker
"""

import sys
import os
import re
import fcntl
import subprocess
import select

# --- Paths ------------------------------------------------------------------

TESTS_DIR    = os.path.dirname(os.path.abspath(__file__))
CONSOLE_DIR  = os.path.dirname(TESTS_DIR)
ZVIBE        = os.path.join(CONSOLE_DIR, "build", "bin", "zvibe_console")
ZVIBE_MINIMAL = os.path.join(CONSOLE_DIR, "build", "bin", "zvibe_minimal")
GAMES_DIR    = os.path.join(os.path.dirname(os.path.dirname(CONSOLE_DIR)), "games")
CATALOG_DIR  = os.path.join(GAMES_DIR, "catalog")
SCRIPTS_DIR  = os.path.join(GAMES_DIR, "scripts")
RESTAURANT   = CATALOG_DIR

# --- Test definitions -------------------------------------------------------

RESTAURANT_TESTS = [
    {
        "name":    "restaurant — text regression",
        "game":    os.path.join(RESTAURANT, "restaurant.z3"),
        "script":  os.path.join(SCRIPTS_DIR, "restaurant-text-check.txt"),
        "seed":    1,
        "markers": [],
        "required_markers": [
            "you can also see a satchel (which is closed) here.",
            "you open the pocket, revealing a toothbrush.",
        ],
        "forbidden_markers": [],
    },
    {
        "name":    "restaurant — walkthrough",
        "game":    os.path.join(RESTAURANT, "restaurant.z3"),
        "script":  os.path.join(SCRIPTS_DIR, "restaurant-script.txt"),
        "seed":    None,
        "markers": ["your score is 42 of a possible 42", "*** you have won ***"],
        "required_markers": [],
        "forbidden_markers": [],
    },
    {
        "name":    "restaurant — stress test",
        "game":    os.path.join(RESTAURANT, "restaurant.z3"),
        "script":  os.path.join(SCRIPTS_DIR, "restaurant-stress-test.txt"),
        "seed":    1,
        "markers": [],
        "required_markers": [],
        "forbidden_markers": [
            "unsupported opcode",
            "stack underflow",
            "division by zero",
            "abbreviation recursion not allowed",
            "segmentation fault",
            "fatal",
            "error:",
        ],
    },
]

OPTIONAL_GAMES = {
    "plunderedhearts": {
        "file":    "plunderedhearts-r26-s870730.z3",
        "script":  "plunderedhearts-script.txt",
        "seed":    None,
        "markers": ["you have achieved a score of 25 out of 25", "happily ever after"],
    },
    "seastalker": {
        "file":    "seastalker-r16-s850603.z3",
        "script":  "seastalker-script.txt",
        "seed":    None,
        "markers": ["your score is 100 points out of 100", "rank of a famous adventurer"],
    },
}

# --- Helpers ----------------------------------------------------------------

def die(msg):
    print(f"ERROR: {msg}", file=sys.stderr)
    sys.exit(1)


def check_binary():
    if not os.path.isfile(ZVIBE):
        die(f"zvibe_console not found: {ZVIBE}\nBuild the project first.")



# --- Script parsing (capture/substitute directives) ----------------------

def parse_script(path):
    """Parse a test script, returning a list of steps.

    Plain command lines become  {"type": "cmd", "text": "<command>"}.
    @capture directives become  {"type": "capture", "name": "…", "pattern": "…"}.
    Comment lines (#) and blank lines are skipped.
    """
    steps = []
    with open(path) as f:
        for line in f:
            stripped = line.strip()
            if not stripped or stripped.startswith("#"):
                continue
            m = re.match(r"@capture\s+(\w+)\s+/(.+)/\s*$", stripped)
            if m:
                steps.append({"type": "capture", "name": m.group(1), "pattern": m.group(2)})
            else:
                steps.append({"type": "cmd", "text": stripped})
    return steps


def script_has_captures(path):
    with open(path) as f:
        return any(line.strip().startswith("@capture") for line in f)


def read_until_prompt(proc, timeout_s=30):
    """Read stdout until a '>' prompt or EOF/timeout. Returns accumulated text."""
    buf = bytearray()
    deadline = __import__("time").time() + timeout_s
    while True:
        remaining = deadline - __import__("time").time()
        if remaining <= 0:
            break
        ready, _, _ = select.select([proc.stdout], [], [], min(remaining, 0.5))
        if ready:
            try:
                chunk = proc.stdout.read(4096)
            except BlockingIOError:
                continue
            if not chunk:
                break
            buf += chunk
            if b"\n>" in buf:
                return buf.decode("utf-8", errors="replace")
    return buf.decode("utf-8", errors="replace")


def run_test_interactive(name, game, script, seed, markers, required_markers, forbidden_markers):
    """Run a test interactively, handling @capture directives."""
    print(f"\n{'─'*60}")
    print(f"  {name}")
    print(f"{'─'*60}")

    for path, label in [(game, "game"), (script, "script")]:
        if not os.path.isfile(path):
            print(f"  FAIL  {label} not found: {path}")
            return False

    steps = parse_script(script)
    captures = {}

    cmd = [ZVIBE_MINIMAL, game]

    import time as _time
    try:
        proc = subprocess.Popen(
            cmd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except OSError as e:
        print(f"  FAIL  could not start zvibe: {e}")
        return False

    flags = fcntl.fcntl(proc.stdout, fcntl.F_GETFL)
    fcntl.fcntl(proc.stdout, fcntl.F_SETFL, flags | os.O_NONBLOCK)

    full_output = ""
    last_response = ""

    try:
        intro = read_until_prompt(proc, timeout_s=10)
        full_output += intro

        for step in steps:
            if step["type"] == "capture":
                m = re.search(step["pattern"], last_response)
                if m:
                    captures[step["name"]] = m.group(1)
                continue

            command = step["text"]
            for var, val in captures.items():
                command = command.replace("{" + var + "}", val)

            proc.stdin.write((command + "\n").encode())
            proc.stdin.flush()

            if command.lower() == "quit":
                _time.sleep(0.5)
                break

            last_response = read_until_prompt(proc, timeout_s=10)
            full_output += last_response

            if last_response and "\n>" not in last_response:
                proc.stdin.write(b"\n")
                proc.stdin.flush()
                more = read_until_prompt(proc, timeout_s=3)
                last_response += more
                full_output += more

        try:
            proc.stdin.close()
        except OSError:
            pass
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
    except BrokenPipeError:
        pass

    lower = full_output.lower()

    for marker in forbidden_markers:
        if marker.lower() in lower:
            print(f"  FAIL  forbidden marker found: {marker}")
            return False

    for marker in required_markers:
        if marker.lower() not in lower:
            print(f"  FAIL  required marker not found: {marker}")
            return False

    if required_markers:
        print(f"  PASS  matched {len(required_markers)} required text markers")
        return True

    for marker in markers:
        if marker.lower() in lower:
            for line in full_output.splitlines():
                if marker.lower() in line.lower():
                    print(f"  PASS  {line.strip()}")
                    break
            return True

    if not markers:
        print("  PASS  script completed without interpreter errors")
        return True

    print("  FAIL  completion marker not found in output")
    for line in full_output.strip().splitlines()[-8:]:
        if line.strip():
            print(f"        {line}")
    return False


def run_test(name, game, script, seed, markers, required_markers, forbidden_markers):
    print(f"\n{'─'*60}")
    print(f"  {name}")
    print(f"{'─'*60}")

    for path, label in [(game, "game"), (script, "script")]:
        if not os.path.isfile(path):
            print(f"  FAIL  {label} not found: {path}")
            return False

    cmd = [ZVIBE]
    if seed is not None:
        cmd += ["-r", str(seed)]
    cmd += ["-s", script, game]

    try:
        result = subprocess.run(
            cmd,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=120,
        )
    except subprocess.TimeoutExpired:
        print("  FAIL  timed out after 120s")
        return False

    text = result.stdout.decode("utf-8", errors="replace")
    lower = text.lower()

    if result.returncode != 0:
        print(f"  FAIL  process exited with code {result.returncode}")
        for line in text.strip().splitlines()[-8:]:
            if line.strip():
                print(f"        {line}")
        return False

    for marker in forbidden_markers:
        if marker.lower() in lower:
            print(f"  FAIL  forbidden marker found: {marker}")
            for line in text.strip().splitlines()[-8:]:
                if line.strip():
                    print(f"        {line}")
            return False

    for marker in required_markers:
        if marker.lower() not in lower:
            print(f"  FAIL  required marker not found: {marker}")
            for line in text.strip().splitlines()[-8:]:
                if line.strip():
                    print(f"        {line}")
            return False

    if required_markers:
        print(f"  PASS  matched {len(required_markers)} required text markers")
        return True

    if not markers:
        print("  PASS  script completed without interpreter errors")
        return True

    for marker in markers:
        if marker.lower() in lower:
            for line in text.splitlines():
                if marker.lower() in line.lower():
                    print(f"  PASS  {line.strip()}")
                    break
            return True

    print("  FAIL  completion marker not found in output")
    for line in text.strip().splitlines()[-8:]:
        if line.strip():
            print(f"        {line}")
    return False

# --- Main -------------------------------------------------------------------

def main():
    check_binary()

    if len(sys.argv) == 1:
        passed = 0
        for t in RESTAURANT_TESTS:
            runner = run_test_interactive if script_has_captures(t["script"]) else run_test
            if runner(
                t["name"],
                t["game"],
                t["script"],
                t["seed"],
                t["markers"],
                t["required_markers"],
                t["forbidden_markers"],
            ):
                passed += 1
        total = len(RESTAURANT_TESTS)
        print(f"\n{'═'*60}")
        if passed == total:
            print(f"  ALL TESTS PASSED  ({passed}/{total})")
            sys.exit(0)
        else:
            print(f"  {total - passed} FAILED  ({passed}/{total} passed)")
            sys.exit(1)

    game_name = sys.argv[1].lower()
    if game_name not in OPTIONAL_GAMES:
        die(f"Unknown game '{game_name}'.\nAvailable: {', '.join(OPTIONAL_GAMES)}")

    cfg = OPTIONAL_GAMES[game_name]
    game_file   = os.path.join(CATALOG_DIR, cfg["file"])
    script_file = os.path.join(SCRIPTS_DIR, cfg["script"])

    if not os.path.isfile(game_file):
        die(f"Game file not found: {game_file}\n"
            f"Download it first: cd games && ./get_games.py browse")

    ok = run_test(game_name, game_file, script_file, cfg["seed"], cfg["markers"], [], [])
    print(f"\n{'═'*60}")
    print(f"  {'PASSED' if ok else 'FAILED'}")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
