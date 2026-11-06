#!/usr/bin/env bash
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

have_cmd() {
    command -v "$1" >/dev/null 2>&1
}

have_xc32() {
    have_cmd xc32-gcc || [ -x /Applications/microchip/xc32/v5.00/bin/xc32-gcc ]
}

run_step() {
    local title="$1"
    shift
    echo
    echo "==> $title"
    "$@"
}

cd "$repo_root"

run_step "Host validation" make validate
run_step "Restaurant integration" python3 target/console/tests/test_script.py

# Version 4: the same checks on a build with V4 compiled in (V3 stories must behave
# identically), the Czech V4 conformance gate, the V4 terminal player on a pty,
# and the web terminal's session and screen tests.
run_step "Host validation (V4 enabled)" make validate-v4
run_step "Webterm tests" make -C target/webterm/tests clean
run_step "Webterm tests" make -C target/webterm/tests test

# The RISC-V firmware's V4 player on the host: the real interpreter, screen layer and flash save
# layer against a mock flash and a VT100 emulator (needs only the host C compiler).
run_step "RISC-V V4 player (host)" make -C target/riscv/serv_zvibe/common/sw/tests clean
run_step "RISC-V V4 player (host)" make -C target/riscv/serv_zvibe/common/sw/tests test

if have_cmd riscv64-unknown-elf-gcc; then
    run_step "RISC-V build" make -C target/riscv/serv_zvibe/common/sw clean
    run_step "RISC-V build" make -C target/riscv/serv_zvibe/common/sw zvibe_riscv_multi.elf
    run_step "Arty V3 build" make -C target/riscv/serv_zvibe/boards/arty_s7_50/sw clean
    run_step "Arty V3 build" make -C target/riscv/serv_zvibe/boards/arty_s7_50/sw firmware
    run_step "Arty V4 build" make -C target/riscv/serv_zvibe/boards/arty_s7_50/sw clean
    run_step "Arty V4 build" make -C target/riscv/serv_zvibe/boards/arty_s7_50/sw firmware V4=1 GAME=czech
    run_step "Arty V4 build" make -C target/riscv/serv_zvibe/boards/arty_s7_50/sw clean
else
    echo
    echo "==> Skipping RISC-V build (riscv64-unknown-elf-gcc not found)"
fi

if have_xc32; then
    run_step "SAME51 build" make -C target/same51 clean
    run_step "SAME51 build" make -C target/same51 STORY_FILE=../../games/catalog/czech.z3
    # V4 firmware (czech.z4); the generated story_data.c is removed so it cannot leak into a V3 build
    run_step "SAME51 V4 build" make -C target/same51 clean
    run_step "SAME51 V4 build" make -C target/same51 v4
    rm -f target/same51/src/story_data.c
else
    echo
    echo "==> Skipping SAME51 build (xc32-gcc not found)"
fi

if have_cmd x86_64-w64-mingw32-gcc; then
    run_step "Windows build" make -C target/windows clean
    run_step "Windows build" make -C target/windows all
    # V4 compiled in: the terminal player is POSIX-only, so Windows must fall back to the plain path
    run_step "Windows V4 build" make -C target/windows clean
    run_step "Windows V4 build" make -C target/windows all \
        WIN_CFLAGS="-Wall -Wextra -O2 -I../../include -D_WIN32 -DZVIBE_ENABLE_V4=1 -DZVIBE_MEMORY_BUFFER_SIZE=73728"
else
    echo
    echo "==> Skipping Windows build (x86_64-w64-mingw32-gcc not found)"
fi
