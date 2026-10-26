# V4 Implementation Design

Companion to [V4_ANALYSIS.md](V4_ANALYSIS.md). This document is the plan for
adding Z-machine V4 to the interpreter core once the Stage 1 screen layer passes
on hardware. It was produced by two independent designs (one drafted
separately from a Codex proposal), then an adversarial review in both directions.
Where the two disagreed, the resolution is noted.

## Decision: one parametrized core, one compile switch

```
ZVIBE_ENABLE_V4 = 0   V3 only. Every V4 branch is a compile-time constant 0.
ZVIBE_ENABLE_V4 = 1   V3 and V4 in one build; the story header selects at load time.
```

- **No fork.** Forking the core duplicates the interpreter and doubles the
  maintenance for every bug fix.
- **No version descriptor or vtable for V3/V4 differences.** The differences are
  a handful of constants and a few branches; an indirection layer would cost
  more than it saves.
- **No `ZVIBE_ENABLE_V3=0` (V4-only) option yet.** Both designs first disagreed
  here. After review the two-flag option was withdrawn: V4-only needs the same
  eight operand slots as a dual build, and would save only tens to hundreds of
  code bytes, which is unmeasured. Add it later only if a real linked
  measurement justifies another supported configuration.
- V3-only stays the default for embedded targets. Console and webterm enable V4.

### Mechanism

A small shared header (core files do not currently include `zvibe_api.h`):

```c
/* include/zvibe_config.h */
#ifndef ZVIBE_ENABLE_V4
#define ZVIBE_ENABLE_V4 0
#endif

#if ZVIBE_ENABLE_V4
#  define Z_IS_V4()        (G->header.version == 4)   /* dual build: one runtime test */
#  define Z_MAX_OPERANDS   8
#else
#  define Z_IS_V4()        0                           /* optimizer deletes the V4 branch */
#  define Z_MAX_OPERANDS   4
#endif
#define Z_VSEL(v3, v4)     (Z_IS_V4() ? (v4) : (v3))
```

Rules for using it:

- Use **`#if ZVIBE_ENABLE_V4`** for anything that must not exist in a V3-only
  build: struct members and array dimensions, V4-only source files, V4 table
  entries (a live function-pointer table keeps every referenced handler
  reachable, so the optimizer cannot remove them), API declarations.
- Use **`Z_IS_V4()` / `Z_VSEL()`** for behavior and layout constants inside
  functions that exist in both builds.
- Array dimensions must never use `Z_VSEL` (a runtime expression there is a
  variable-length array and a build error in struct members). Use the
  `#if`-selected `Z_MAX_OPERANDS`.

## Where V3 is baked in today

Grouped by how invasive the change is. Line numbers are as of this branch.

| Impact | Site | Change |
|---|---|---|
| Low | `zvibe.h:72`, `zvibe_core.c:416`, `zvibe_api.c:137`, `zvibe_api_file.c:29,40` | Story size limit 128 KB to 256 KB (loader and static file buffer) |
| Low | `zvibe_core.c:80`, `zvibe_ops_base.c:170`, `zvibe_ops_text.c:167` | Three separate packed-address `*2`; use one helper with shift `Z_VSEL(1,2)`. **Abbreviation word addresses stay x2** (`zvibe_ops_text.c:245`). |
| Low | `zvibe_ops_mem.c:159` | `verify` file-length multiplier x2 to x4 |
| Low | `zvibe_core.c:441,494` | Header: accept version 4, set V4 `flags1`, write screen size (0x20/0x21) and interpreter id (0x1E/0x1F). Restart paths repeat this (`zvibe_api.c:205,400`). |
| Medium | `zvibe_core.c:191`, `zvibe.h:236` | Decoder: `call_vs2` (opcode 236) must read **both** type bytes before any operand data; operands 4 to 8 |
| Medium | `zvibe_ops_base.c:46` | Opcode tables are mutable RAM arrays filled at runtime; make `static const` (see first slice) |
| Medium | `zvibe_object.c:30-57,88,145,231,249,389-438` | Object layout constants and byte-wide relations; attribute limit 32 to 48; object limit 255 to 65535 (`:52,:179`) |
| Medium | `zvibe_object.c:114,317,336` | Property header parsing is duplicated three times; centralize forward decode and backward length decode |
| Medium | `zvibe_ops_text.c:43,324,341,346,369` | Dictionary encoding: 6 to 9 Z-characters, 2 to 3 words. The entry length already comes from the story. |
| High | `zvibe_ops_mem.c:203`, `zvibe_api.c:189,197,357,368` | Save/restore: V4 stores a result (0 failed, 1 saved, 2 resumed after restore) instead of branching |
| High | `zvibe_ops_text.c:446,478`, `zvibe_api.c:152,222` | Input: whole-line only today; needs a character-request state for `read_char` |
| High | `zvibe_api.c:248,292,153` | Status-line extraction and room-id inference are V3-only; V4 games draw their own status line |

