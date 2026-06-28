# ZVibe Development History

ZVibe is a Z-machine Version 3 interpreter targeting resource-constrained RISC-V FPGA platforms.
This document covers the project's development from May 2025 through its public release in
June 2026.

The name comes from "vibe coding": an experiment to see how far hardware and software engineering
background, combined with AI-assisted development, could take a non-trivial embedded systems
project from scratch.

---

## Target Development Timeline

The chart shows when work began on each target and when it reached a stable
state. `●` marks start and completion; `→` indicates ongoing or unshipped work.

```
              ←──────────────── 2025 ────────────────→←───── 2026 ─────→
              May  Jun  Jul  Aug  Sep  Oct  Nov  Dec  Jan  Feb  Mar  Jun
              │    │    │    │    │    │    │    │    │    │    │    │
console       ╭──────────────────────────────────────────────────────────→
sam-e51                      ╭─────╮
arty s7-50                                       ╭─────────╮
max10                                                 ╭────╮
web                                                        ╭────────╮

restaurant                                                      ╭───╮
```

`restaurant` = separate game repo; developed as a ZVibe test vehicle.

| Target | Period | Key milestone |
|--------|--------|---------------|
| Console | May 2025+ | First working interpreter; Czech 368/368 |
| SAM E51 | Aug–Sep 2025 | 25 KB RAM; multi-game flash layout |
| Arty S7-50 (SERV) | Dec 2025–Feb 2026 | QSPI XIP controller; 2.7× cache speedup on hardware |
| MAX10 | Jan–Feb 2026 | UFM XIP; save/restore with wear-leveling; 100 MHz timing |
| Web terminal | Feb–Jun 2026 | WebSocket session layer; 17/17 tests; merged Jun 19 |
| Restaurant game | Mar–Jun 2026 | 42-point game; 3 regression scripts (separate repo) |

---

## May 2025 — First Working Interpreter

The project started with a Z-machine Version 3 interpreter written in C for desktop (console)
platforms. The initial focus was getting the core opcode engine correct and validating it against
the public-domain czech.z3 conformance suite before touching any hardware.

Early work established a platform-abstraction API that would later allow the same core to run
on microcontrollers and FPGAs without modification.

## July 2025 — Split Memory Architecture

The most significant algorithmic milestone of the project: a split memory architecture that
reduced peak RAM usage from 160 KB to approximately 25 KB — an 84% reduction.

The original design allocated a monolithic block covering the full story file, dynamic state,
and stack. The redesign was directly inspired by how Infocom's original interpreters handled
memory on 1980s home computers. Machines like the Apple IIe (64 KB total RAM) and the Atari ST
(512 KB, though earlier Infocom titles targeted much tighter configurations) couldn't hold a
full game image in RAM alongside the interpreter. Infocom's solution was to treat the story
file as a read-only resource on disk or ROM and page in only what the interpreter needed at
runtime.

ZVibe applies the same principle to flash: read-only story data stays in flash and is accessed
via XIP (Execute-In-Place) or DMA. Only the truly dynamic Z-machine state — the global
variable table, object properties, and the call stack — lives in RAM. This is what makes the
interpreter viable on microcontrollers and FPGAs with tens of kilobytes of SRAM.

Czech conformance tests (368/368) passed at this stage.

## August–September 2025 — First Embedded Target

ZVibe was ported to the Microchip SAM E51 Curiosity Nano, a Cortex-M4F microcontroller. This
was the first real hardware validation of the split memory design.

The port drove further optimization work:
- Two rounds of RAM reduction on the SAM E51 (90 KB → 41 KB → 25 KB) through eliminating
  save buffers and tightening the memory layout
- Chunked text output to eliminate a 512-byte stack buffer
- A `multi_game_menu` branch added an on-device game selector for multi-game flash layouts
- A unified cross-platform build system (console + SAM E51 building from the same core)
- Python bindings via a shared library for scripted testing

## December 2025 — RISC-V FPGA SoC

