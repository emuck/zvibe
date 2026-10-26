#!/bin/bash
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
#
# Czech Z-machine conformance suite, version 4 build (games/catalog/czech.z4).
#
# This is the gate for V4 support: zvibe must reach the same result as the
# reference interpreter (frotz), whose output for this story is
#     Performed 386 tests.
#     Passed: 367, Failed: 0, Print tests: 19
# Print tests are checked by eye in the Czech output and are not pass/fail, and
# Czech leaves save/restore/restart and much of the I/O untested, so a match is a
# regression check on opcode behavior, not a claim of full conformance.
# The counts are pinned on purpose: a change in the number of tests or in how
# many passed must be looked at, not absorbed.
#
# Needs a console binary built with ZVIBE_ENABLE_V4=1 (see `make validate-v4`).

set -u
TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONSOLE_DIR="$(cd "$TESTS_DIR/.." && pwd)"
BIN="$CONSOLE_DIR/${ZVIBE_BUILD:-build_v4}/bin/zvibe_minimal"
STORY="$TESTS_DIR/../../../games/catalog/czech.z4"
LOG="$TESTS_DIR/czech_v4_test_output.log"

EXPECT_PERFORMED=386
EXPECT_PASSED=367
EXPECT_FAILED=0
EXPECT_PRINT=19

fail() { echo "FAIL: $1"; exit 1; }

[ -x "$BIN" ]   || fail "no V4-enabled binary at $BIN (build with: make -C .. BUILD_DIR=build_v4 EXTRA_CFLAGS=-DZVIBE_ENABLE_V4=1)"
[ -f "$STORY" ] || fail "story file not found: $STORY"

# Run with a watchdog: a hung interpreter must fail the test, not stall it.
printf 'quit\n' > "$LOG.in"
"$BIN" "$STORY" < "$LOG.in" > "$LOG" 2>&1 &
pid=$!
( sleep "${CZECH_TIMEOUT:-60}"; kill -9 "$pid" 2>/dev/null ) &
watchdog=$!
wait "$pid"
rc=$?
kill "$watchdog" 2>/dev/null
wait "$watchdog" 2>/dev/null
rm -f "$LOG.in"
[ $rc -eq 0 ] || fail "interpreter exited with status $rc, or timed out (see $LOG)"

# Anchored, so a line that merely contains the text does not satisfy the check
grep -Eq "^Performed $EXPECT_PERFORMED tests\.[[:space:]]*\$" "$LOG" \
  || fail "expected 'Performed $EXPECT_PERFORMED tests.'; got: $(grep -m1 'Performed' "$LOG" || echo 'nothing')"
grep -Eq "^Passed: $EXPECT_PASSED, Failed: $EXPECT_FAILED, Print tests: $EXPECT_PRINT[[:space:]]*\$" "$LOG" \
  || fail "expected 'Passed: $EXPECT_PASSED, Failed: $EXPECT_FAILED, Print tests: $EXPECT_PRINT'; got: $(grep -m1 'Passed:' "$LOG" || echo 'nothing')"
grep -Eq "^Didn't crash" "$LOG" || fail "the story did not run to its end"
if grep -q -i "unsupported opcode\|segmentation\|abort\|fatal" "$LOG"; then fail "interpreter error in output (see $LOG)"; fi

echo "PASS: czech.z4 matches the reference interpreter: $EXPECT_PERFORMED tests, $EXPECT_PASSED passed, $EXPECT_FAILED failed, $EXPECT_PRINT print tests"