Memory needs no widening: `zmem_addr_t` is already 32-bit and the PC is wide.
Only packed addresses reach the high half of a V4 story; ordinary byte
addresses stay 16-bit.

## Opcodes

One compact set of class tables (not a 256-entry table) serves both versions.
For valid V3 stories there are **no slot collisions**: no V3 opcode changes
meaning to an unrelated operation, and the new V4 slots were empty in V3.

New handlers, all under `#if ZVIBE_ENABLE_V4` in a new `zvibe_ops_v4.c`:

| Slot | Opcode | Note |
|---|---|---|
| 1OP:8 | `call_1s` | alias of `op_call` |
| 2OP:25 | `call_2s` | alias of `op_call` |
| VAR:12 | `call_vs2` | alias of `op_call`; needs the decoder change |
| VAR:13-18 | `erase_window`, `erase_line`, `set_cursor`, `get_cursor`, `set_text_style`, `buffer_mode` | go through the screen table; `get_cursor` is **V4** (VAR:16) and needs a cursor query added to `zvibe_screen` |
| VAR:19-21 | `output_stream`, `input_stream`, `sound_effect` | missing today even for V3. `output_stream 3` (memory table) needs real routing and state. `input_stream` is a documented no-op. |
| VAR:22-23 | `read_char`, `scan_table` | |

`op_call` already works from `G->operand_count`, so the four storing call forms
need no new call logic.

Existing opcodes whose behavior differs by version, decided inside the handler
with `Z_IS_V4()`:

- `save`, `restore`: branch to store.
- `read`: V4 permits time and routine operands, keeps the V3 zero-terminated
  buffer layout, and stores nothing.
- `split_window`: clears the upper window **only in V3**.
- `show_status`: V3 only; a no-op in V4.
- Packed-address operations (`call`, `print_paddr`), object, property, attribute
  and dictionary operations (layout), `verify` (length multiplier).

## Screen and I/O

- The platform supplies a **pointer to a const screen-operations table**,
  present only in V4 builds, with explicit flush and reset contracts.
  `src/screen/zvibe_screen.c` is the stock ANSI implementation behind it.
- The existing V3 `split_window`/`set_window` callbacks stay for V3-only builds.
- `read_char` adds a `ZVIBE_WAIT_FOR_CHAR` state and `zvibe_input_char()` to
  the API.
- **Timed input is deferred.** The standard makes it optional: clearing
  `flags1` bit 7 means the interpreter does not support it, and games must
  check. The canonical V4 games are *A Mind Forever Voyaging*, *Bureaucracy*,
  *Nord and Bert*, and *Trinity*. None needs a timer for ordinary play.
  *Border Zone* is V5 and does require real-time interaction. Timed input would
  add roughly 150-300 lines and is the riskiest piece (interrupt return, input
  redraw).

## Order of work

Each slice leaves the V3-only build byte-for-byte equivalent or smaller, and
each is a separate commit.

1. **Const opcode tables.** **Done** (`4076301`). No behavior change. Measured
   with `scripts/core_size.sh`, core objects only:
   - SAM E51 (xc32 `-O2`): text 9,504 to 9,308 (-196), RAM 17,960 to 17,572
     (-388). The full firmware shrinks by 244 bytes of flash and 388 bytes of
     RAM.
   - RISC-V (rv32i `-O1`, the MAX10 and Arty flags): text 18,600 to 18,160
     (-440), RAM 17,960 to 17,572 (-388).
   Czech 368/368, Restaurant, and the unit tests pass.
