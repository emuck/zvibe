# V4 Memory Analysis by Target

How a V4 story would fit on each current target, where it does not, and what
would have to change. Companion to [V4_DESIGN.md](V4_DESIGN.md). Check any
story against these budgets with:

```
scripts/zinfo.py story.z4 [more stories]
```

## What V4 changes for memory

| | V3 | V4 | Effect |
|---|---|---|---|
| Story size limit | 128 KB | 256 KB | Storage: flash/XIP (read in place) |
| Dynamic + static limit | 64 KB | 64 KB | Same ceiling; real V4 games sit higher than typical V3 |
| Dynamic memory (RAM) | 8 KB for Restaurant | unknown, see below | The binding constraint on the 32 KB targets |
| Object entry | 9 B | 14 B | Dynamic memory grows with object count |
| Operands per instruction | 4 | 8 | +16 B of state |
| Screen state | none | about 184 B (`zscreen`) | +0.2 KB RAM |
| Z-machine stack | 512 words | assumed 1024 words (unverified need) | +1 KB RAM (see below) |
| Code | 27 KB | +3-5 KB estimated | Flash only |

The story's read-only part (static memory, code, text, dictionary) is read in
place from flash and costs no RAM. Only **dynamic memory** is copied into RAM.

### How the interpreter uses RAM today

- The shared buffer (`ZVIBE_MEMORY_BUFFER_SIZE`, default 16 KB) holds **only
  dynamic memory** (`ram_size = staticmem_addr`, `zvibe_memory.c:101`).
- The Z-machine stack is **not** in that buffer. It is `stack_mem[512]` inside
  `zState` (`zvibe.h:223`). Several comments and `ARCHITECTURE.md` say the
  buffer holds the stack too; that is inaccurate.
- The load check at `zvibe_core.c:475` still reserves 1 KB of the buffer for a
  stack that is not there, so it rejects a story whose dynamic memory is
  between 15 and 16 KB for no reason. Worth fixing when the buffer size is made
  per-target.
- Total RAM for a story is therefore `dynamic + interpreter state`, where the
  state is about 3.7 KB on RISC-V (measured: `.bss` 20,100 B minus the 16 KB
  buffer).

## Story sizes: what is known and what is not