An earlier attempt to implement the interpreter directly in RTL was tried and
abandoned in favor of SERV, a proven bit-serial RV32I CPU core that fits in
approximately 200 LUTs. A complete SoC was assembled using a TDD approach — each subsystem verified before
integration:

- Wishbone-based interconnect
- UART peripheral (115200 baud) with hardware-verified echo test
- QSPI XIP flash controller built across 9 milestones: command framing → address transmission
  → data read → continuous read mode → full Wishbone integration
- Dual simulator support: Verilator (fast iteration) and Questa/ModelSim (vendor flash model)

By Christmas 2025, ZVibe was running a complete Z-machine game end-to-end on real FPGA
hardware (Digilent Arty S7-50) over UART.

## January 2026 — Intel MAX10 Board + Timing Closure

A second FPGA target was added: the Intel MAX10 08 Evaluation Board. Unlike the Arty, the
MAX10 uses on-chip User Flash Memory (UFM) instead of external QSPI — a different flash
architecture requiring a new XIP controller, a different write HAL, and new simulation models.

Key milestones:
- 100 MHz timing closure (setup +0.946 ns, hold +0.274 ns; FMAX 118.65 MHz)
- XIP-only boot: CPU resets directly into firmware in UFM at 0x80000100, no bootloader copy
- UART echo test on hardware; character-level input with line editing confirmed working
- A prescaler bug causing character corruption identified and fixed
- Boot stub technique documented for bring-up debugging on new targets (if XIP hangs silently,
  a RAM-resident boot stub confirms the FPGA and SRAM are alive before touching flash)

## February 2026 — Save System, Cache, and Production Hardening

The most feature-dense month of the project.

**UFM Write / Save System (Feb 6–12)**
A complete runtime flash write HAL for the MAX10 UFM was built and verified on hardware.
The save system uses wear-leveling slot rotation so repeated saves don't burn through a single
page. SAVE and RESTORE commands are functional end-to-end, with CRC32 validation on restore.
Verified on hardware February 12, 2026.

**QSPI XIP Cache (Feb 15–17)**
A 4 KB BRAM cache was added to the Arty QSPI XIP controller. Measured on hardware with a
232-command Plundered Hearts replay: median response time dropped from 1.481 s to 0.546 s —
a 2.7× speedup. The cache costs approximately 500 LUTs.

**ALTPLL Refactor (Feb 19)**
The MAX10 PLL was refactored from a wizard-generated IP wrapper to a direct `altpll`
instantiation in the top-level RTL. This simplified the build, eliminated generated files
from the repository, and made the SDC timing constraints explicit.

**Dynamic Flash Layout System**
A build-time layout calculator (`build_flash.py`) was written for both boards. It packs
firmware, a ZVGM Table of Contents, game data, and save slots into available flash with no
wasted space — recovering 74 KB of usable capacity (52 KB → 126 KB) over the original static
layout.

**Automated Testing**
`test_uart.py` provides end-to-end automated hardware testing: auto-detects the USB-to-UART
adapter, sends `~reset~` for a deterministic clean start, replays a full game script, and
reports per-command latency statistics. With `--auto-program`, the full flow — build, download
game if missing, program FPGA, run test — takes approximately 11 seconds.

**OSS Release Preparation (Feb 19–22)**
License transitioned from zlib to BSD-3-Clause. Proprietary device headers removed from the
SAM E51 target. GitHub Actions CI workflow added. Documentation rewritten for a public
audience. 71/71 unit tests confirmed passing.

## Exploration Branches (Not Merged)

Several branches were created to evaluate future directions:

**`multi-instance-refactor`** — Made the C engine compile-time multi-instance safe
(`ZVIBE_MULTI_INSTANCE` gate, thread-local state, heap-allocated context). Also built a
FastAPI + xterm.js web frontend using Python ctypes bindings. This work was the direct
predecessor to the Rust rewrite below; the C web target was explicitly removed from
`rust-zvibe` as superseded.

