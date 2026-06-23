# UFM Model Comparison: Simple vs Intel Actual

## Summary

Our simple UFM model is **significantly simplified** compared to Intel's actual simulation model. The Intel model has complex multi-cycle read operations, multiple state toggles, and timing delays that could affect bootloader FSM behavior.

## Critical Differences

### 1. Read State Machine Complexity

**Simple Model** (3 states):
```
IDLE → READ (1 cycle) → WAIT → IDLE
```

**Intel Model** (8 states):
```
IDLE → ADDR → PULSE_SE → SETUP → DUMMY → READY → FINAL → CLEAR → IDLE
```

### 2. Read Latency

| Model | Latency | Notes |
|-------|---------|-------|
| Simple | 1-2 cycles | Immediate data ready |
| Intel | 5-7+ cycles | Depends on FLASH_READ_CYCLE_MAX_INDEX (default 3) |

### 3. Waitrequest Behavior

**Simple Model**:
```verilog
// Lines 103-106
o_waitrequest <= 1'b1;  // Assert immediately
state <= STATE_READ;

// Line 125
o_waitrequest <= 1'b0;  // Deassert after 1 cycle
```
- Asserted once at start of read
- Deasserted after 1 cycle
- Simple, predictable behavior

**Intel Model**:
```verilog
// Line 338 - Complex waitrequest logic
assign avmm_waitrequest = ~reset_n ||
    ((~is_write_busy && avmm_write) ||
     write_wait_w ||
     (~is_read_busy && avmm_read) ||
     (avmm_read && read_wait_w));
```

**Intel read_wait signal toggling**:
- Line 852: `read_wait <= 1` (IDLE → ADDR)
- Line 869: `read_wait <= 0` (ADDR → PULSE_SE)  ← **Brief deassert!**
- Line 873: `read_wait <= 1` (PULSE_SE → SETUP)
- Line 930: `read_wait <= 0` (CLEAR → IDLE)

**Glitch Prevention**: Intel uses `read_wait_w = (read_wait || read_wait_neg)` to prevent waitrequest glitches during state transitions.

### 4. Readdatavalid Behavior

**Simple Model**:
```verilog
// Lines 126-127
o_readdatavalid <= 1'b1;  // Assert same cycle as data ready
state <= STATE_WAIT;
```
- Asserted for 1 cycle minimum
- Stays asserted until read is deasserted

**Intel Model**:
```verilog
// Lines 1057-1114 - Separate readdatavalid state machine
READ_VALID_IDLE → READ_VALID_PRE_READING → READ_VALID_READING
```
- Separate state machine for readdatavalid
- Tied to `avmm_readdata_ready` signal
- Supports burst reads with per-word valid signals
- Minimum 2 cycles from READY state to readdatavalid assertion

### 5. Burst Support

| Feature | Simple Model | Intel Model |
|---------|-------------|-------------|
| Burst reads | ❌ No | ✅ Yes (incrementing & wrapping) |
| Burstcount | Ignored | Respected (1-4 or 1-2 for ZB8) |
| Sequential addressing | ❌ | ✅ Auto-increment |

### 6. Flash Interface Signals

**Simple Model**: None (behavioral memory array)

**Intel Model**: Full flash interface
- `flash_arclk`, `flash_arshft`, `flash_ardin` (address shift register)
- `flash_drclk`, `flash_drshft`, `flash_drdin` (data shift register)
- `flash_xe_ye`, `flash_se` (chip enable, sector enable)
- `flash_busy`, `flash_osc` (busy status, oscillator)
- `flash_nprogram`, `flash_nerase` (write/erase control)

### 7. Timing Parameters

**Intel Model** has configurable timing:
- `FLASH_READ_CYCLE_MAX_INDEX = 3` (cycles per sequential read)
- `FLASH_SEQ_READ_DATA_COUNT = 2` (32-bit words per read)
- `FLASH_ADDR_ALIGNMENT_BITS = 1` (address alignment)
- `FLASH_RESET_CYCLE_MAX_INDEX = 28` (reset delay)
- `FLASH_BUSY_TIMEOUT_CYCLE_MAX_INDEX = 112` (1200ns timeout)
- `FLASH_ERASE_TIMEOUT_CYCLE_MAX_INDEX = 40603248` (350ms timeout)
- `FLASH_WRITE_TIMEOUT_CYCLE_MAX_INDEX = 35382` (305us timeout)

