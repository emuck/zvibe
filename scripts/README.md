## CI Scripts

`ci-source-check.sh` runs the source-change regression gate used for this repo:

- `make validate`
- `python3 target/console/tests/test_script.py`
- RISC-V build if `riscv64-unknown-elf-gcc` is installed
- SAME51 build if `xc32-gcc` is installed

This script is capability-aware by design so contributors are not forced to
install embedded toolchains for targets they are not working on.
