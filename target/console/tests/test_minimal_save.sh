#!/bin/bash
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
# zvibe_minimal saves and restores: a save file beside the story (game.z3 -> game.sav, the console player's naming, so the two
# players read each other's saves), a missing or damaged file is refused and the game goes on.
#   ./test_minimal_save.sh        (builds first: make -C .. )
TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$TESTS_DIR/.." && pwd)"
MINIMAL="$ROOT/${ZVIBE_BUILD:-build}/bin/zvibe_minimal"
CONSOLE="$ROOT/${ZVIBE_BUILD:-build}/bin/zvibe_console"
STORY="$TESTS_DIR/../../../games/catalog/restaurant.z3"
[ -x "$MINIMAL" ] || { echo "zvibe_minimal not found: build first"; exit 1; }
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
cp "$STORY" "$TMP/restaurant.z3"
pass=0; fail=0
check() { if [ "$2" = 0 ]; then echo "PASS  $1"; pass=$((pass+1)); else echo "FAIL  $1"; fail=$((fail+1)); fi; }
has() { grep -q -- "$1" <<<"$2"; echo $?; }

# save on the Ramp, walk down (the place changes), restore: back on the Ramp
out="$(printf 'save\ndown\nlook\nrestore\nlook\nquit\n' | "$MINIMAL" "$TMP/restaurant.z3" 2>&1)"
check "save is acknowledged" "$(has '\[Game saved to' "$out")"
check "the save file is restaurant.sav beside the story" "$([ -s "$TMP/restaurant.sav" ]; echo $?)"
before="${out%%\[Game restored*}"; after="${out#*\[Game restored}"
check "the place changed after the save (Blighted Ground)" "$(has 'Blighted Ground' "$before")"
check "restore is acknowledged" "$(has '\[Game restored from' "$out")"
check "after the restore the game is back on the Ramp" "$(has 'On the Ramp' "$after")"
check "and not on Blighted Ground" "$(grep -q 'Blighted Ground' <<<"$after"; echo $((1 - $?)))"

# a new run restores the file the first one wrote (the save outlives the process)
out="$(printf 'restore\nlook\nquit\n' | "$MINIMAL" "$TMP/restaurant.z3" 2>&1)"
check "a later run restores it" "$(has '\[Game restored from' "$out")"

# the console player reads the minimal player's save
if [ -x "$CONSOLE" ]; then
    out="$(printf 'restore\nlook\nquit\n' | "$CONSOLE" --script /dev/stdin "$TMP/restaurant.z3" 2>&1)"
    check "the console player restores the minimal player's save" "$(has 'Game restored from' "$out")"
fi

# no save file: refused, the game goes on
rm -f "$TMP/restaurant.sav"
out="$(printf 'restore\nlook\nquit\n' | "$MINIMAL" "$TMP/restaurant.z3" 2>&1)"
check "restore without a file says so" "$(has 'No save file found' "$out")"
check "and the game goes on" "$(has 'On the Ramp' "$out")"

# a damaged file: refused, the game goes on
printf 'this is not a save' > "$TMP/restaurant.sav"
out="$(printf 'restore\nlook\nquit\n' | "$MINIMAL" "$TMP/restaurant.z3" 2>&1)"
check "a damaged save is refused" "$(grep -q 'Game restored from' <<<"$out"; echo $((1 - $?)))"
check "and the game goes on" "$(has 'On the Ramp' "$out")"

# an unwritable place: the save fails with a message, the game goes on
mkdir "$TMP/ro" && cp "$STORY" "$TMP/ro/restaurant.z3" && chmod 555 "$TMP/ro"
if [ -w "$TMP/ro" ]; then
    echo "SKIP  unwritable directory (running as a user who can write anywhere)"
else
    out="$(printf 'save\nlook\nquit\n' | "$MINIMAL" "$TMP/ro/restaurant.z3" 2>&1)"
    check "saving where it cannot says so" "$(has 'Cannot create save file' "$out")"
    check "and the game goes on" "$(has 'On the Ramp' "$out")"
fi
chmod 755 "$TMP/ro"

echo "$pass passed, $fail failed"
[ "$fail" = 0 ]
