# Testing

Test structure and procedures.

## Test Suite Location

Tests are located in `target/console/tests/`.

## Test Categories

### Unit Tests

**File:** `test_split_memory.c`

Tests the split memory abstraction layer:
- Memory initialization
- Address validation
- Byte and word operations
- Stack operations
- Pointer access
- Boundary conditions

**Run:**
```bash
cd target/console/tests
make test-unit
```

**Expected:** 133 split-memory tests pass, then the screen layer suite below.

**File:** `test_screen.c`

Tests the V4 ANSI screen layer (`src/screen/zvibe_screen.c`) against a minimal
VT100 emulator that models cursor addressing, scroll regions, SGR rendition,
erase, and auto-wrap. Covers split-window scrolling, word wrap, upper-window
clipping, erase semantics, text styles, input echo at the right edge, and
dimension validation.

**Expected:** 97 checks pass. The board-side counterpart is
`make screen-demo` in `target/same51` (see [V4 Feasibility](V4_ANALYSIS.md)).

### V4 screen and stream opcodes

**Files:** `test_v4_ops.c` (169 checks), `test_screen_v4.c` (142 checks),
`test_restart_screen.c` (91), `test_v4term_pty.py` (29), `test_v4_plain.sh`,
`vt100_emu.h`, `data/v4screen.inf` / `v4screen.z4`, `data/v4keys.inf` / `v4keys.z4`.

`test_v4_ops.c` drives the V4-only opcodes (erase, cursor, text style, buffer
mode, output streams including nested stream 3, `read_char`, `scan_table`)
with hand-assembled code. `test_screen_v4.c` runs a small Inform-built V4 story
through the real ANSI screen layer into a VT100 emulator and compares the
resulting screen. Rebuild the story with `inform -v4 v4screen.inf v4screen.z4`.
Both run under `make validate-v4` only.

`test_v4_ops.c` also covers output stream 3 at its limits (sixteen nested captures
each keeping its own text, a seventeenth that must abort, a 301-character table whose
count crosses into the high byte, stream 1 switched off before and during a capture) and
`scan_table` with an inverted branch, a stack store and a table in static memory.
`test_restart_screen.c` runs a story that turns buffering off and picks reverse+bold,
splits the screen and restarts, through the real ANSI layer: afterwards the screen is
unsplit, buffered and unstyled. `test_screen.c` also checks that `zscreen_init` repairs
a terminal left in origin mode, insert mode and the line-drawing set (the emulator models
all three), a split that leaves one lower row, and `erase_window -2` with the upper window
selected. `test_v4term_pty.py` runs the full-screen player on a real pseudo-terminal
(`make test-v4term`): Return, Delete (8) from DEL and Ctrl-H, a lone Escape, Escape and
another key arriving together (neither lost), cursor keys in both encodings, escape
sequences with no key dropped whole, line editing, a line longer than the screen, and
that Ctrl-C and a normal exit leave the terminal modes as they were. `test_v4_plain.sh`
covers a V4 story on the plain path (piped keys, script lines, no escapes; Return is key 13, not
10). Each was checked by reintroducing the bug it guards against.

The pty test also covers: a truncated escape sequence (the next key is not lost), abort,
Ctrl-backslash and hangup (terminal restored), Ctrl-Z followed by `fg`, and a save over 64 KB
(`data/v4huge.z4`, saved from 24 calls deep so the stack adds to the size).

**SAM E51 saves.** `test_delta_pack.c` (335 checks) now also checks `save_crc32` against the
standard CRC-32 test value and that every bit of a 4 KB blob counts, and cuts a packed save at
every length with the recorded length rewritten to match (so the checks inside the runs, not the
length check, must reject it; the cut copy is exactly sized, so an over-read is an ASan error).
`target/same51/tests/test_v4_save_over_big.py` stores a large save, flashes a small story, saves and restores it
after a reset (this failed before the SmartEEPROM tail fix). `target/same51/tests/test_v4_identity.py` flashes release 1 and release 2 of a story, saves on
release 1 and checks that release 2 refuses the save and release 1 still restores it.

