# MAX10 Bootloader FSM Testbench

This testbench verifies that the bootloader FSM correctly copies boot stub code from UFM to RAM.

## Test Overview

The testbench:
1. Initializes UFM with a test pattern (boot stub code)
2. Resets the bootloader FSM
3. FSM copies boot stub from UFM to RAM
4. Verifies RAM contains correct boot stub code
5. Verifies CPU reset is released after copy completes

## Running the Test

```bash
cd target/riscv/servant_zvibe/common/tb/bootloader_fsm
make run
```

## Test Pattern

The UFM is initialized with a simple pattern:
- Word 0: 0x00000000
- Word 1: 0x00000001
- Word 2: 0x00000002
- ...
- Word 255: 0x000000FF

This pattern makes it easy to verify that the bootloader FSM copied the data correctly.

## Expected Output

```
========================================
MAX10 Bootloader FSM Testbench
========================================

[TB] Initialized UFM with boot stub pattern (256 words) at address 0x04000
[TB] Releasing reset...
[TB] State: INIT
[TB] State: READ_UFM (addr=0x04000)
[TB] State: WAIT_DATA
[TB] State: WRITE_RAM (addr=0x0000, data=0x00000000)
[TB] State: NEXT_WORD (count=0)
...
[TB] Bootloader complete! CPU reset released.

[TB] Verifying RAM contents...
[TB] OK: RAM[0] = 0x00000000
[TB] OK: RAM[1] = 0x00000001
...

========================================
Test Summary
========================================
Words copied: 256
Errors: 0
TEST PASSED!
========================================
```

## Files

- `max10_bootloader_fsm_tb.sv` - Testbench
- `Makefile` - Build system
- `README.md` - This file

## Dependencies

- Verilator (for simulation)
- UFM model: `../models/max10_ufm_model.v`
- RAM model: `../models/max10_ram_model.v`
- Bootloader FSM: `../../../rtl/max10_bootloader_fsm.v`