2. **`zvibe_config.h`**: **Done.** `include/zvibe_config.h` is now the one
   place that defines `ZVIBE_ENABLE_V4` (validated: only a literal 0 or 1),
   `Z_IS_V4()`, `Z_VSEL()`, `Z_MAX_OPERANDS`, `Z_STORY_MEM_SIZE`, and a
   configurable `ZVIBE_STACK_WORDS`. The old `Z3_STACK_SIZE`,
   `Z3_STORY_MEM_SIZE` and `Z3_MAX_OPERANDS` are gone. V4 stories are still
   refused. In a dual build a V3 story keeps its 128 KB limit, checked before
   any state is touched.

   The adversarial review of this slice (two rounds, with Codex) also forced
   real fixes that belong with it, because a configurable stack makes them
   reachable:
   - `delta_decode` copied a saved stack into `stack_mem` with no bound. It now
     takes the capacity, rejects an oversized stack, and checks the structure of
     the whole save (lengths and bounds; not that the PC or base pointer are
     meaningful) before writing anything. The core's raw restore does the same.
     Both `save_delta.c` copies were updated, read the header with `memcpy`
     instead of a typed cast, and compare remaining lengths instead of forming
     pointers past the end; a test keeps the two copies identical.
   - The load check that keeps 1 KB of the RAM buffer free no longer follows
     `ZVIBE_STACK_WORDS`; it keeps its historical fixed value.

   **What the V3-only proof showed.** The first measurement, section totals,
   was not proof, and the review showed it. `scripts/core_size.sh` gained
   `HASH=1` (a hash of each object's disassembly, relocations and section
   contents) and `OPT=`. At `-O0`, `-Os`, `-O1`, `-O2` and `-O3`, on SAM E51
   (xc32) and RISC-V, against the previous commit, **eight of the nine core
   objects have identical code and data; only `zvibe_core` differs**, because
   of the restore-safety change, and it gets *smaller*: SAM E51 -8 to -12 bytes
   at every level, RISC-V -16 to -28 bytes at every level except `-Os`, where it
   grows by 20 bytes. No target in this repository builds RISC-V with `-Os`
   (SAM E51 uses `-O2`, RISC-V uses `-O1`); that growth is reported, not
   hidden. Core totals with V4 off: SAM E51 9,308 to 9,300 bytes, RISC-V 18,160
   to 18,132, RAM unchanged. Reviewers cannot guarantee equal size at every
   optimization level for any C rewrite, so the rule is: shipped build settings
   must not grow, and every other level is measured and recorded.

   **What a dual build costs** (`ZVIBE_ENABLE_V4=1`, core only): SAM E51 +32
   bytes of flash and +8 bytes of RAM (the four extra operand slots), RISC-V +24
   bytes and +8 bytes. With file I/O enabled (hosted builds only) the existing
   static file buffer grows from 128 KB to 256 KB.

   **How it is tested.** `make validate` (V3-only) runs the unit suites and
   Czech; `make validate-v4` (V4 compiled in, V3 stories) runs the unit suites,
   Czech and Restaurant. The console binaries embed a configuration marker
   (V4 flag and stack size) and the tests refuse to run against the wrong
   configuration; test binaries rebuild when headers or compiler flags change.
   The restore tests also run under AddressSanitizer and UBSan, with exact-size
   inputs so an over-read fails. Nine deliberate bugs reintroduced in a scratch
   copy are each caught by a test. See [TESTING.md](TESTING.md).

