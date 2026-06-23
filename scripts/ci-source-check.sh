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

if have_cmd riscv64-unknown-elf-gcc; then
    run_step "RISC-V build" make -C target/riscv/serv_zvibe/common/sw clean
    run_step "RISC-V build" make -C target/riscv/serv_zvibe/common/sw zvibe_riscv_multi.elf
else
    echo
    echo "==> Skipping RISC-V build (riscv64-unknown-elf-gcc not found)"
fi

if have_xc32; then
    run_step "SAME51 build" make -C target/same51 clean
    run_step "SAME51 build" make -C target/same51 STORY_FILE=../../games/catalog/czech.z3
else
    echo
    echo "==> Skipping SAME51 build (xc32-gcc not found)"
fi