**Web terminal.** `make test` in `target/webterm/tests` also runs `test_server_games.py` (the
POSIX server's game list under ASan: a crowded directory, names with quotes and backslashes) and,
in `test_screen_session.c`, a story that reads line after line (`data/v4lines.z4`): the story's
input limit reaches the client and a reconnect after a command replays that command.

For hand testing, build with V4 enabled (`make validate-v4` leaves the binary at
`target/console/build_v4/bin/zvibe_console`) and run a V4 story in a terminal:
`zvibe_console story.z4` switches to the full-screen player automatically. With
`-s`, or when stdin or stdout is not a terminal, it uses the plain line mode.

### RISC-V V4 player (host)

`make -C target/riscv/serv_zvibe/common/sw/tests test` (add `SAN=1` for ASan and UBSan) builds
the Arty firmware's own V4 player (`zvibe_riscv_v4.c`), the real interpreter and screen layer, and
the firmware's flash save layer (`flash_save.c`, with the Arty's slot configuration) against a mock
NOR flash that counts erase-before-program violations, with the console tests' VT100 emulator as
the terminal. 191 checks, using the console test stories (`v4save`, `v4bigsave`, `v4keys`,
`v4lines`, `v4screen`): save and restore across a simulated power cycle, the newest of several
saves, a save from another story (each identity field flipped in turn), a damaged save, a save too
large for its buffer, a flash write error, line editing and the story's input limit, Return, arrows,
Escape and CR LF as keys, the terminal-size probe (no answer, noise, too-small and good answers),
the split screen, and the terminal's state after a game ends. It cannot cover the board: the UART,
the timing of the busy-wait loops, the QSPI flash. Mutations that must fail it: remove the CRC check,
any identity byte, the input limit, the lone-Escape handling, the CR LF rule, the flash error report,
the size sanity check.

### Build configuration and restore safety (V3 / V4 switch)

**Files:** `test_config.c`, `test_save_restore.c`, `test_story.h`

`test_config.c` is built three ways (V3-only, V3+V4, and a 1024-word stack),
each stating what it must be (`EXPECT_V4`, `EXPECT_STACK_WORDS`) so a wrong
define fails the build. It checks the capacity constants, that `Z_IS_V4()` is a
constant in a V3-only build, story size limits, that a rejected oversize load
leaves the loaded machine unchanged, and that the stack setting does not
change how much dynamic memory a story may have. `make test-config-defines`
checks that `ZVIBE_ENABLE_V4` accepts only a literal 0 or 1.

`test_save_restore.c` covers raw saves (`zvibe_restore_data`) and the delta
codec shared by the SAM E51 and RISC-V targets: a save made with a larger stack
must be rejected, never copied past the stack, and a rejected restore must
leave the running machine untouched. It is built three ways, including under
AddressSanitizer and UBSan, and feeds the decoder exact-size and misaligned
inputs so an over-read or a typed cast on unaligned data fails. `make test-delta-copies` keeps the two
copies of `save_delta.c` identical.

`test_delta_pack.c` covers `delta_pack`/`delta_unpack`, which the V4 firmware uses to
fit a save larger than its storage by keeping only the bytes that differ from the
story: round trips (identical, single and merged changes, runs over 65,535 bytes,
no base, random blobs), exact and insufficient output capacity, every truncation and
corruption of the packed data (output must stay untouched), and a real save from the
core restored after packing. Also built under ASan/UBSan; eight deliberately
reintroduced bugs are each caught.

**Run:** `cd target/console/tests && make test-unit`

### Object table, properties, packed addresses and dictionary (V3 / V4 layouts)

**Files:** `test_objects.c`, `test_dict.c`

Each is built twice: V3-only, and V3+V4 (the V3 cases must behave identically in
the dual build). Because the loader still refuses V4 stories, the V4 cases load
a V3 story and set the header version. Fixtures (object tables, property
headers, dictionary words) are written from the Z-machine standard and do not
share the production layout macros; `test_dict.c` also checks literal expected
words worked out by hand. `test_objects.c` covers tree relations with object
numbers up to 299, attributes against raw bytes, properties of 1 to 64 bytes,
`print_obj`, `print_paddr`, `call` and `verify` scaling; `test_dict.c` covers
encoding (including the ZSCII escape and truncation), lookup, empty and
different-length entries, and tokenizing with several separators.

### Loading, header, decoding and save/restore (V3 / V4)

**File:** `test_opcodes.c`

Built twice (V3-only and V3+V4). Instruction streams are assembled by hand from
the standard and run through `zvibe_run`, so the decoder is exercised end to end:
version acceptance, header setup and restart, `call` forms, `call_vs2` with eight
operands and two type bytes, save/restore results, refusal of a save from another
story or version, the status line, and `read`. `make test-config-identity` checks
the defaults of `ZVIBE_VERIFY_SAVE_IDENTITY`.

### V4 conformance gate: Czech

`make test-czech-v4` (run by `make validate-v4`) runs `games/catalog/czech.z4`
on the V4-enabled console and requires exactly the result frotz gives: 386
tests, 367 passed, 0 failed, 19 print tests. The counts are pinned on purpose.
Czech leaves save/restore/restart and much I/O untested and its print tests are
checked by eye, so this is a regression check on opcode behavior, not a claim of
full conformance.

