# Arty S7-50 version 4 board test: session prompt

For a session on the Ubuntu machine (sarastro) with the Arty attached and Vivado installed.
Point the session at this file: "read target/riscv/serv_zvibe/boards/arty_s7_50/tests/V4_BOARD_TEST_PROMPT.md
and follow it".

---

You are on an Ubuntu 24.04 machine with an Arty S7-50 board attached over USB (JTAG + UART via the FT2232H) and Vivado installed. Your job is to finish testing the RISC-V Z-machine VERSION 4 firmware for the Arty, which was written and host-tested on a Mac with no FPGA tools. Nothing here has ever run on the board. Be skeptical, measure, and report what you actually observed. If a test fails, say so with the output; do not talk it into a pass.

## Repo
- git@github.com:emuck/zvibe.git, branch `dev/riscv-v4` (`git fetch && git checkout dev/riscv-v4 && git pull`). Do NOT open a PR; do not push to main.
- Commits: plain messages, no attribution lines or mention of the assistant in commits, notes or docs.
- Read first: target/riscv/serv_zvibe/docs/build.md ("Version 4 stories"), docs/V4_DESIGN.md item 9, docs/V4_MEMORY.md (Arty section), target/riscv/serv_zvibe/common/sw/zvibe_riscv_v4.{h,c}, boards/arty_s7_50/sw/Makefile, boards/arty_s7_50/fpga/Makefile.

## What was built (all unverified on hardware)
- `make flash-test V4=1 STORY=<file.z4>` (in boards/arty_s7_50/sw) builds the V4 firmware sized for the story; `make ram-kb V4=1 STORY=...` says the SoC RAM it needs (32/64/128 KB); `make ram-budget` shows the breakdown. V3 builds must be unchanged.
- The SoC RAM is now a bitstream parameter (RAM_KB in rtl/servant_zvibe_arty_s7_50.sv, set from ARTY_RAM_KB by fpga/build.tcl and fpga/Makefile). Default 32 = the old behaviour. These RTL/TCL/Makefile changes have NEVER been run in Vivado.
- V4 player (common/sw/zvibe_riscv_v4.c): terminal-size probe, line input, read_char keys, saves packed against the story (identity + CRC-32) in the flash save slots (64 KB sectors).
- Host test (already passing on the Mac, 191 checks): `make -C target/riscv/serv_zvibe/common/sw/tests test`. Run it here first as a sanity check.

## Known unknowns to resolve (the point of this session)
1. RV_SPIN_PER_MS (zvibe_riscv_v4.h, default 40) is a guess for the bit-serial SERV core. It sets the wait for an escape sequence's tail (arrow keys) and the terminal-size reply (500 ms worth). Measure real iterations/ms (e.g. a toggling GPIO LED or the SoC timer at 0x40000010 against a known time), set a calibrated value, and verify: arrow keys arrive as 129-132, a lone Escape is 27, and the size probe reports the real terminal size.
2. Does the V4 screen layer render correctly over the real UART at 115200 (status bar reverse video, split window, scrolling)?
3. Real QSPI saves: does save/restore survive a power cycle? Does a save from another story/release get refused? (flash_save.c does not verify its own CRC-16 on read; the V4 save carries its own CRC-32.)
4. Stack depth: the budget assumes 4 KB of stack, the linker only enforces 2 KB. Measure the high-water mark (fill the stack region with a pattern at boot in a throwaway build, play, read it back) with the biggest story you test.
5. A bitstream with RAM_KB > 32 (Vivado build, timing, utilization). Check timing_summary after the build.

## Steps (stop and report at each gate; ask before programming the bitstream region at flash offset 0x000000 if anything looks off)
0. Environment: `ls /dev/ttyUSB*`, `openFPGALoader --detect -b arty_s7_50`, `vivado -version`, `riscv64-unknown-elf-gcc --version`, Python venv with pyserial and pyte (the SAM E51 tests in target/same51/tests/test_v4_*.py are good models for a pyserial+pyte driver; there is no Arty V4 test script yet, write one under boards/arty_s7_50/tests/).
1. Baseline, V3 unchanged: build the default bitstream (`cd boards/arty_s7_50/fpga && make build`; this also proves the RAM_KB=32 default and build.tcl edit work), `make program-complete`, then `python3 ../tests/test_uart.py` (Restaurant). It must pass as before. If it does not, stop: the RTL change broke V3.
2. V4 on the EXISTING 32 KB design: `cd ../sw && make flash-test V4=1 GAME=czech`, then `cd ../fpga && make program-flash` (flash image only). Terminal: 115200 8N1, at least 56 columns x 10 rows (use a real terminal emulator or pyte at 80x24). Run the Czech V4 gate (games/catalog/czech.z4); the expected result is the same as the console: 386 tests, 367 passed, 0 failed, 19 print tests (see target/console/tests/test_czech_v4.sh).
3. Small V4 test stories, also 32 KB: target/console/tests/data/ v4keys.z4 (prints key codes), v4lines.z4 (line input, 39-char limit), v4screen.z4 (split screen; row 1 is a reverse-video bar), v4save.z4 (keys i p s r q; restore resumes with "save result 2"). Flash each with `make flash-test V4=1 STORY=/abs/path.z4`. Verify screen, keys, line editing, and save -> power cycle -> restore. Also: a save from v4save refused by v4bigsave (identity), and a deliberately corrupted save refused.
4. Calibrate RV_SPIN_PER_MS (unknown 1), then re-test arrow/Escape keys and the size probe (try resizing the terminal and restarting the game).
5. Big stories need a bigger bitstream: *Bureaucracy* (64 KB). The story file is bureaucracy-r116-s870602.z4; the user will copy it to this machine (ask where it is if you cannot find it). `make ram-budget V4=1 STORY=<PATH>` then `cd fpga && make program-complete V4=1 STORY=<PATH>` (builds a 64 KB bitstream; check build/timing_summary.txt and utilization). Play it: status line, the form screens, save/restore across a power cycle, a long play session. Measure stack high-water (unknown 4).
6. Optional if time allows: 128 KB (Trinity or A Mind Forever Voyaging), same checks.
7. Write boards/arty_s7_50/tests/test_v4_*.py scripts for what you verified, so it is repeatable.

## Reporting
Commit fixes and test scripts to dev/riscv-v4 (plain messages). Update docs/V4_DESIGN.md item 9 and docs/V4_MEMORY.md with what is now VERIFIED versus still not (be exact: which stories, which RAM size, calibrated RV_SPIN_PER_MS value, measured stack, Vivado timing/utilization for 64/128 KB). Finish with a short list: passed, failed, fixed, still unverified. Do not push to main and do not open a PR; push the branch only when told to.

## Cautions
- Programming offset 0x000000 replaces the bitstream stored in QSPI; offset 0x100000 is the user region (firmware + games + saves). `program-flash` touches only 0x100000.
- Do not change system or security settings, and do not install anything outside a venv without asking.
- Do not bypass failures: if a step breaks, report the exact command and output.