## Potential Issues for Bootloader FSM

### Issue 1: Waitrequest Glitch Window
Between ADDR and PULSE_SE states, `read_wait` toggles 0 → 1. If bootloader FSM samples waitrequest during this window, it might see a brief deassertion and proceed prematurely.

**Mitigation**: Intel uses `read_wait_w = (read_wait || read_wait_neg)` to mask the glitch.

### Issue 2: Increased Read Latency
Bootloader FSM might timeout if expecting 1-2 cycle reads but getting 5-7+ cycle reads from real hardware.

**Check**: Review bootloader FSM timeout values.

### Issue 3: Readdatavalid Timing
Intel model asserts readdatavalid 2 cycles after entering READY state, not immediately with data. Bootloader FSM must wait for readdatavalid, not just deasserted waitrequest.

**Verification**: Bootloader FSM code shows it waits for `i_ufm_readdatavalid` (correct).

## Intel Model Read Operation Timeline

```
Cycle  State           avmm_read  read_wait  waitrequest  readdatavalid  Notes
─────────────────────────────────────────────────────────────────────────────
  0    IDLE              0          0           0             0          Idle
  1    IDLE→ADDR         1          1           1             0          Read asserted
  2    ADDR              1          0→1         1             0          Address validated
  3    PULSE_SE          1          1           1             0          SE pulse
  4    SETUP             1          1           1             0          Calculate next addr
  5    DUMMY             1          1           1             0          Dummy cycle
  6    READY             1          1           1             0          Data ready internally
  7    READY             1          1           1             1          readdatavalid asserted
  8    FINAL             1          1           1             1          Data valid
  9    CLEAR             1          0           1             0          Cleanup
 10    IDLE              0          0           0             0          Back to idle
```

**Key**: Readdatavalid appears ~7 cycles after read assertion!

## Simple Model Read Operation Timeline

```
Cycle  State      i_read  o_waitrequest  o_readdatavalid  Notes
─────────────────────────────────────────────────────────────────
  0    IDLE         0         0               0           Idle
  1    IDLE→READ    1         1               0           Read asserted
  2    READ         1         0               1           Data ready immediately!
  3    WAIT         1         0               1           Keep valid
  4    IDLE         0         0               0           Back to idle
```

**Key**: Readdatavalid appears ~2 cycles after read assertion (5x faster than Intel!)

## Recommendations

### Option 1: Update Simple Model (Recommended for Simulation)
Add key missing behaviors:
1. Multi-cycle read latency (at least 5 cycles)
2. Proper waitrequest toggling with glitch prevention
3. Delayed readdatavalid (2 cycles after data ready)
4. Support for burstcount=1 (ignore higher values)

### Option 2: Replace with Intel Model (Accurate but Complex)
Use Intel's actual simulation files:
- Requires pulling in multiple dependencies (util modules, synchronizer, etc.)
- More accurate to hardware behavior
- Harder to debug in Verilator

### Option 3: Hybrid Approach
Keep simple model but add:
- Configurable read latency parameter (default 5)
- Pipeline stages to match Intel timing
- Glitch-free waitrequest using dual-rail logic

## Files Examined

1. `common/tb/models/max10_ufm_model.v` (in this repo)
   - Simple behavioral model (148 lines)

2. `<quartus-install>/ip/altera/altera_onchip_flash/simulation/submodules/altera_onchip_flash_avmm_data_controller.v`
   - Intel's Avalon-MM data controller (1270 lines!)

3. `<quartus-install>/ip/altera/altera_onchip_flash/simulation/submodules/altera_onchip_flash.v`
   - Top-level wrapper (324 lines)

4. `<quartus-install>/ip/altera/altera_onchip_flash/simulation/submodules/altera_onchip_flash_util.v`
   - Utility modules for address checking, conversion (271 lines)

## Next Steps

1. **Immediate**: Test bootloader FSM with current simple model to see if it works
2. **If failing**: Update simple model to add 5-cycle read latency
3. **If still failing**: Replace with Intel model (more work but accurate)
4. **Hardware test**: Ultimate validation - does it work on real MAX10?

