# Architecture

System design and implementation details.

## Overview

ZVibe is a Z-machine version 3 interpreter optimized for embedded systems. The core interpreter is platform-independent C code. Target-specific code handles I/O, storage, and memory allocation.

```
┌─────────────────────────────────────────────────────┐
│                    Application                      │
│              (target/*/main.c)                      │
├─────────────────────────────────────────────────────┤
│                    ZVibe API                        │
│              (src/api/zvibe_api.c)                  │
├─────────────────────────────────────────────────────┤
│                  Core Interpreter                   │
│              (src/core/zvibe_*.c)                   │
├─────────────────────────────────────────────────────┤
│                Memory Abstraction                   │
│            (src/core/zvibe_memory.c)                │
└─────────────────────────────────────────────────────┘
```

## Modules

### Core Interpreter (`src/core/`)

| File | Responsibility |
|------|----------------|
| `zvibe_core.c` | Main loop, PC management, opcode dispatch |
| `zvibe_memory.c` | Split memory abstraction |
| `zvibe_object.c` | Object tree operations |
| `zvibe_ops_base.c` | Control flow opcodes (call, return, branch) |
| `zvibe_ops_math.c` | Arithmetic and logical opcodes |
| `zvibe_ops_mem.c` | Memory access opcodes |
| `zvibe_ops_text.c` | Text output and dictionary opcodes |

### API Layer (`src/api/`)

| File | Responsibility |
|------|----------------|
| `zvibe_api.c` | Context management, public interface, callbacks |

### Headers (`include/`)

| File | Contents |
|------|----------|
| `zvibe.h` | Core types, constants, internal structures |
| `zvibe_api.h` | Public API declarations |
| `zvibe_memory.h` | Memory abstraction interface |

## Memory Model

### Split Memory Architecture

Z-machine story files contain two regions:

1. **Dynamic memory** (0 to `staticmem_addr`): Variables, object states, game flags. Must be writable.
2. **Static memory** (`staticmem_addr` to end): Text, code, dictionary. Read-only during execution.

ZVibe keeps only dynamic memory in RAM. Static memory is accessed directly from the source (flash, ROM, or memory-mapped file).

```
Story file layout:
┌──────────────────────────────────────┐
│  Dynamic Memory (0 - staticmem_addr) │  → Copied to RAM
├──────────────────────────────────────┤
│  Static Memory (staticmem_addr - end)│  → Accessed in place
└──────────────────────────────────────┘
```

### Shared Buffer

Desktop and embedded targets use a single buffer for both dynamic memory and stack:

```
Shared buffer layout:
┌────────────────────────────────────────────┐
│  Dynamic Memory         │      Stack       │
│  (0 to staticmem_addr)  │  (grows upward)  │
└────────────────────────────────────────────┘
         ↑                         ↑
    Buffer start              Buffer end
```

Buffer size is controlled by `ZVIBE_MEMORY_BUFFER_SIZE` (default 16KB).

### Memory Access API

All memory access goes through the abstraction layer:

```c
zmem_read_byte(state, addr, &value);   // Read byte
zmem_write_byte(state, addr, value);   // Write byte (dynamic only)
zmem_read_word(state, addr, &value);   // Read big-endian word
zmem_get_ptr(state, addr, writing);    // Get direct pointer
```

The abstraction routes reads to RAM or flash based on address, and enforces write protection on static memory.

## I/O Abstraction

Targets provide callbacks for I/O operations:

```c
typedef void (*zvibeOutputCallback)(const char *text);
typedef void (*zvibeInputCallback)(char *buffer, size_t max_len);
typedef void (*zvibeStatusCallback)(const char *location, int score, int turns);
typedef int (*zvibeSaveCallback)(void *data, size_t size, int slot);
typedef int (*zvibeRestoreCallback)(void *data, size_t *size, int slot);
```

The interpreter calls these at appropriate points during execution.

## Error Handling

### Fatal Errors

Fatal errors call `G->die()` which terminates execution immediately.
Hosted builds may print diagnostics first; freestanding builds halt.

The application should check return values and handle errors appropriately.

### Error Codes

