#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""Show a Z-machine story's header and check it against each ZVibe target's budget.

Usage: scripts/zinfo.py story.z3 [story.z4 ...]

The budgets below come from docs/V4_MEMORY.md; update both together.
"""
import struct
import sys

# RAM a story needs besides its dynamic memory: interpreter state, including the
# Z-machine stack, which lives in zState and not in the shared buffer
# (see docs/V4_MEMORY.md). V3 is measured (RISC-V non-buffer .bss); V4 adds a
# doubled stack (+1 KB, recommended) and ~250 B of screen/operand state.
STATE_BYTES = {3: 3716, 4: 3716 + 1024 + 250}
STACK_WORDS = {3: 512, 4: 1024}

# name: (total RAM budget for dynamic + state, max story bytes, max full-save bytes)
# None = limited only by build configuration.
TARGETS = {
    "console":    (None, None, None),
    "same51":     (None, 786432, 16384),        # single-game firmware saves the full image in a 16 KB buffer
    "arty_s7_50": (28144, 8 * 1024 * 1024, None),   # 32,256 B RAM - 16 B .data - 4 KB C stack reserve (assumed)
    "max10":      (28144, 140 * 1024, None),    # same RAM; 172 KB UFM minus firmware and ~4 save slots
}


def header(path):
    d = open(path, "rb").read()
    w = lambda o: struct.unpack(">H", d[o:o + 2])[0]
    ver = d[0]
    scale = {1: 2, 2: 2, 3: 2, 4: 4, 5: 4}.get(ver, 8)
    return dict(
        version=ver, release=w(2), serial=d[0x12:0x18].decode("latin1"), size=len(d),
        high=w(4), pc=w(6), dict=w(8), objtab=w(0x0A), globals=w(0x0C),
        dynamic=w(0x0E), abbr=w(0x18), declared=w(0x1A) * scale,
    )


def main(paths):
    for p in paths:
        h = header(p)
        v = h["version"]
        state = STATE_BYTES.get(v, STATE_BYTES[4])
        ram = h["dynamic"] + state
        full_save = h["dynamic"] + 2 * STACK_WORDS.get(v, STACK_WORDS[4]) + 64
        print(f"{p}\n  V{v} release {h['release']} serial {h['serial']}  file {h['size']:,} B  "
              f"dynamic {h['dynamic']:,} B  high mem at {h['high']:,}")
        print(f"  RAM: dynamic {h['dynamic']:,} + state {state:,} = {ram:,} B")
        for name, (budget, flash, save) in TARGETS.items():
            issues = []
            if budget is not None and ram > budget:
                issues.append(f"RAM {ram:,} > {budget:,}")
            if flash is not None and h["size"] > flash:
                issues.append(f"story {h['size']:,} B > {flash:,} B flash")
            if save is not None and full_save > save:
                issues.append(f"full save ~{full_save:,} B > {save:,} B (needs delta saves)")
            print(f"    {name:11s} {'ok' if not issues else 'NO: ' + '; '.join(issues)}")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    main(sys.argv[1:])
