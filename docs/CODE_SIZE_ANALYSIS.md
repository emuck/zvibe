# ZVibe Performance Analysis

## Binary Size

| Binary | Size | Notes |
|--------|------|-------|
| `zvibe_minimal` | 31,488 bytes | Host file I/O enabled; status line and save/restore disabled |
| `zvibe_console` | 45,456 bytes | Full-featured desktop build |

The split memory architecture added ~1.2 KB (+2%) to the console binary despite a 19% source code increase — the abstraction layer compiles away almost entirely at `-O2`.

## RAM Usage

ZVibe loads only dynamic game state into RAM; read-only game data is accessed directly from flash/UFM.

| Game type | Traditional interpreter | ZVibe |
|-----------|------------------------|-------|
| Small (Czech, ~10 KB) | ~160 KB | ~12 KB |
| Medium (~64 KB story) | ~160 KB | ~15 KB |
| Large (~128 KB story) | ~288 KB | ~18 KB |

Result: 85–95% RAM reduction, which is what makes embedded deployment viable.

Recent embedded cleanup work trimmed interpreter state further:
- compact opcode dispatch tables reduce dispatch RAM from 1024 bytes to 384 bytes on 32-bit targets
- current measured firmware sizes:
  - SAME51: `text 27742 / data 32 / bss 18227`
  - RISC-V common: `text 26752 / data 16 / bss 20100`
  - RISC-V Arty: `text 26752 / data 16 / bss 20100`
  - RISC-V MAX10: `text 27196 / data 16 / bss 20100`

## Hardware Latency — Plundered Hearts (232 commands)

Measured on hardware. All timings include character transmission at 115200 baud.

| Platform | Median | p95 | Max |
|----------|--------|-----|-----|
| Arty S7-50, QSPI uncached (CLK_DIV=1) | 1.481 s | 3.578 s | 8.271 s |
| **Arty S7-50, 4 KB BRAM cache (CLK_DIV=2)** | **0.546 s** | **1.285 s** | **2.900 s** |
| MAX10, on-chip UFM (no cache needed) | 0.625 s | 1.401 s | 3.487 s |

The 4 KB cache gives a 2.7× median speedup on Arty by converting ~130-cycle QSPI misses into 4-cycle hits. Cached Arty and MAX10 are now comparable — MAX10's advantage (zero-latency on-chip flash) is roughly matched by Arty's faster clock.

Raw latency CSVs: `boards/arty_s7_50/tests/` and `boards/max10_08_eval/tests/`.

## Source Code Size

| Component | Lines |
|-----------|-------|
| `zvibe_memory.c` | 380 |
| `zvibe_memory.h` | 230 |
| Core integration changes | 88 |
| **Total added** | **+698 (+19%)** |

## Test Coverage

| Suite | Count | Status |
|-------|-------|--------|
| Czech Z-machine conformance | 368/368 | Pass |
| Memory unit tests | 119/119 | Pass |
| Verilator RTL regression (Tier 1) | 13/13 | Pass |
