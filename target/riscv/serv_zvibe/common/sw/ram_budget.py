#!/usr/bin/env python3
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
"""
ram_budget.py - how much RAM a version 4 story needs on the RISC-V firmware.

The firmware keeps the story's dynamic memory in RAM (the story itself is read in
place from flash), plus two buffers for saves (see zvibe_riscv_v4.c): the core's
save data, as large as the dynamic memory, and the packed save that goes to a flash
slot. The Arty's SoC RAM is a bitstream parameter (RAM_KB, a power of two), so the
firmware and the bitstream must agree.

Usage:
  ram_budget.py dyn  STORY...            story buffer to build for (bytes, multiple of 256): ZVIBE_MEMORY_BUFFER_SIZE
  ram_budget.py kb   STORY...            SoC RAM to build for (KB, power of two)
  ram_budget.py show STORY...            the budget, line by line

All stories of one flash image share one firmware, so the largest counts.
"""
import struct
import sys

# Measured on the V3 Arty firmware (-O1): .data + .bss without the story buffer
# (interpreter state, flash and menu code, line buffers, the delta buffer). The V4 player
# adds the screen layer's state. A little slack is included; the linker checks the exact fit.
FIXED_BYTES   = 4 * 1024
SCREEN_BYTES  = 512
STACK_BYTES   = 4 * 1024       # the linker insists on 2 KB; leave room for the interpreter's recursion
PACKED_MAX    = 16 * 1024      # RV_V4_PACKED_MAX: this, or the story buffer if that is smaller
STACK_WORDS   = 512            # ZVIBE_STACK_WORDS (the core's save data carries the stack too)
CORE_RESERVE  = 1024           # the core refuses a story whose dynamic memory + 1 KB exceeds the buffer
                               # (Z_LEGACY_STACK_RESERVE_BYTES in src/core/zvibe_core.c)
RAM_STEPS_KB  = (32, 64, 128, 256)
RAM_LIMIT_KB  = 256            # the Arty has far more BRAM, but timing and use are untested above this


def dyn_size(path):
    with open(path, "rb") as f:
        head = f.read(64)
    if len(head) < 64:
        sys.exit(f"{path}: too short to be a story file")
    if head[0] != 4:
        sys.exit(f"{path}: version {head[0]}, not a version 4 story")
    return struct.unpack(">H", head[14:16])[0]


def round_up(n, to):
    return (n + to - 1) // to * to


def budget(paths):
    dyn = round_up(max(dyn_size(p) for p in paths) + CORE_RESERVE, 256)
    blob = dyn + 2 * STACK_WORDS + 64
    rows = [
        ("story buffer (dynamic memory + 1 KB)", dyn),
        ("save data buffer", blob),
        ("packed save buffer", min(PACKED_MAX, dyn)),
        ("interpreter, flash and menu code state", FIXED_BYTES),
        ("screen layer", SCREEN_BYTES),
        ("stack", STACK_BYTES),
    ]
    need = sum(v for _, v in rows)
    kb = next((k for k in RAM_STEPS_KB if k * 1024 >= need), None)
    return dyn, rows, need, kb


def main(argv):
    if len(argv) < 2 or argv[0] not in ("dyn", "kb", "show"):
        sys.exit(__doc__)
    dyn, rows, need, kb = budget(argv[1:])
    if kb is None:
        sys.exit(f"needs {need} bytes of RAM: more than the {RAM_LIMIT_KB} KB this build supports")
    if argv[0] == "dyn":
        print(dyn)
    elif argv[0] == "kb":
        print(kb)
    else:
        for name, v in rows:
            print(f"  {name:42s} {v:7d}")
        print(f"  {'total':42s} {need:7d}  ->  SoC RAM {kb} KB")


if __name__ == "__main__":
    main(sys.argv[1:])
