# Z-machine V4 Feasibility

Status: **Stage 1 (screen layer) in progress** on branch `dev/v4-screen-layer`.
V4 stays out of scope for the main line until the Stage 1 hardware test passes.

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

1. **Screen layer + host test + board exerciser** (`include/zvibe_screen.h`,
   `src/screen/zvibe_screen.c`, `target/console/tests/test_screen.c`,
   `target/same51/src/screen_demo.c`, `make screen-demo`).
2. **Minimal V4 core**, wired to the screen layer. Must cover the whole matrix
   above, not only what the test story uses: `read_char`, timed `read`
   (resumable editing, interrupt routine), `call_vs2`, store-style
   `save`/`restore`, `scan_table`, ZSCII decoding above the ASCII screen API
   (`print_char(0)` vs string zero, extended characters), and replacing the
   firmware-side echo in `zvibe_same51.c` so the screen layer is the single
   owner of the cursor.
3. **Standalone Inform 6 `-v4` madlibs story** (form in the upper window,
   answers reused for Douglas-Adams-style humour). Transcript compared to frotz.

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
