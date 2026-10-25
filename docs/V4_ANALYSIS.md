# Z-machine V4 Feasibility

Status: **Stage 1 (screen layer) passes on SAME51 hardware** (2026-10-25,
automated run through a terminal emulator; a visual check in a real terminal
program is still to do). Work is on branch `dev/v4-screen-layer`. V4 stays out
of scope for the main line until the remaining green-light items below are done.

How V4 will be built in the core (one parametrized core, one `ZVIBE_ENABLE_V4`
compile switch, V3-only builds unchanged or smaller) is in
[V4_DESIGN.md](V4_DESIGN.md).

## Why V4

*Bureaucracy*, *Trinity*, and *A Mind Forever Voyaging* are V4. V4 does not
need undo, sound, or heap allocation (those are V5+), so it fits the static
allocation model. The real new cost is the **screen model**: V4 drops the
automatic status line, and games draw their own in a fixed-width upper window.

## Can a UART do it?

Yes, if the far end is an ANSI/VT100 terminal. The terminal remembers the
screen, so the interpreter keeps no frame buffer: one wrap line plus cursor
fields (`zscreen`, 184 bytes).

| Z-machine need | Terminal mapping |
|---|---|
| `split_window` | DECSTBM scroll region below the upper window |
| `set_cursor` | `CSI row;col H` |
| `set_text_style` | SGR 7 reverse, 1 bold, 4 underline (italic) |
| `erase_window` / `erase_line` | `CSI 2K` per row / `CSI K` |
| fixed-pitch header bit | always true on a terminal |
| screen size (header 0x20/0x21) | cursor-position probe (`CSI 6n`) |

## V4 changes outside the screen

| Area | V3 | V4 |
|---|---|---|
| Packed addresses | x2 | x4 (`z_unpack_addr`, `verify` file-length scaling) |
| Story size | 128 KB | 256 KB |
| Dictionary | 6 chars, 4-byte encoding | 9 chars, 6-byte encoding |
| Objects | 9 bytes, 32 attrs, 255 objs, 31 defaults | 14 bytes, 48 attrs, 65535 objs, 63 defaults |
| Property headers | 1 byte, max 8 data | 1-2 bytes, max 64 data |
| Calls | `call` | `call_vs`, `call_1s`, `call_2s`, `call_vs2` (8 operands; parser allows 4 today) |
| `save`/`restore` | branch | store result |
| `read` | 2 operands | adds timer + routine (timed input) |
| New opcodes | none | `read_char`, `scan_table`, `get_cursor`, `erase_*`, `set_cursor`, `buffer_mode`, `set_text_style`, `input_stream`, `sound_effect` |
| `show_status` | used | illegal |
| Header `flags1` | status-line bit | bold/italic/fixed-pitch/timed-input bits |

## Stages

**Gate (this document):** prove a UART terminal can host the V4 screen model.

1. **Screen layer + host test + board exerciser** (`include/zvibe_screen.h`,
   `src/screen/zvibe_screen.c`, `target/console/tests/test_screen.c`,
   `target/same51/src/screen_demo.c`, `make screen-demo`). Built and passing on
   the host. Next step: run the exerciser on the SAME51 and check the criteria
   below.
2. **Standalone Inform 6 `-v4` madlibs story** (form in the upper window,
   answers reused for Douglas-Adams-style humour). It needs no interpreter
   work, so it can be written and checked against frotz in parallel with
   Stage 1. Its transcript becomes the reference for the first V4 run.