3. **Addresses, objects, properties, dictionary**: **Done** (`90131d2`,
   `617ae25`). V4 stories are still refused by the loader; the V4 code paths are
   exercised by tests that load a V3 story and set the header version.
   - `z_packed_to_byte()` (x2 V3, x4 V4) is used by routine calls, `print_paddr`
     and unpacking; `verify` scales the header length x2/x4. Abbreviation
     addresses stay x2.
   - Object table: 9/14-byte entries, 32/48 attributes, 1/2-byte
     parent/sibling/child, 31/63 defaults, 255/65535 objects, behind layout
     macros and a few small helpers. Property headers: V3 one byte; V4 one or
     two bytes, size 1-64, six-bit numbers. One forward and one backward
     decoder replace three copies of the size parsing.
   - Dictionary: 6 vs 9 Z-characters, 2 vs 3 words, compared a word at a time.
   - `Z_VERSION_HELPER` forces the tiny helpers inline on GCC-family compilers;
     the field offset and read are macros so a V3 build folds them to constants.

   **Size, V3-only, core text** (`scripts/core_size.sh`, shipped settings SAM E51
   `-O2` and RISC-V `-O1`), against the commit before:

   | | SAM E51 | RISC-V |
   |---|---|---|
   | before | 9,300 | 18,132 |
   | V4 layering (`90131d2`) | 9,273 (-27) | 18,052 (-80) |
   | plus bug fixes (`617ae25`) | 9,361 (+61 net) | 18,260 (+128 net) |

   RAM is unchanged. The layering shrinks V3-only builds; the growth is the three
   fixes below (+88 / +208 bytes, mostly the escape encoding), which is why they
   are a separate commit that can be reverted on its own. Unoptimized (`-O0`)
   builds grow by about 9% in `zvibe_object` (+236 bytes SAM E51, +408 RISC-V)
   because the compiler does not fold the version constants; no target in this
   repository builds with `-O0`.

   **Three V3 bugs found and fixed here** (inherited, not introduced):
   - A character outside the two alphabets (for example `@`) was dropped from a
     dictionary word instead of being written as the ZSCII escape, and at the
     length limit a shift that did not fit was skipped while a later letter took
     its slot. The standard truncates the encoded stream, so V3 `abcde!f` must
     end `0xA545`; the old code produced `0xA54B`.
   - An empty dictionary started the binary search at entry 65535.
   - `print_obj` on an object with no short name read the next property header
     as text and ran until it found an end bit (it crashed the test).

   **Testing.** `test_objects.c` and `test_dict.c` (V3-only and V3+V4 builds). The
   fixtures are written from the standard, not from the production layout macros,
   with literal expected words worked out by hand. Tree relations use objects 256
   to 299 so every parent, sibling and child value needs both bytes; attributes
   are checked against raw bytes with sentinel neighbours. 25 deliberately
   reintroduced bugs are each caught by a test.

   **Known gaps carried to later slices** (from the review):
   - `get_next_prop` on a property the object does not have returns 0; the
     standard recommends halting (inherited from V3).
   - The API's status-line code limits room ids to 255 (`zvibe_api.c:248`, `:318`)
     and the webterm frontend has a 128 KB buffer and discovers only `.z3` files;
     both must change before a frontend can advertise V4.
   - Negative dictionary entry counts (unsorted user dictionaries) belong to V5
     `tokenise` and are not needed for V4.
   - Calls and printing above 128 KB need a story that large to test; slice 6.
