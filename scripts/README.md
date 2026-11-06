## CI Scripts

`ci-source-check.sh` runs the source-change regression gate used for this repo:

- `make validate`
- `make validate-v4` and the webterm tests (V4 enabled; includes the Czech V4 gate and the pty test of the terminal player)
- `python3 target/console/tests/test_script.py`
- RISC-V build if `riscv64-unknown-elf-gcc` is installed
- SAME51 build, and the V4 firmware build, if `xc32-gcc` is installed
- Windows build, V3 and with V4 compiled in, if `x86_64-w64-mingw32-gcc` is installed

This script is capability-aware by design so contributors are not forced to
install embedded toolchains for targets they are not working on.
