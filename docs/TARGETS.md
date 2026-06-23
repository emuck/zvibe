# Targets

Supported platforms and porting guide.

## Target Matrix

| Target | CPU/OS | Toolchain | I/O | Storage | RAM | Status |
|--------|--------|-----------|-----|---------|-----|--------|
| console | x86/ARM Linux, macOS | GCC/Clang | Terminal | Filesystem | Unlimited | Supported |
| windows | x86 Windows | MinGW-w64 | Terminal | Filesystem | Unlimited | Supported |
| same51 | ARM Cortex-M4 | arm-none-eabi-gcc | UART | SmartEEPROM | 256KB | Supported |
| riscv | SERV (RV32I) | riscv64-unknown-elf-gcc | UART | QSPI Flash | 32KB | Supported |

## Target Details

### console

Desktop interpreter with full features.

- **Location:** `target/console/`
- **Build:** `make all`
- **Features:** Status bar, file-based saves, shared library
- **Limitations:** None

### windows

Windows executables cross-compiled from WSL2.

- **Location:** `target/windows/`
- **Build:** `make deploy`
- **Features:** Same as console, Windows file associations
- **Limitations:** Requires WSL2 for building

### same51

SAM E51 Curiosity Nano microcontroller.

- **Location:** `target/same51/`
- **Build:** `make program`
- **Features:** Multi-game menu, SmartEEPROM saves
- **Limitations:** 256KB flash for games, limited save slots
- **Embedded profile:** `ZVIBE_ENABLE_FILE_IO=0`, `ZVIBE_ENABLE_STATUS_LINE=0`, `ZVIBE_ENABLE_SAVE_RESTORE=1`

### riscv

RISC-V SoC on Intel MAX10 08 Eval Board (primary) or Digilent Arty S7-50.

- **Location:** `target/riscv/serv_zvibe/`
- **Build:** See [FPGA.md](FPGA.md)
- **Features:** XIP from flash, delta compression saves; Arty also supports multi-game menu
- **Limitations:** 32KB RAM, bit-serial CPU (~5 MIPS effective)
- **Embedded profile:** `ZVIBE_MINIMAL_FEATURES`, `ZVIBE_ENABLE_FILE_IO=0`, `ZVIBE_ENABLE_STATUS_LINE=0`, `ZVIBE_ENABLE_SAVE_RESTORE=1`

## Adding a New Target

### Requirements

1. **Entry point** - Initialize interpreter, load story, run main loop
2. **I/O callbacks** - Print function, input function
3. **Memory** - Provide RAM buffer for dynamic memory and stack
4. **Story data** - Load or embed story file

### Implementation Checklist

1. Create `target/<name>/` directory
2. Implement main entry point:
   ```c
   #include "zvibe_api.h"

   void print_callback(const char *text, size_t len) { /* ... */ }

   int main() {
       zvibeContext *ctx = zvibe_create(print_callback);
       zvibe_load_story_from_memory(ctx, story_data, story_size);
       for (;;) {
           zvibeResult result = zvibe_run(ctx);

           if (result == ZVIBE_WAIT_FOR_INPUT) {
               char input[256];
               get_user_input(input, sizeof(input));
               zvibe_input(ctx, input);
               continue;
           }

#if ZVIBE_ENABLE_SAVE_RESTORE
           if (result == ZVIBE_SAVE_REQUESTED) {
               size_t save_size = zvibe_get_save_size(ctx);
               static uint8_t save_buffer[MAX_SAVE_SIZE];

               if (save_size <= sizeof(save_buffer) &&
                   zvibe_get_save_data(ctx, save_buffer, save_size) == save_size &&
                   persist_save(save_buffer, save_size)) {
                   zvibe_save_completed(ctx, 1);
               } else {
                   zvibe_save_completed(ctx, 0);
               }
               continue;
           }

           if (result == ZVIBE_RESTORE_REQUESTED) {
               static uint8_t restore_buffer[MAX_SAVE_SIZE];
               size_t save_size = load_save(restore_buffer, sizeof(restore_buffer));

               if (save_size > 0 &&
                   zvibe_restore_data(ctx, restore_buffer, save_size) == ZVIBE_OK) {
                   zvibe_restore_completed(ctx, 1);
               } else {
                   zvibe_restore_completed(ctx, 0);
               }
               continue;
           }

           if (result == ZVIBE_RESTART_REQUESTED) {
               zvibe_restart_completed(ctx);
               continue;
           }
#endif

           break;
       }
       zvibe_destroy(ctx);
   }
   ```
   `MAX_SAVE_SIZE` must be large enough for `zvibe_get_save_size(ctx)`. In practice this
   means the full dynamic-memory footprint plus a small execution-state trailer, so a
   static buffer is usually safer than allocating it on a small MCU stack.
   `persist_save()` and `load_save()` are target-specific helpers. The important part is
   the completion protocol: after `ZVIBE_SAVE_REQUESTED`, the host must call
   `zvibe_save_completed()`, and after `ZVIBE_RESTORE_REQUESTED`, it must call
   `zvibe_restore_data()` followed by `zvibe_restore_completed()`.
3. Create Makefile linking against `src/core/*.c` and `src/api/zvibe_api.c`
4. Set `ZVIBE_MEMORY_BUFFER_SIZE` appropriately for available RAM
5. Set explicit feature flags for the target build profile
6. Add target documentation to `target/<name>/README.md`

### Conditional Compilation

Preferred embedded profile:
- `ZVIBE_FREESTANDING=1` uses a freestanding fatal-error path instead of `abort()`
- `ZVIBE_ENABLE_FILE_IO=0` disables `zvibe_load_story()` and host-side filesystem helpers
- `ZVIBE_ENABLE_STATUS_LINE=0` compiles out status callback support
- `ZVIBE_ENABLE_SAVE_RESTORE=1` keeps explicit save/restore request handling when the target implements persistence

`ZVIBE_MINIMAL_FEATURES` remains available as a convenience default for deeply embedded builds, but new targets should prefer explicit feature flags so the build contract is obvious from the makefile.

## Third-Party Dependencies

### SERV (RISC-V target only)

The RISC-V target uses the SERV bit-serial CPU core as a Git submodule.

- **Repository:** https://github.com/olofk/serv
- **License:** ISC
- **Integration:** Submodule at `target/riscv/serv_zvibe/serv/`

Initialize with (from repo root — see [`serv_zvibe/README.md`](../target/riscv/serv_zvibe/README.md#first-time-setup--serv-submodule)):
```bash
git submodule update --init target/riscv/serv_zvibe/serv
```

For SERV documentation and details, see the upstream repository.

### Microchip SDK (same51 only)

The SAM E51 target includes Microchip device headers in `target/same51/packs/`.

- CMSIS headers
- ATSAME51J20A device family pack

These are required for building the SAM E51 target.