- Version 4 stories can be up to 256 KB, and the Digital Antiquarian describes
  *A Mind Forever Voyaging* and *Trinity* as both pegged to that limit
  ([source](https://www.filfre.net/2019/11/new-tricks-for-an-old-z-machine-part-3-a-renaissance-is-nigh/)).
  Treat those two as roughly 256 KB.
- The canonical V4 set is *AMFV*, *Bureaucracy*, *Nord and Bert*, and *Trinity*
  ([standards appendix](https://inform-fiction.org/zmachine/standards/z1point1/appf.html)).
- **No source found for the dynamic-memory size of any of them**, and no
  verified file size for *Bureaucracy* or *Nord and Bert*. The standard only
  bounds dynamic memory at 64 KB. Do not size hardware from guesses: run
  `scripts/zinfo.py` on the real story files (the dynamic size is the
  big-endian word at header offset 0x0E).
- Test material that is available: the Czech conformance suite builds as V4
  (`czech.z4`, 11 KB, dynamic 2.3 KB; frotz runs 386 tests, 0 failed) and is
  the right first gate for the core.

## Per-target verdict

Budgets are from the linker scripts, `FPGA.md`, `registry.json`, the save
configs and the measured firmware sizes in `CODE_SIZE_ANALYSIS.md`.

| Target | RAM | Story storage | Saves | V4 outlook |
|---|---|---|---|---|
| console, windows | host memory; buffer is a build-time constant (default 16 KB) | filesystem | files | Fine. Raise the default buffer to 64 KB for V4 builds. |
| SAME51 | 256 KB SRAM, 18.3 KB `.bss` today | 1 MB flash, 786,432 B available for games | SmartEEPROM 16 KB | Fits any V4 story. One real issue: saves (below). |
| Arty S7-50 | 32 KB BRAM today, 12.1 KB free after `.bss` | 16 MB QSPI, 8 MB for games | 4 slots of 64 KB | Flash fits everything. RAM is the limit unless the BRAM is enlarged. |
| MAX10 08 Eval | 32 KB today, 31 of 46 M9K blocks used | **172 KB UFM total**, shared with firmware and saves | 2 KB slots | Only small V4 stories fit. |

### SAME51

- **RAM:** no concern. A 64 KB buffer costs 48 KB of 256 KB.
- **Flash:** 786,432 B available holds three maximum-size V4 stories.
- **Saves, single-game firmware:** `zvibe_same51.c:37` uses a 16 KB on-stack
  buffer for a **full** save (dynamic memory plus stack). A V4 story with
  dynamic memory above about 15 KB cannot save. The multi-game build already
  uses delta compression (`delta_encode`); the single-game path should be moved
  to it.

### Arty S7-50 (RISC-V)

- **Flash:** no concern.
- **RAM:** usable RAM is 32,256 B (`xip_link.ld`: 32 KB minus 512). Today
  `.bss` is 20,100 B and `.data` 16 B, leaving 12.1 KB for the C stack and any
  growth. The C stack depth has not been measured. If 4 KB is reserved for it,
  the budget for dynamic memory plus interpreter state is 28,144 B, which is
  about 23 KB of dynamic memory for V4 (state grows by about 1.3 KB with the
  doubled stack and screen state).
- **The fix if a story needs more:** enlarge the BRAM in the SoC. The S7-50 has
  about 337 KB of block RAM and the SoC uses 8.5 tiles, so this is a bitstream
  parameter change and rebuild, not a redesign. Not checked in this analysis.
- **Saves:** 64 KB slots, no concern.

### MAX10 08 Eval (RISC-V)

- **Flash is the hard limit.** The UFM is 176,128 B in total
  (`0x80000000-0x8002AFFF`) and holds the ZVIF header, firmware (about 27 KB),
  the game, and all save slots. With four 2 KB slots a game can be about
  137 KiB (140,000 B). *AMFV* and *Trinity* (about 256 KB) cannot fit at all.
  Whether *Bureaucracy* and *Nord and Bert* fit is unknown until their sizes
  are checked.
- **RAM:** the same 32 KB as Arty. It could grow by roughly 15 M9K blocks
  (about 15 KB) before the device is full, to a ceiling near 47 KB, minus any
  cache or FIFO use. Not checked against the Quartus report.
- **Saves:** fixed 2 KB slots (`flash_save_config.h`). A delta save is a header,
  the used stack, and the changed dynamic bytes, and it must fit in 2,036 B.
  Restaurant fits. A V4 story that changes more state between start and save
  may not.
- **Outlook:** a limited V4 target: small stories such as the madlibs piece and
  `czech.z4`, not the Infocom V4 titles.

## Cross-cutting issues

1. **Stack size.** The Z-machine stack is a fixed 512 words (`Z3_STACK_SIZE`).
   A larger stack (1024 words was assumed for the estimates above) may be
   needed by deeply recursive V4 games, but this was not verified against the
   standard or a reference interpreter, and whether the Infocom V4 titles need
   more than 512 words is unmeasured. Make it a per-build constant (`ZVIBE_STACK_WORDS`) so the 32 KB
   targets can stay at 512 until a real story proves otherwise.
2. **Per-target buffer size.** `ZVIBE_MEMORY_BUFFER_SIZE` is a single 16 KB
   default. It must become an explicit per-target setting.
3. **Delta saves everywhere.** Full saves cap dynamic memory at the save
   buffer size. Use the delta path on SAME51 single-game and check delta sizes
   against the 2 KB MAX10 slot.
4. **Speed, not size.** The SERV core runs about 5 MIPS. *AMFV* runs a
   heavy simulation; an Arty latency of 0.5 s median on V3 (see
   `CODE_SIZE_ANALYSIS.md`) may not carry over. This was not measured and is a
   separate risk from memory.

## What to measure to replace the estimates

| Unknown | How |
|---|---|
| Dynamic and file size of each real V4 story | `scripts/zinfo.py` on the files |
| C stack high-water on RISC-V | fill the stack region with a pattern at boot and scan after a long session |
| Z-machine stack depth V4 games reach | log the peak `stack_ptr` in a build with diagnostics enabled |
| V4 code and state growth | build the V4 core for SAME51 and RISC-V and compare `size` output |
| MAX10 RAM headroom | read the Quartus fitter report for free M9K blocks |
| Delta save size under V4 play | log `delta_encode` output sizes through a long session |