**After the gate passes:** the core work follows the ordered slices in
[V4_DESIGN.md](V4_DESIGN.md#order-of-work): const opcode tables, config header,
addresses/objects/properties/dictionary, decoder and save/restore lifecycle,
screen/streams/`read_char`, then regression and playthroughs. That work must
cover the whole V4 matrix above, not only what the test story uses, and must
replace the firmware-side echo in `zvibe_same51.c` so the screen layer is the
single owner of the cursor.

## Green-light criteria

Transcript equality only proves text output. It cannot show cursor placement,
styles, or input integrity, so the test needs more than frotz parity:

- No screen corruption after the long-scroll and word-wrap phases on the board.
- Status bar and form survive lower-window scrolling.
- Echo and backspace correct in the lower window, including at column 80.
- Burst phase: exact RX byte count and byte-sum match the host, with zero ring
  overflow and zero UART hardware errors. At 115200 8N1 the ring covers about
  22 ms of full-rate input, so a supported paste limit or flow control must be
  decided.
- Terminal size is probed, and the header values agree with it.
- Verified in at least two terminal programs (scroll-region handling differs).
- Stage 3 story transcript equals frotz for the text; screen state is checked
  separately by the host VT100 emulator.

## Running the Stage 1 hardware test

Host side (no board needed): `cd target/console/tests && make test-unit`
runs the 66 screen-layer checks against the VT100 emulator.

Board side, automated: flash the demo, then
`python3 target/same51/tests/test_screen_demo.py` (needs `pip install pyserial
pyte`). It resets the board with `edbg -x 100 -t same51`, answers the size
query, runs all four phases through a pyte terminal emulator, types into the
form with backspace edits, pastes a known payload, and checks the screen and
the RX statistics. 32 checks.

Board side, by eye (SAM E51 Curiosity Nano):

1. `cd target/same51 && make screen-demo` (builds `build_screen_demo/`, about 9.8 KB).
2. `make screen-demo-program` flashes it with `edbg`. To restart the demo
   later without reflashing, `edbg -x 100 -t same51` pulses reset (`-b` is
   verbose, not boot).
3. Open the EDBG virtual COM port at 115200 8N1 in an xterm-compatible
   terminal, at least 56 columns wide. Press the board reset so the demo
   probes the terminal size on start.
4. Press RETURN to step through the four phases and check each against the
   green-light criteria below:
   - Phase 1: the reverse-video status bar stays on row 1 through 60 scrolled
     lines and updates to `Score: 60`.
   - Phase 2: no word is split across lines; reverse, bold and underline stop
     at their words.
   - Phase 3: the form box is intact; type the answers with backspace; the
     answers appear in the form afterward.
   - Phase 4: paste a known block of text while the burst runs. Compare the
     reported RX byte count and byte-sum with the host
     (`python3 -c "print(sum(open('f','rb').read()))"`). Ring overflow and
     UART hardware errors must both be 0.
5. Repeat in a second terminal program; scroll-region handling differs.

If RX is silent, suspect the interrupt wiring first: the demo is the first
code to use the SERCOM5 RX interrupt, which was previously mis-numbered.

## Stage 1 hardware results (2026-10-25)

SAM E51J20A (Rev D), 115200 8N1, driven through pyte as an 80x24 terminal.

- All 32 automated checks pass: the status bar stays on row 1 in reverse video
  through 60 scrolled lines and updates; words wrap at boundaries with reverse,
  bold and underline attributes landing on the right cells; the form box stays
  intact while answers are typed with backspace and then filled in; the size
  probe gets its answer on start.
- RX interrupt (first use of the corrected IRQ numbering): a 200-byte paste at
  full line rate during the burst arrives exactly, count and byte-sum, with
  zero ring overflow and zero UART hardware errors. A 2000-byte paste also
  arrives exactly with zero hardware errors while the transmitter is saturated.
  Ring overflow in that run (1745) is the demo's 255-byte ring filling because
  the demo does not read during the burst, as expected.
- Consequence for Stage 2: the hardware keeps up; the supported paste size is a
  software choice. The real input path must drain the ring while printing, or
  use a larger ring.

Real terminal, terminal 1 (Cursor's built-in terminal, xterm.js, through
`screen /dev/cu.usbmodem14102 115200`): phases 1-3 confirmed by eye on 2026-10-25.
Reverse, bold and underline render and stop at their words; wrapping and the
bottom-anchored lower window match the emulator capture; the form stays intact;
backspace works. (The underlined text in phase 2 is also bold because Z-machine
style bits accumulate until a roman reset; the demo does not clear bold first.)

One unexplained event: in the same session the burst phase reported
`UART hw errors: 1` with 0 bytes received. That counter was cumulative since
boot, so the error may have happened at any point, including when the terminal
connected. It could not be reproduced: 8 trials of closing and reopening the port,
including GNU screen connecting and quitting, gave 0 errors, as did every
automated run. The demo now reports burst errors and since-boot errors
separately, so a recurrence will show where it happened. A framing error also
makes the driver flush the RX FIFO, so if it recurs under load it can drop bytes.

Still open for the green light: a second terminal program (scroll-region
handling differs between terminals), and the Stage 3 story.

## Known limits of Stage 1

- Input does not wrap past the right edge of the line; it stops.
- Input is edited in the lower window only. The demo does not type directly
  inside the upper window.
- `erase_window(-2)` resets the upper cursor; frotz preserves it. Upper-window
  newline at the last row stays on that row; frotz returns to the top-left.
  Both are compatibility choices to revisit against real V4 games.
- The lower window keeps at least 2 lines (`split_window` is clamped to
  rows-2) because DECSTBM rejects a one-line region.
- Minimum terminal width is 56 columns; maximum is `ZSCREEN_MAX_COLS` (80).

## Adversarial review (Codex, 2026-10-22)

Findings that held up and were fixed:

- Vector table in `startup_xc32.c` omitted reserved slots 42-44, so SERCOM5
  handlers sat three entries early. Fixed through IRQ 69.
- `same51_minimal.h` had wrong IRQ numbers (SERCOM5 80-83 instead of 66-69,
  NVMCTRL 37/38 instead of 29/30). The firmware therefore enabled the wrong
  NVIC line; RX interrupts never worked in any build and polling hid it.
  RX interrupts are now opt-in via `SERCOM5_USART_EnableRxInterrupt()`.
- Wrap bugs: an exactly-full line moved its last word down; a flush followed
  by more text could split a short word.
- Upper window did not re-home on repeated selection; init put the lower
  cursor at the top instead of the V4 bottom-left; one-line lower window made
  an invalid scroll region; echo/backspace broke at the last column; dimension
  overflow; terminal modes (origin, insert, charset) not established.
- Test emulator was too forgiving: blanks now inherit the current rendition
  (bce), degenerate scroll regions are rejected, and six mutation checks
  confirm the new tests catch the bugs they target.

Not yet addressed:

- `startup_xc32.c` entries after IRQ 70 still do not match the device
  (no SERCOM6/7, other peripherals shifted). All are unused weak dummies.
- `zvibe_same51.c` still echoes in firmware and injects CRLF; Stage 2 replaces it.
