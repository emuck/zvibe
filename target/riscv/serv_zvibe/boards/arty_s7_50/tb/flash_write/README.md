# Flash Write Integration Test

This testbench verifies the complete integration of the flash write controller into the `serv_zvibe` SoC.

## Purpose

Tests that:
1. XIP reads work normally (CPU can execute from flash)
2. Write operations don't cause resets or conflicts
3. QSPI mux arbitration works correctly
4. XIP idle detection prevents conflicts

## Running the Test

```bash
cd target/riscv/serv_zvibe/common/tb/soc/flash_write
make run
```

## What It Tests

### Test 1: XIP Read Test
- Waits for flash startup to complete
- Monitors QSPI activity (CS# toggling indicates XIP reads)
- Verifies no resets occur

### Test 2: Write Operation - No Reset Test
- Monitors system during potential write operations
- Verifies no unexpected resets occur
- This is the critical test for the integration issue

### Test 3: QSPI Mux Arbitration Test
- Verifies QSPI signals are valid (not undefined)
- Checks that mux is working correctly

### Test 4: XIP Idle Detection Test
- Verifies that write controller waits for XIP idle
- Prevents conflicts between XIP and write controllers

## Expected Output

```
================================================================================
 Servant ZVibe Flash Write Integration Test
================================================================================
Testing full SoC integration with flash write support

[TEST 1] XIP Read Test
  Flash ready after XXXX cycles (XX CS# toggles)
  QSPI activity count: XX
  ✓ PASS

[TEST 2] Write Operation - No Reset Test
  Monitoring system during potential write operations...
  No resets detected during monitoring period
  ✓ PASS

[TEST 3] QSPI Mux Arbitration Test
  QSPI signals are valid
  ✓ PASS

[TEST 4] XIP Idle Detection Test
  ✓ PASS

================================================================================
 Test Summary
================================================================================
Total:  4 tests
Passed: 4 tests
Failed: 0 tests
Resets detected: 0
QSPI activity count: XX
Max QSPI idle: XX cycles

✓ ALL TESTS PASSED!
================================================================================
```

## Debugging

If tests fail:

1. **Check for resets**: The test monitors for unexpected resets. If resets are detected, check:
   - XIP idle detection logic
   - QSPI mux arbitration
   - Write controller state machine

2. **View waveform**: 
   ```bash
   make wave
   ```
   Look for:
   - QSPI CS# activity
   - Write controller `o_active` signal
   - XIP controller stall signals
   - Any unexpected resets

3. **Check integration**: Verify that:
   - Flash write registers are accessible at 0x81000000
   - QSPI mux is instantiated correctly
   - Write controller is connected to QSPI mux
   - XIP idle signal is connected correctly

## Files

- `serv_zvibe_flash_write_tb.sv` - Main testbench
- `Makefile` - Build and run scripts

## Integration Checklist

- [x] Flash write register Wishbone interface at 0x81000000
- [x] Write controller instantiation
- [x] QSPI mux arbitration
- [x] XIP idle detection
- [x] Memory mux routing for 0x81000000
- [ ] Hardware validation (after simulation passes)