**`rust-zvibe`** — A full Rust rewrite of the Z-machine engine targeting WebAssembly and
multi-user server-side hosting. Includes an 18-chapter tutorial series covering the C→Rust
migration. The C/RISC-V/embedded path was chosen for the initial public release; Rust targets
multi-user web deployment.

**`explore/ice40up5k`** — Feasibility study for the Lattice iCE40UP5K (5.3K LUTs, 128 KB
SPRAM). Concluded: tight but possible with SERV + minimal peripherals.

**`explore/tang-nano-20k`** — Feasibility study for the Sipeed Tang Nano 20K (20K LUTs,
64 MB SDRAM). Concluded: ample resources; SDRAM controller would be the main new component.

## March 2026 — Restaurant Game Development (Separate Repo)

The Restaurant game was built in parallel with ZVibe firmware work during March
2026, in a dedicated repository. Starting from a first working build on March 9,
it went through major puzzle and narrative revisions before reaching release 3 by
March 28: a 42-point, 15-milestone game playable end-to-end. Three regression
scripts were written alongside the game content, each targeting a specific engine
capability: save/restore round-trip, cold death handling, and daemon-driven
timeout. Light polish and canon fixes continued through June 2026.

The game was built as a purpose-made test vehicle for ZVibe. See
[Restaurant as a ZVibe Test Engine](RESTAURANT_TEST_ENGINE.md) for the full
coverage rationale.

## March–June 2026 — Firmware Optimization and Release Hardening

Significant firmware work between the February production builds and public release:

- Compact opcode dispatch, streamlined ZSCII text decoding, streaming output (eliminated
  512-byte decode buffer truncation bug)
- `@capture`/`{var}` script directives for handling random game elements — Restaurant's
  randomized panel code now works with any seed across all hardware targets
- Consolidated UART test harness: ~1000 lines of duplicated board-specific test code
  replaced with a shared 339-line module; board scripts reduced to ~100-line thin wrappers
- Fixed ZSCII alphabet table placement for MAX10 UFM XIP — `static const` data in flash
  caused corruption due to interleaved instruction/data reads through the UFM controller;
  forced to RAM via section attribute (80 bytes, negligible cost)
- Freestanding C library shims for CI (string.h, stdlib.h, stdio.h, time.h) — bare-metal
  RISC-V toolchain on GitHub Actions lacks newlib headers
- BSD-3-Clause SPDX license headers added to all source files
- Performance improved ~20% over February baseline (Plundered Hearts median: 0.512 s on
  MAX10, 0.421 s on Arty, down from 0.638 s and 0.614 s respectively)
- `feature/webterm` merged: platform-agnostic Z-machine web terminal with WebSocket
  session layer

## June 2026 — Public Release

119 unit tests passing. Z-machine V3 conformance (368/368). Three game walkthroughs
verified on all four targets. Dual simulator support. Automated end-to-end hardware
testing with latency tracking and @capture directive support.

---

## By the Numbers

| Metric | Result |
|--------|--------|
| RAM (initial design) | 160 KB |
| RAM (final) | ~25 KB |
| RAM reduction | 84% |
| FPGA platforms | 2 (Arty S7-50, MAX10 08 Eval) |
| MCU platforms | 1 (SAM E51 Curiosity Nano) |
| FPGA clock speed | 100 MHz (MAX10: +0.946 ns setup slack) |
| SERV CPU size | ~200 LUTs |
| XIP cache speedup | 2.7× median (Arty S7-50) |
| Game capacity (MAX10) | up to 126 KB |
| Unit tests | 119 / 119 passing |
| Z3 conformance tests | 368 / 368 passing |
| Game walkthroughs verified | 3 (Restaurant, Plundered Hearts, Seastalker) × 4 targets |
| Hardware-verified saves | Yes (wear-leveling, Feb 12, 2026) |
| Feature branches merged | 10 |
| Exploration branches | 5 |

