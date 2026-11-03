# Security Policy

## Reporting a vulnerability

**Do not open a public issue for security vulnerabilities.**

Report privately via GitHub Security Advisories:
https://github.com/emuck/zvibe/security/advisories/new

Include a description of the issue, steps to reproduce, and affected targets.
Expect an acknowledgment within 48 hours and an initial assessment within
7 days.

## Scope

zVibe is a Z-machine interpreter with no network access and no privilege
elevation. The primary attack surface is **story file parsing** — malformed
`.z3`/`.z4` files supplied as input. Story files are treated as trusted: the
interpreter checks the header, the stack and most addresses and aborts on invalid
opcodes, but a deliberately corrupted story can still make it read outside its
buffers (the dictionary and object-table lookups are not range-checked) or follow a
null pointer. `scripts/fuzz_story.c` finds these; see "Corrupt stories" in
[docs/V4_DESIGN.md](docs/V4_DESIGN.md) for what is fixed and what remains. Do not
run story files from untrusted sources on a device that holds secrets. Range-checking
those lookups is a possible future hardening step.

Save file restoration validates a CRC32 before applying any delta. Corrupted
saves are rejected.

Embedded targets (RISC-V, SAM E51) run without an MMU or memory protection
unit. Physical access to the board is outside the threat model.

## Supported versions

This project does not yet have a versioned release. Security fixes are applied
to the `main` branch.
