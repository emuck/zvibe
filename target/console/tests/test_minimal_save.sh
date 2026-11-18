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

# run a script of commands; sets $out (output) and $rc (exit status of the player)
run() { out="$(printf "$1" | "$MINIMAL" "$2" 2>&1)"; rc=$?; }
# the text after the first occurrence of a marker (so "the game goes on" is judged on what comes after the refusal)
after() { local rest="${1#*"$2"}"; [ "$rest" != "$1" ] && printf '%s' "$rest"; }

# no save file: refused, and the game goes on (look still answers AFTER the refusal; the player exits normally)
rm -f "$TMP/restaurant.sav"
run 'restore\nlook\nquit\n' "$TMP/restaurant.z3"
check "restore without a file says so" "$(has 'No save file found' "$out")"
check "and the game goes on (the room is described after it)" "$(has 'On the Ramp' "$(after "$out" 'No save file found')")"
check "and the player exits normally" "$rc"

# a damaged file: refused, the game goes on
printf 'this is not a save' > "$TMP/restaurant.sav"
run 'restore\nlook\nquit\n' "$TMP/restaurant.z3"
check "a damaged save is refused with a message" "$(has 'Error: That save does not fit this game' "$out")"
check "and the game goes on" "$(has 'On the Ramp' "$(after "$out" 'does not fit this game')")"
check "and the player exits normally" "$rc"

# a huge file is refused before it is read
dd if=/dev/zero of="$TMP/restaurant.sav" bs=1048576 count=3 2>/dev/null
run 'restore\nlook\nquit\n' "$TMP/restaurant.z3"
check "a 3 MB save file is refused" "$(has 'Error: Invalid save file size' "$out")"
check "and the game goes on" "$(has 'On the Ramp' "$(after "$out" 'Invalid save file size')")"

# a path as long as the buffer, ending in .z3, does not overflow the file name (nothing to load; the one-byte overflow only
# shows in a sanitizer build: make BUILD_DIR=build_asan EXTRA_CFLAGS='-fsanitize=address -g' and ZVIBE_BUILD=build_asan)
long="$(python3 -c "import os; n=int(os.popen('getconf PATH_MAX /').read() or 4096); print('/tmp/' + 'a' * (n - 1 - 5 - 3) + '.z3')")"
"$MINIMAL" "$long" >/dev/null 2>&1; r=$?
check "a story path of exactly PATH_MAX-1 characters ending in .z3 does not crash" "$([ "$r" -lt 128 ]; echo $?)"

# an unwritable place: the save fails with a message, the game goes on
mkdir "$TMP/ro" && cp "$STORY" "$TMP/ro/restaurant.z3" && chmod 555 "$TMP/ro"
if [ -w "$TMP/ro" ]; then
    echo "SKIP  unwritable directory (running as a user who can write anywhere)"
else
    run 'save\nlook\nquit\n' "$TMP/ro/restaurant.z3"
    check "saving where it cannot says so" "$(has 'Cannot create save file' "$out")"
    check "and the game goes on" "$(has 'On the Ramp' "$(after "$out" 'Cannot create save file')")"
    check "no temporary file is left behind" "$(ls "$TMP/ro" | grep -q tmp; echo $((1 - $?)))"
fi
chmod 755 "$TMP/ro"

echo "$pass passed, $fail failed"
[ "$fail" = 0 ]
