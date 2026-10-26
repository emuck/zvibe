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

3. **Addresses, objects, properties, dictionary**, with focused V4 fixtures.
4. **Decoder, new opcode entries, save/restore lifecycle, header init and
   restart paths.** Then enable V4 loading.
5. **Screen table, output streams, `read_char`**, `zscreen_get_cursor`, target
   adapters.
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