```c
ZVIBE_OK              // Success
ZVIBE_ERROR           // Generic error
ZVIBE_WAIT_FOR_INPUT  // Interpreter waiting for input
ZVIBE_GAME_FINISHED   // Game ended normally
ZVIBE_SAVE_REQUESTED  // Save requested (if enabled)
ZVIBE_RESTORE_REQUESTED // Restore requested (if enabled)
ZVIBE_RESTART_REQUESTED // Restart requested (if enabled)
```

### Memory Errors

```c
ZMEM_SUCCESS              // Success
ZMEM_ERROR_OUT_OF_BOUNDS  // Address outside valid range
ZMEM_ERROR_WRITE_PROTECTED // Write to static memory attempted
ZMEM_ERROR_NULL_PTR       // NULL pointer argument
ZMEM_ERROR_BAD_SIZE       // Invalid size parameter
```

## Platform Hooks

### Conditional Compilation

| Macro | Effect |
|-------|--------|
| `ZVIBE_FREESTANDING` | Selects freestanding fatal-error handling instead of hosted `abort()` |
| `ZVIBE_ENABLE_FILE_IO` | Enables/disables `zvibe_load_story()` filesystem support |
| `ZVIBE_ENABLE_STATUS_LINE` | Enables/disables status callback support |
| `ZVIBE_ENABLE_SAVE_RESTORE` | Enables/disables explicit save/restore request APIs |
| `ZVIBE_MINIMAL_FEATURES` | Legacy convenience default that sets file I/O, status line, and save/restore defaults to off |
| `ZVIBE_MEMORY_BUFFER_SIZE` | Sets shared buffer size (default 16KB) |

### Target Implementation Requirements

Each target must provide:
1. Entry point that creates context and runs main loop
2. Output callback for text display
3. Input collection and explicit `zvibe_input()` handoff when `ZVIBE_WAIT_FOR_INPUT` is returned
4. Memory buffer (or use static default)

Optional:
- Status callback for status bar updates
- Save/restore request handlers for persistence

Embedded targets in this tree now use an explicit profile instead of relying on implicit defaults:
- `ZVIBE_FREESTANDING=1`
- `ZVIBE_ENABLE_FILE_IO=0`
- `ZVIBE_ENABLE_STATUS_LINE=0`
- `ZVIBE_ENABLE_SAVE_RESTORE=1`

## Save Format

The save/restore API serializes the interpreter state as one native-endian
binary blob:

1. Dynamic memory bytes (`ram_size` bytes)
2. Program counter (`uint32_t`)
3. Stack depth in words (`uint32_t`)
4. Stack words (`stack_depth * sizeof(zWord)`)
5. Base pointer (`zWord`)

This format is intentionally simple for embedded hosts, but it is not a
cross-platform interchange format. Save data must come from the same story
file and from a compatible build ABI with the same endianness and `zWord`
layout.

## Determinism

The interpreter is deterministic given identical:
- Story file contents
- Input sequence
- Random seed (if applicable)

This enables:
- Regression testing with recorded input
- Reproducible game sessions
- Comparison between implementations

## RISC-V SoC Architecture

The FPGA implementation is a distinct layer below this software stack. It is documented separately:

- **[serv_zvibe/docs/architecture.md](../target/riscv/serv_zvibe/docs/architecture.md)** — Block diagram, memory maps for both boards, Wishbone interconnect, XIP boot sequence, clock/reset strategy, full peripheral register map.

Key points for software developers:

- The CPU (SERV, bit-serial RV32I) executes instructions directly from flash at `0x80000100` (MAX10) or `0x80100100` (Arty). No bootloader copy occurs.
- RAM (`0x00000000`) holds only dynamic Z-machine memory, stack, and `.ramfunc` sections. The Z-machine static memory (read-only text, code, dictionary) is accessed in-place from flash.
- Peripherals sit at `0x40000000`: UART (0x40000000), Timer (0x40000010), GPIO (0x40000020).

## Code Size

Typical code size by component:

| Component | Size (approximate) |
|-----------|-------------------|
| Core interpreter code | 15-20 KB flash |
| Memory abstraction | 2 KB flash |
| API layer | 2 KB flash |
| Total code | 20-25 KB flash |

RAM usage depends on `ZVIBE_MEMORY_BUFFER_SIZE` plus ~3KB for interpreter state.
