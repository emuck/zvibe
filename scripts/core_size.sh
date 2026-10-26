#!/usr/bin/env bash
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
#
# Per-file and total code/RAM size of the interpreter core for the embedded
# targets, using the same flags as their firmware builds. Used to prove that a
# change costs nothing (or saves bytes) in V3-only builds: run it before and
# after and diff the output.
#
#   scripts/core_size.sh                  # print sizes
#   scripts/core_size.sh > before.txt     # save a baseline, then later:
#   scripts/core_size.sh | diff before.txt -
#
# Extra defines can be passed through EXTRA_CFLAGS, e.g.
#   EXTRA_CFLAGS=-DZVIBE_ENABLE_V4=1 scripts/core_size.sh
#
# Targets whose toolchain is missing are skipped with a note.

set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
EXTRA_CFLAGS="${EXTRA_CFLAGS:-}"
SOURCES=(src/core/zvibe_core.c src/core/zvibe_memory.c src/core/zvibe_object.c
         src/core/zvibe_ops_base.c src/core/zvibe_ops_math.c src/core/zvibe_ops_mem.c
         src/core/zvibe_ops_text.c src/api/zvibe_api.c src/api/zvibe_api_file.c)
# V4-only sources, picked up automatically when they exist
for f in src/core/zvibe_ops_v4.c; do [ -f "$ROOT/$f" ] && SOURCES+=("$f"); done

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

report() { # name, size-tool, objdir
  local name="$1" sizetool="$2" dir="$3" t=0 d=0 b=0
  printf '%s\n' "== $name"
  printf '%-22s %8s %6s %8s\n' file text data bss
  for o in "$dir"/*.o; do
    read -r tx da bs _ < <("$sizetool" "$o" | awk 'NR==2 {print $1, $2, $3}')
    printf '%-22s %8d %6d %8d\n' "$(basename "$o" .o)" "$tx" "$da" "$bs"
    t=$((t + tx)); d=$((d + da)); b=$((b + bs))
  done
  printf '%-22s %8d %6d %8d\n\n' TOTAL "$t" "$d" "$b"
}

build() { # objdir, compiler-and-flags...
  local dir="$1"; shift
  mkdir -p "$dir"
  for s in "${SOURCES[@]}"; do
    # shellcheck disable=SC2086
    "$@" $EXTRA_CFLAGS -c "$ROOT/$s" -o "$dir/$(basename "${s%.c}").o" || return 1
  done
}

# --- SAM E51 (Cortex-M4, XC32), flags from target/same51/Makefile ---
XC32_BASE=/Applications/microchip/xc32
[ -d "$XC32_BASE" ] || XC32_BASE=/opt/microchip/xc32
XC32_VER="$(ls -d "$XC32_BASE"/v* 2>/dev/null | sort -V | tail -1)"
DFP="$(ls -d "$HOME"/.mchp_packs/Microchip/SAME51_DFP/* 2>/dev/null | sort -V | tail -1)"
if [ -n "$XC32_VER" ] && [ -x "$XC32_VER/bin/xc32-gcc" ]; then
  DFPFLAG=()
  [ -n "$DFP" ] && DFPFLAG=("-mdfp=$DFP")
  if build "$WORK/same51" "$XC32_VER/bin/xc32-gcc" -x c -mprocessor=ATSAME51J20A "${DFPFLAG[@]}" \
       -ffunction-sections -fdata-sections -O2 -fno-common -Wall \
       -I"$ROOT/include" -I"$ROOT/src" \
       -DZVIBE_FREESTANDING=1 -DZVIBE_ENABLE_FILE_IO=0 -DZVIBE_ENABLE_STATUS_LINE=0 \
       -DZVIBE_ENABLE_SAVE_RESTORE=1; then
    report "SAME51 (xc32 -O2)" "$XC32_VER/bin/xc32-size" "$WORK/same51"
  else
    echo "== SAME51: BUILD FAILED"; exit 1
  fi
else
  echo "== SAME51: skipped (XC32 not found)"
fi

# --- RISC-V (SERV RV32I), flags from target/riscv/serv_zvibe/common/sw/Makefile ---
riscv_works() {
  command -v riscv64-unknown-elf-gcc >/dev/null 2>&1 &&
    echo 'int x;' | riscv64-unknown-elf-gcc -march=rv32i -mabi=ilp32 -x c -c - -o /dev/null 2>/dev/null
}
if riscv_works; then
  if build "$WORK/riscv" riscv64-unknown-elf-gcc -march=rv32i -mabi=ilp32 -O1 \
       -Wall -Wextra -Wno-unused-parameter -I"$ROOT/include" \
       -DZVIBE_MINIMAL_FEATURES -DZVIBE_ENABLE_FILE_IO=0 -DZVIBE_ENABLE_STATUS_LINE=0 \
       -DZVIBE_ENABLE_SAVE_RESTORE=1; then
    report "RISC-V (rv32i -O1)" riscv64-unknown-elf-size "$WORK/riscv"
  else
    echo "== RISC-V: BUILD FAILED"; exit 1
  fi
else
  echo "== RISC-V: skipped (riscv64-unknown-elf-gcc missing or not working; on macOS a missing"
  echo "   libisl.23.dylib means: brew install isl)"
fi