4. **Decoder, call forms, save/restore lifecycle, header init and restart,
   V4 loading**: **Done.** V4 stories load in a V4-enabled build, and
   `czech.z4` gives exactly the result frotz gives (386 tests, 367 passed, 0
   failed, 19 print tests). Czech needed nothing beyond this slice: no screen
   opcodes.
   - `call_1s`, `call_2s` and `call_vs2` map to the existing `op_call`. The
     decoder reads **both** operand-type bytes of `call_vs2` (opcode 236)
     before any operand data, even when the first already ends in "omitted",
     and takes up to 8 operands.
   - Save and restore: V3 branches on success; V4 stores 0 (failed), 1 (saved)
     or 2 (restored, stored into the *save's* variable). One inline helper,
     `z_do_save_result()`, is exactly the old `z_do_branch()` call in a V3-only
     build.
   - Header: `z_init_interpreter_header()` runs at load and on both restart
     paths. V3 sets flags1 bit 4 as before; V4 clears the capability bits
     (bold, italic, fixed-space, timed input) because no screen is attached at
     this level, and writes the interpreter number/version and screen size
     (compile-time defaults 24x80 until slice 5 supplies the real size). The
     standard revision (0x32/0x33) was **deliberately not written** in this
     slice: declaring 1.1 claims every opcode. Slice 5 completed the set and
     writes it.
   - The status-line callback is skipped for V4 stories (a V4 game draws its own).
   - A V4 story whose dynamic memory is smaller than the 64-byte header is
     refused at load.
   - **Restoring a save of another story is refused**, found by review: a V3
     save restored into a V4 story put a V3 header and continuation into the V4
     machine and then misdecoded the next instruction. A raw save is accepted
     only if version, release, addresses, serial, length and checksum match, and
     after a restore the interpreter-owned header fields are rewritten. This is
     governed by `ZVIBE_VERIFY_SAVE_IDENTITY`: on for hosted builds and any V4
     build, off for freestanding V3-only firmware (where saves live in per-game
     slots and the check cost 70-100 bytes of flash). Overridable.
   - V3-only core text against the previous commit on the shipped settings: SAM
     E51 `-O2` 9,361 to 9,357, RISC-V `-O1` 18,260 to 18,252; RAM unchanged.

   **Testing.** `test_opcodes.c` assembles instruction streams by hand from the
   standard and runs them through `zvibe_run`: version acceptance and size
   limits, header setup and restart, `call` in V3 and V4 forms, `call_1s`,
   `call_2s`, `call_vs2` with 7 arguments, mixed operand types across the two
   type bytes, stack-variable operands, save and restore results into
   different variables, a non-empty and a deeper-than-current stack, a restore
   from an interpreter with other settings, refusal of saves from another
   release, serial, checksum or version, the status line, and `read` with a real
   parse buffer. `test_czech_v4.sh` pins Czech's counts to frotz's. About 40
   deliberately reintroduced bugs are each caught by a test, and the Czech gate
   alone catches 8 of 9 sampled decoder, object and address bugs (it does not
   exercise 9-character dictionary words; the dictionary unit tests do).

   **A caveat about the gate.** Czech leaves save, restore, restart and much I/O
   untested and its 19 print tests are checked by eye, so matching frotz is a
   regression check on opcode behavior, not a claim of full conformance.

   **Known gaps carried forward:**
   - The two live Flags 2 bits (transcript and fixed-pitch) must survive a
     restore and a restart; they are overwritten today (inherited from V3).
     They belong with the screen and stream work in slice 5, which gives them
     meaning.
   - V4 `read` with a zero parse-buffer address is a V5 convention; V4 stories
     always pass one.
   - Public status and room queries still use V3 conventions.
   - Header screen size must come from the frontend, not compile-time defaults.
5. **Screen table, output streams, `read_char`**, `zscreen_get_cursor`, target
   adapters. **Done (core, API, screen layer, console player).**

   - `include/zvibe_screen_ops.h` is the one public screen interface: a table of
     optional callbacks (`erase_window`, `erase_line`, `set_cursor`,
     `get_cursor`, `set_text_style`, `set_buffer_mode`, `set_window`,
     `split_window`) plus capability bits. `zvibe_set_screen()` attaches it.
     `src/screen` (ANSI/VT100, scroll region for the lower window, word wrap,
     no frame buffer) provides `zscreen_ops` as one implementation; a frontend
     with its own display supplies a different table.
   - `src/core/zvibe_ops_v4.c` holds the V4-only opcodes, so a V3-only build
     does not compile them: `erase_window`, `erase_line`, `set_cursor`,
     `get_cursor`, `set_text_style`, `buffer_mode`, `output_stream` with
     stream 3 memory tables (nested to `ZVIBE_STREAM3_DEPTH`, text routed
     exclusively while open, length word written on close), `input_stream` and
     `sound_effect` as no-ops, `read_char`, `scan_table`.
   - `read_char` suspends the interpreter with `ZVIBE_WAIT_FOR_CHAR`; the
     frontend answers with `zvibe_input_char(ctx, zscii)`. Timed input is not
     supported, and flags1 bit 7 is never set.
   - The header now declares Standard 1.1 (0x32/0x33), since the V4 opcode set
     is complete.
   - `target/console/zvibe_v4term.c` is the reference frontend. `zvibe_console`
     hands a V4 story to it when stdin and stdout are terminals and no `-s`
     script is given (POSIX only; V3 stories and piped runs keep the plain
     path, which also draws the V3 status bar over a V4 story). It sizes the
     screen from the terminal, edits input lines itself, maps cursor keys to
     ZSCII, honours `-t`, `-c`, `-r`, and saves like the console.
   - **Cost** of enabling V4 on top of V3 (SAM E51 `-O2`, RISC-V `-O1`): about
     +2,036 B and +4,268 B of flash and +56 B of RAM. V3-only objects are
     byte-identical to before the slice at every optimisation level.
   - Flags 2 bits 0 and 1 (transcript, fixed-pitch) now survive restart and
     restore in V4 stories (`Z_FLAGS2_KEEP/RETURN`); V3 stories and V3-only
     builds are unchanged.
   - Review fixes: `get_cursor` checks every byte against dynamic memory;
     stream 3 keeps extended ZSCII and drops null; `scan_table` treats a match at
     address 0 as a match; restart re-enables buffering and clears the text
     style; the player restores the terminal on SIGINT/SIGTERM/SIGTSTP, reports
     Delete as ZSCII 8 and tells a lone Escape from a sequence by timeout.
   - **Testing.** `test_screen_v4.c` runs `data/v4screen.z4` (built from
     `v4screen.inf`, Inform 6.44) with the real ANSI layer feeding a VT100
     emulator and checks the resulting screen cell by cell; `test_v4_ops.c`
     covers the opcodes and streams directly; `test_screen.c` the layer alone.

   **Deferred from this slice:**
   - Frontend migrations are separate work: SAME51 firmware (replace the
     firmware-side echo with the UART adapter; needs the board), RISC-V
     firmware, webterm (128 KB and `.z3`-only limits).
   - Public status and room queries are V3-shaped.
   - `get_next_prop` on a property that does not exist.
   - Timed input.
   - `split_window` is clamped to `rows - 2` lines (DECSTBM needs a lower
     window of at least two rows); a story that splits off all but one row gets
     a shorter upper window than it asked for.
   - The console's V4 player has no horizontal scrolling in its line editor
     and is POSIX only.
   - A V4 madlibs story as an acceptance demo once the frontends are migrated.
6. **Regression and playthroughs.**

### Proving "zero cost"

Equal totals alone are not enough. For SAME51 and SERV, with identical
toolchains, compare before and after: `.text`, `.rodata`, `.data`, `.bss`, the
loadable image bytes, symbol sizes, normalized disassembly, and
`sizeof(zState)`. Investigate any difference. Run `test-czech` and
`test-restaurant`, then the same V3 stories in the dual build. Also check that a
V4-only story is rejected by a V3-only build, and the reverse.

### V4 gate: Czech

`czech.z4` (Czech 0.8 built with `inform -v4`; see
[V4_ANALYSIS.md](V4_ANALYSIS.md#stages)) is the conformance gate for slices 3
and 4: same result as frotz (386 tests, 367 passed, 0 failed, 19 print tests).
It already covers `call_1s`, `call_2s` and `call_vs2`. The repo runs the V3
build today (368/368), so the harness extends naturally.

### V4 fixtures needed

Eight-operand call, object ids above 255, a 64-byte property, a third
dictionary word, execution above 128 KB, and a restored-save result of 2.
Inform 6 (`-v4`) is installed locally and frotz is the reference.

## Estimate

About 250-400 existing lines touched and 450-750 added, excluding tests and
target adapters. Timed input would add 150-300. The earlier draft estimate
(150 touched, 400 added) undercounted integration and tests.

Highest risks: restored-save continuation (resume with result 2), the
two-type-byte decoder, and output stream 3 routing.

## Memory

Per-target RAM, flash and save limits, and where V4 does not fit, are in
[V4_MEMORY.md](V4_MEMORY.md). Design consequences: `ZVIBE_MEMORY_BUFFER_SIZE`
and the Z-machine stack size become explicit per-target settings
(`ZVIBE_STACK_WORDS`), and the single-game SAME51 firmware moves to delta saves.

## Existing issues to fix independently

- Opcode tables are mutable RAM (first slice above).
- `op_call` hardcodes `operands[0] * 2` beside `z_unpack_addr`.
- The local-default byte handling in `op_call` (`zvibe_ops_base.c:199`) looks
  like a little-endian read of story data, but it compensates for the
  interpreter's host-endian variable representation. It is correct on
  little-endian hosts only; the comment is misleading.
- The comment calling the stack buffer "shared" is inaccurate; stack storage
  stays separate in `zState`. The load check at `zvibe_core.c:475` therefore
  reserves 1 KB of the buffer for a stack that is not there.
- `output_stream`, `input_stream` and `sound_effect` are unimplemented, which
  affects V3 stories too.
