#!/bin/bash
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
#
# A version 4 story on the console's plain path (piped stdin or -s script): no
# status bar or screen clearing, and read_char takes keys from the script or stdin.
# Needs a console binary built with ZVIBE_ENABLE_V4=1 (ZVIBE_BUILD, default build_v4).

set -u
TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="$TESTS_DIR/../${ZVIBE_BUILD:-build_v4}/bin/zvibe_console"
STORY="$TESTS_DIR/data/v4save.z4"
fail=0

check() {  # name, output, expected text
    if grep -q -- "$3" <<<"$2"; then echo "  PASS $1"; else echo "  FAIL $1"; fail=1; fi
}

[ -x "$BIN" ] || { echo "no V4 console at $BIN (build with EXTRA_CFLAGS=-DZVIBE_ENABLE_V4=1)"; exit 1; }

out=$(printf 'iipq' | "$BIN" "$STORY" 2>&1)
check "piped keys" "$out" "counter=2"
check "piped quit" "$out" "bye"
if grep -q $'\033' <<<"$out"; then echo "  FAIL no terminal escapes"; fail=1; else echo "  PASS no terminal escapes"; fi

printf 'ip\n\nq\n' > "$TESTS_DIR/v4_plain_script.tmp"
out=$("$BIN" -s "$TESTS_DIR/v4_plain_script.tmp" "$STORY" 2>&1)
rm -f "$TESTS_DIR/v4_plain_script.tmp"
check "script line is a run of keys" "$out" "counter=1"
check "empty script line is Return" "$out" "key? $"
check "script reaches quit" "$out" "bye"


# Return is ZSCII 13 (not 10): v4keys.z4 prints the code of every key it reads.
KEYS="$TESTS_DIR/data/v4keys.z4"
out=$(printf '\nqhello\n' | "$BIN" "$KEYS" 2>&1)
check "piped Return is key 13" "$out" "key=13"
check "piped q is key 113" "$out" "key=113"
check "the line after the keys is read whole" "$out" "line=\[hello\]"
printf '\nq\nhi there\n' > "$TESTS_DIR/v4_plain_script.tmp"
out=$("$BIN" -s "$TESTS_DIR/v4_plain_script.tmp" "$KEYS" 2>&1)
rm -f "$TESTS_DIR/v4_plain_script.tmp"
check "an empty script line is key 13" "$out" "key=13"
check "a script line of keys is read key by key" "$out" "key=113"
check "a later script line is the typed line" "$out" "line=\[hi there\]"
exit $fail