## Bootloader FSM Analysis

I reviewed `max10_bootloader_fsm.v` to check how it handles UFM reads:

### Read Protocol (lines 106-128)

```verilog
STATE_READ_UFM:
    o_ufm_read <= 1'b1;           // Assert read
    state <= STATE_WAIT_DATA;

STATE_WAIT_DATA:
    o_ufm_read <= 1'b0;           // Deassert after 1 cycle
    if (i_ufm_readdatavalid) begin
        data_buffer <= i_ufm_readdata;  // Latch data
        state <= STATE_WRITE_RAM;
    end
```

### Key Findings

1. **Correct readdatavalid Wait**: FSM correctly waits for `i_ufm_readdatavalid` before latching data (line 118). This means it will work with both 2-cycle and 7-cycle read latencies.

2. **Minor Spec Violation**: FSM deasserts `o_ufm_read` after 1 cycle (line 115) regardless of `i_ufm_waitrequest`. Per Avalon-MM spec, master should keep read asserted until waitrequest deasserts.

   **However**: Both our simple model AND Intel's model latch the address on the first cycle of read assertion:
   - Simple model line 102: `read_addr <= i_address;`
   - Intel model line 854: `flash_seq_read_ardin <= avmm_addr;`

   So both models work around this spec violation.

3. **No Timeout**: FSM has no timeout in WAIT_DATA state. It will wait indefinitely for readdatavalid. This is fine for simulation but could hang hardware if UFM fails.

### Compatibility Assessment

| Scenario | Simple Model | Intel Model | Will it work? |
|----------|-------------|-------------|---------------|
| Bootloader reads boot stub | 2-cycle latency | 7-cycle latency | ✅ Yes (both work) |
| Early read deassertion | Latches addr immediately | Latches addr immediately | ✅ Yes (both work) |
| Timing differences | Fast | Slow | ✅ Yes (FSM waits for valid) |

**Verdict**: UFM model timing differences should NOT prevent bootloader FSM from working correctly in simulation or hardware.

## Root Cause of "No UART Output"

The UFM model is **NOT** the problem. The real issue (discovered in previous session) is:

**Bootloader FSM RAM write signals are not connected to actual RAM**

From `serv_zvibe_max10_08_eval.v` lines 230-237:

```verilog
wire [14:0] ram_address = bootloader_boot_complete ? 15'h0 : bootloader_ram_address;
wire [31:0] ram_write_data = bootloader_boot_complete ? 32'h0 : bootloader_ram_data;
wire        ram_write_enable = bootloader_boot_complete ? 1'b0 : bootloader_ram_write_enable;
wire [31:0] ram_read_data;

// TODO: Instantiate M9K RAM block with bootloader FSM write port
// For now, serv_zvibe handles RAM internally, but bootloader needs direct access
```

**Problem**: Signals are declared but never connected. RAM is inside serv_zvibe module with no external write ports.

**Current Workaround**: Use `memfile` parameter to pre-initialize RAM, bypassing bootloader entirely:

```verilog
serv_zvibe #(
    .memfile("flash_boot_banner.hex"),  // Pre-initialize RAM
    .wb_rst(reset),  // Bypass bootloader, use POR reset directly
```

## Conclusion

The simple model is **too optimistic** about read timing (1-2 cycles vs 7+ cycles real hardware), but this timing difference does **NOT** affect bootloader FSM functionality because:

1. FSM correctly waits for readdatavalid signal (not fixed cycle count)
2. Both models latch address on first read cycle (works with early deassertion)
3. No burst or pipelining used (simple one-at-a-time reads)

**Hypothesis Confirmed**: The bootloader FSM would work correctly in simulation with either model, but it cannot write to RAM due to architectural issue. The UFM model timing is **NOT** the root cause of the "no UART output" problem.

**Priority**:
1. ✅ Use memfile pre-initialization workaround (already implemented)
2. ⏭️ Test on hardware with current workaround
3. ⏭️ If hardware testing successful, decide whether to:
   - Keep memfile workaround (simple, works)
   - Fix RAM write connection for proper bootloader operation
   - Update UFM model for better timing accuracy (nice to have, not critical)

**Recommendation**: Don't update UFM model yet - test hardware first with current workaround. UFM model timing is not blocking progress.