### V4-enabled build

`make validate-v4` (repo root) builds the console with `ZVIBE_ENABLE_V4=1` into
`target/console/build_v4/` and runs the unit tests, Czech and Restaurant
against it. (`make validate` runs the unit tests and Czech on the default
V3-only build.) The console binaries embed a `zvibe-build:v4=N` marker and
`make verify-console-config` refuses to run integration tests against a binary
of the wrong configuration (`EXPECT_V4`, `EXPECT_STACK`). To test another build
directory: `make test-czech ZVIBE_BUILD=<dir> EXPECT_V4=<0|1>`.

### Proving a change is free for V3-only builds

```bash
scripts/core_size.sh                # per-object text/data/bss, SAM E51 and RISC-V
HASH=1 OPT=-O0 scripts/core_size.sh # plus a hash of each object's code and data
```

Run the second form on the tree before and after at several `OPT` levels and
compare: an object whose hash is unchanged has identical code and data. (`ONLY=zvibe_core` limits a run to one file for quick experiments.) Section totals
alone are not enough. (`brew install isl libmpc` if the RISC-V compiler fails
to start on macOS.)

### Integration Tests

Tests run complete games with scripted input.

**Czech Test Suite:**
- File: `games/catalog/czech.z3` (Czech v0.8 by Amir Karger, freely redistributable; see [THIRD_PARTY_NOTICES](../THIRD_PARTY_NOTICES.md))
- Tests Z-machine opcode implementation
- 368 individual tests

**Game Completion Tests (individual targets):**
- Restaurant: `make test-restaurant` — game ships with the repo, runs walkthrough + stress test
- Plundered Hearts: `make test-plundered` — downloads game file, then runs full playthrough
- Seastalker: `make test-seastalker` — downloads game file, then runs full playthrough

Restaurant is also documented as an integrated interpreter test engine in
[Restaurant as a ZVibe Test Engine](RESTAURANT_TEST_ENGINE.md).

**Run:**
```bash
cd target/console/tests
make test-integration
```

## Running All Tests

```bash
cd target/console/tests
make test
```

Or explicitly:
```bash
make test-unit test-integration
```

## Test Scripts

| Script | Purpose |
|--------|---------|
| `target/console/tests/test_czech.sh` | Z-machine conformance — runs as part of `make test-integration` |
| `target/console/tests/test_script.sh` | Game-completion runner (wraps `test_script.py`) — restaurant in default run; plundered/seastalker via individual targets |
| `games/scripts/restaurant-script.txt` | Restaurant walkthrough command input |
| `games/scripts/restaurant-text-check.txt` | Restaurant text-regression markers |
| `games/scripts/restaurant-stress-test.txt` | Restaurant stress-test command input |
| `games/scripts/plunderedhearts-script.txt` | Plundered Hearts walkthrough command input |
| `games/scripts/seastalker-script.txt` | Seastalker walkthrough command input |

## Script Format

Test scripts are plain text files with one command per line. The format supports
directives for handling random game elements (e.g., randomized puzzle codes):

| Syntax | Meaning |
|--------|---------|
| `examine panel` | Plain command — sent to the game |
| `.` | Z-machine no-op — used as a turn separator |
| `@capture name /regex/` | Extract a value from the previous command's output |
| `{name}` | Substitute a previously captured value into a command |
| `# comment` | Comment — skipped by the test harness |

Example from `restaurant-script.txt`:
```
examine panel
@capture panel_code /code:\s*(\d+)/
.
enter {panel_code}
```

Scripts without directives (Plundered Hearts, Seastalker) work as plain command
lists and are fully backward compatible. The restaurant walkthrough uses
`@capture` to handle the randomized panel code, allowing it to run with any
random seed — no deterministic seed required.

## Adding Regression Tests

### Adding a Unit Test

1. Add test function to `test_split_memory.c`:
   ```c
   static void test_new_feature(void) {
       // Setup
       // Action
       // Assert with TEST_ASSERT()
   }
   ```

2. Call from `main()`:
   ```c
   test_new_feature();
   ```


## Test Output

Test results are written to:
- `czech_test_output.log`
- `restaurant_test_output.log`
- `plunderedhearts_test_output.log`
- `seastalker_test_output.log`

## Hardware Tests

### SAM E51

```bash
cd target/same51
make test
```

Tests SmartEEPROM save/restore functionality.

The V4 firmware (`make v4-program`, see `target/same51/README.md`) has three scripts
that need `pyserial` and `pyte` and drive the board over its serial port, each with
its own small story built with Inform 6.44 (`target/console/tests/data`):

