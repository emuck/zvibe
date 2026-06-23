# Contributing to zVibe

## Getting started

```bash
git clone https://github.com/emuck/zvibe.git
cd zvibe
# RISC-V target only — initialize SERV submodule
git submodule update --init target/riscv/serv_zvibe/serv
```

Install build prerequisites for the console target:

```bash
# Ubuntu / Debian
sudo apt install build-essential

# macOS — Xcode command-line tools provide cc and make
xcode-select --install
```

Build and verify:

```bash
cd target/console
make clean && make
cd tests && make test-unit test-czech
```

For embedded targets see [docs/BUILDING.md](docs/BUILDING.md) and
[docs/INSTALL.md](docs/INSTALL.md).

## Before submitting a PR

- All unit tests pass: `cd target/console/tests && make test-unit`
- Czech conformance tests pass: `make test-czech`
- If toolchains are available, run `make validate` from the repo root
- No debug output left in code
- Public API changes include a Doxygen header update

CI runs on Ubuntu and macOS and must be green.

## Code style

**C (src/, target/\*/src/)**
- C99, 4-space indent, no tabs
- 100-character line limit
- `snake_case` for functions and variables; `CamelCase_t` for typedefs;
  `UPPER_SNAKE_CASE` for macros
- `static` for file-scope helpers
- `/* */` for block comments; `//` for inline

**Verilog / SystemVerilog (target/riscv/)**
- 4-space indent, lowercase signal and module names, `UPPER_SNAKE_CASE`
  for parameters
- `// SPDX-License-Identifier:` header on every new file

**Python (games/, boards/\*/)**
- PEP 8, 100-character line limit
- `# Copyright (c) <year> Martin R. Raumann` + SPDX header on new files

## Commit messages

```
component: short summary in imperative mood (≤72 chars)

Optional body. Explain the why, not the what. Wrap at 72 chars.
Reference issues with "Fixes #N" or "See #N".
```

Component prefixes: `src`, `riscv`, `same51`, `console`, `docs`,
`build`, `tests`, `ci`.

Examples:
```
src: add bounds check in zmem_read_byte for addresses near staticmem
riscv: fix UART prescaler at 100 MHz clock
docs: document flash layout header format
```

## Scope

zVibe interprets Z-machine version 3 only. Contributions that expand
scope to v4+ or add features incompatible with the 32KB RAM constraint
will not be accepted.

## License

By submitting a pull request you agree your contribution will be licensed
under the BSD-3-Clause license (see [LICENSE](LICENSE)).
