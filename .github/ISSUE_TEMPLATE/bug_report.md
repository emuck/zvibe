---
name: Bug report
about: Report a problem with ZVibe
title: ''
labels: bug
assignees: ''
---

## Description

Provide a clear description of the bug.

## To Reproduce

Steps to reproduce the behavior:

1. Build command used: `...`
2. Run command: `...`
3. Input provided: `...`
4. Error observed: `...`

## Expected Behavior

Describe what you expected to happen.

## Actual Behavior

Describe what actually happened.

## Environment

**Operating System:**
- OS: [e.g., Ubuntu 22.04, macOS 14, Windows 11 WSL2]
- Architecture: [e.g., x86_64, aarch64]

**Target Platform:**
- Target: [console, windows, same51, riscv]
- Board (if applicable): [e.g., Arty S7-50, SAM E51 Curiosity Nano]

**Version:**
- ZVibe version: [e.g., 1.0.0, commit hash]
- Story file: [e.g., czech.z3, restaurant.z3, or a downloaded game]

**Toolchain:**
- Compiler: [e.g., gcc 11.4, arm-none-eabi-gcc 10.3]
- Vivado version (if RISC-V): [e.g., 2025.1, 2024.2]
- Python version (if applicable): [e.g., 3.10.12]

## Logs

Attach relevant output:

**Build output:**
```
[Paste build errors or warnings here]
```

**Runtime output:**
```
[Paste console output or error messages here]
```

**Validation output (if applicable):**
```
[Paste make validate output here]
```

## Additional Context

Add any other relevant information:
- Does this work in a previous version?
- Does this affect all targets or just one?
- Have you modified any files?
- Related issues or pull requests?

## Checklist

- [ ] Searched existing issues for duplicates
- [ ] Read TROUBLESHOOTING.md
- [ ] Tested on clean build (`make clean && make all`)
- [ ] Included complete environment information
- [ ] Attached relevant logs
