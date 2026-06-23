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
`.z3` files supplied as input. All memory accesses are bounds-checked against
the story file size; invalid opcodes are detected and abort execution.

Save file restoration validates a CRC32 before applying any delta. Corrupted
saves are rejected.

Embedded targets (RISC-V, SAM E51) run without an MMU or memory protection
unit. Physical access to the board is outside the threat model.

## Supported versions

This project does not yet have a versioned release. Security fixes are applied
to the `main` branch.