| Script | Story | Checks |
|---|---|---|
| `tests/test_v4_game.py` | `v4screen.z4` | status bar, styles, scrolling, wrap, stream 3, `read_char` (letter or arrow key), line editing, `get_cursor`, `scan_table`, `erase_line`, restart prompt |
| `tests/test_v4_save.py` | `v4save.z4` | save stores 1, restore resumes with 2, counter restored, survives a reset |
| `tests/test_v4_bigsave.py` | `v4bigsave.z4` | a 21 KB save packed into the 16 KB SmartEEPROM, exact contents after restore and reset, an oversized change set fails with 0 and keeps the old save |

Czech V4 on the board should give 386 tests, 367 passed, 0 failed, 19 print tests.
Flash the right story first: each script's header gives the `make` line.

### RISC-V FPGA — MAX10 08 Eval Board

```bash
cd target/riscv/serv_zvibe/boards/max10_08_eval/tests
python3 test_uart.py
```

Replays a scripted game session via serial. Auto-detects CP2102 on `/dev/ttyUSB0`.

```bash
# Questa simulation (validate before hardware programming)
cd target/riscv/serv_zvibe/boards/max10_08_eval/sim
make -f Makefile.vsim questa-batch   # XIP boot + UART echo test
```

### RISC-V FPGA — Arty S7-50

```bash
cd target/riscv/serv_zvibe/boards/arty_s7_50/tests
python3 test_uart.py
```

Tests UART communication with hardware.

## Simulation Tests

RISC-V target RTL tests run through a tiered regression runner. All Tier 1 tests use Verilator and require no vendor tools.

### Tier 1 — Verilator (no vendor tools required)

```bash
make sim-tier1          # From repo root — runs all 13 Verilator tests (~2 min)
```

Or individually:

```bash
cd target/riscv/serv_zvibe
./run_regression.sh --tier1
```

Tests included in Tier 1:

| Test | What it exercises |
|------|-------------------|
| `uart_wb_tb` | UART Wishbone controller: TX/RX, FIFO overflow, error flags |
| `servant_mem_mux_tb` | RAM/flash address decode, pipelined ACK |
| `servant_zvibe_mux_tb` | Peripheral decode, case statement, pipelined ACK |
| `s25fl_xip_wb_tb` | QSPI XIP controller: address translation, burst, back-to-back reads |
| `s25fl_xip_burst_tb` | QSPI burst read, cache-line fill, contiguous access |
| `qspi_cache_bram_tb` | 4KB BRAM cache: hit/miss, LRU eviction, write-through |
| `ufm_model_unit_tb` | MAX10 UFM behavioral model: program, erase, status |
| `max10_ufm_unified_tb` | MAX10 Wishbone→Avalon-MM bridge: all transaction types |
| `max10_board_xip_tb` | MAX10 board-level: PLL lock, reset sequencing, boot to UART output |
| `arty_xip_echo` | Arty S7-50 SoC XIP boot path, UART echo (uncached) |
| `arty_xip_echo_cache` | Arty S7-50 SoC XIP boot path, UART echo (4KB BRAM cache) |
| `s25fl_flash_rw_tb` | S25FL QSPI flash read/write/erase model |
| `arty_soc_flash_write_tb` | Arty SoC flash write controller arbitration |

**Pass/fail**: Each test emits `PASS: <name>` or `FAIL: <name> (N errors)` on stdout. The regression script greps for `^PASS:` and exits 0 if all pass.

Logs are captured to `target/riscv/serv_zvibe/regression_logs/`.

### Tier 2 — Questa (vendor UFM model; requires `vsim` license)

```bash
make sim-questa         # From repo root
```

Tests included:

| Test | What it exercises |
|------|-------------------|
| `max10_xip_questa_uart_echo` | Full XIP boot path with Intel vendor UFM model; verifies UART banner output |
| `max10_xip_questa_ufm_write` | Runtime UFM erase/page-write/verify against vendor model |

Prerequisites: Questa FSE on PATH; UFM vendor IP generated (`make generate-ip` in `boards/max10_08_eval/fpga/`); `fpga/ip/ufm/simulation/ufm.v` has `INIT_FILENAME_SIM` set. See `boards/max10_08_eval/sim/QUESTA_UFM_INIT_GUIDE.md`.

### Tier 2 — Vivado xsim (placeholder)

```bash
make sim-xsim           # Currently a no-op (infrastructure not yet implemented)
```

See [`target/riscv/serv_zvibe/common/tb/README.md`](../target/riscv/serv_zvibe/common/tb/README.md) for the full testbench inventory.
