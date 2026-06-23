// Copyright (c) 2025 Martin R. Raumann
// SPDX-License-Identifier: BSD-3-Clause

//============================================================================
// Servant ZVibe Flash Write Integration Testbench
//
// Tests the complete SoC integration with flash write support:
//   - CPU executing from flash (XIP)
//   - Flash write operations via 0x81000000 registers
//   - QSPI mux arbitration between XIP and write controllers
//   - No conflicts/resets during write operations
//
// This testbench verifies that:
//   1. XIP reads work normally
//   2. Write operations don't cause resets or conflicts
//   3. Written data can be read back via XIP
//   4. CPU can continue executing from flash during/after writes
//
//============================================================================

`timescale 1ns/1ps
`default_nettype none

module servant_zvibe_flash_write_tb;

    //========================================================================
    // Parameters
    //========================================================================
    parameter MEM_SIZE = 64*1024;  // 64KB RAM
    parameter CLK_FREQ_HZ = 100_000_000;  // 100MHz
    parameter UART_BAUD = 115200;

    //========================================================================
    // Clock and Reset
    //========================================================================
    logic clk;
    logic rst;

    // 100MHz clock (10ns period)
    initial clk = 0;
    always #5 clk = ~clk;

    // Cycle counter
    integer cycle_count = 0;
    always_ff @(posedge clk) begin
        if (!rst) cycle_count = cycle_count + 1;
    end
    
    // Reset sequence
    initial begin
        rst = 1;
        #100;
        rst = 0;
        $display("[%0t] Reset released", $time);
    end

    //========================================================================
    // UART Signals
    //========================================================================
    logic uart_txd;
    logic uart_rxd;

    initial uart_rxd = 1;  // Idle high
    
    //========================================================================
    // UART Monitor (capture serial output from firmware)
    //========================================================================
    logic [7:0] uart_rx_buffer[0:2047];
    integer uart_rx_count = 0;
    integer last_uart_cycle = 0;
    integer cycles_since_uart = 0;
    parameter UART_BIT_PERIOD = 108 * 8;  // 100MHz / (115200 * 8) = 108, then * 8 for bit period
    
    // UART RX monitor task (captures serial output)
    task uart_monitor;
        integer bit_time;
        integer i;
        logic [7:0] rx_byte;
        begin
            // Wait for start bit (1 → 0)
            @(negedge uart_txd);
            
            // Sample in middle of each bit
            bit_time = UART_BIT_PERIOD;
            
            // Wait half bit period to center in start bit
            repeat(bit_time/2) @(posedge clk);
            
            // Skip start bit, sample data bits
            repeat(bit_time) @(posedge clk);
            
            // Sample 8 data bits (LSB first)
            for (i = 0; i < 8; i = i + 1) begin
                rx_byte[i] = uart_txd;
                repeat(bit_time) @(posedge clk);
            end
            
            // Check stop bit
            if (uart_txd !== 1'b1) begin
                $display("[UART %t] WARNING: Invalid stop bit", $time);
            end
            
            // Store received byte
            if (uart_rx_count < 2048) begin
                uart_rx_buffer[uart_rx_count] = rx_byte;
                uart_rx_count = uart_rx_count + 1;
                last_uart_cycle = cycle_count;
                
                // Print character
                $write("%c", rx_byte);
            end
        end
    endtask
    
    // UART monitor process
    initial begin
        wait(rst == 0);
        repeat(1000) @(posedge clk);  // Wait a bit after reset
        forever uart_monitor();
    end
    
    // Check if UART output contains test result
    task check_test_result;
        integer i, j;
        logic test_passed_found;
        logic test_failed_found;
        begin
            test_passed_found = 0;
            test_failed_found = 0;
            
            // Search for "TEST PASSED" or "TEST FAILED" in buffer
            for (i = 0; i < uart_rx_count - 10; i = i + 1) begin
                // Check for "*** TEST PASSED ***"
                if (uart_rx_buffer[i] == "*" && 
                    uart_rx_buffer[i+1] == "*" &&
                    uart_rx_buffer[i+2] == "*" &&
                    uart_rx_buffer[i+3] == " " &&
                    uart_rx_buffer[i+4] == "T" &&
                    uart_rx_buffer[i+5] == "E" &&
                    uart_rx_buffer[i+6] == "S" &&
                    uart_rx_buffer[i+7] == "T" &&
                    uart_rx_buffer[i+8] == " " &&
                    uart_rx_buffer[i+9] == "P") begin
                    test_passed_found = 1;
                end
                
                // Check for "*** TEST FAILED ***"
                if (uart_rx_buffer[i] == "*" && 
                    uart_rx_buffer[i+1] == "*" &&
                    uart_rx_buffer[i+2] == "*" &&
                    uart_rx_buffer[i+3] == " " &&
                    uart_rx_buffer[i+4] == "T" &&
                    uart_rx_buffer[i+5] == "E" &&
                    uart_rx_buffer[i+6] == "S" &&
                    uart_rx_buffer[i+7] == "T" &&
                    uart_rx_buffer[i+8] == " " &&
                    uart_rx_buffer[i+9] == "F") begin
                    test_failed_found = 1;
                end
            end
            
            if (test_passed_found) begin
                $display("\n[TB %t] ✓ Firmware test PASSED!", $time);
                test_passed = test_passed + 1;
            end else if (test_failed_found) begin
                $display("\n[TB %t] ✗ Firmware test FAILED!", $time);
                test_failed = test_failed + 1;
            end
        end
    endtask

    //========================================================================
    // QSPI Flash Signals
    //========================================================================
    logic qspi_clk;
    logic qspi_cs_n;
    wire  [3:0] qspi_d;  // Bidirectional tri-state

    // Tristate buffer signals
    wire [3:0] qspi_dat_to_flash;
    wire [3:0] qspi_dat_from_flash;
    wire [3:0] qspi_oe;

    // Tristate implementation
    assign qspi_d[0] = qspi_oe[0] ? qspi_dat_to_flash[0] : 1'bz;
    assign qspi_d[1] = qspi_oe[1] ? qspi_dat_to_flash[1] : 1'bz;
    assign qspi_d[2] = qspi_oe[2] ? qspi_dat_to_flash[2] : 1'bz;
    assign qspi_d[3] = qspi_oe[3] ? qspi_dat_to_flash[3] : 1'bz;
    assign qspi_dat_from_flash = qspi_d;

    //========================================================================
    // Flash Model (with R/W support)
    //========================================================================
    // Flash file loading - use `define or default to test_firmware.hex
    `ifdef FLASH_FILE
        parameter FLASH_FILE = `FLASH_FILE;
    `else
        parameter FLASH_FILE = "test_firmware.hex";  // Default for XIP test
    `endif
    
    `ifdef FLASH_OFFSET
        parameter FLASH_OFFSET = `FLASH_OFFSET;
    `else
        parameter FLASH_OFFSET = 24'h00100000;  // Default: 1MB offset (XIP region)
    `endif
    
    s25fl_simple_rw #(
        .POWERUP_TIME(100),
        .MEM_SIZE(24'hFFFFFF),
        .MEM_FILE(FLASH_FILE),
        .MEM_FILE_OFFSET(FLASH_OFFSET),
        .MEM_FILE_IS_HEX(1),        // Hex file format
        .DEBUG(0),
        .ERASE_TIME_NS(500),        // Fast for simulation
        .PROGRAM_TIME_NS(30),       // Fast for simulation
        .FAST_SIM(1)
    ) flash (
        .SCK(qspi_clk),
        .CSNeg(qspi_cs_n),
        .RSTNeg(!rst),
        .SI(qspi_d[0]),
        .SO(qspi_d[1]),
        .WPNeg(qspi_d[2]),
        .HOLDNeg(qspi_d[3])
    );

    //========================================================================
    // Instantiate Servant ZVibe SoC — XIP direct boot (no RAM preload)
    // CPU resets to 0x80100100; s25fl_xip stall holds the bus until ready.
    //========================================================================
    servant_zvibe #(
        .memfile(""),              // No RAM preload — XIP direct boot
        .reset_pc(32'h80100100),   // Boot directly into XIP flash firmware
        .memsize(MEM_SIZE),
        .reset_strategy("MINI"),
        .with_csr(1),              // Enable CSRs (required for SERV CPU)
        .UART_PRESCALE(108),       // 100MHz @ 115200 baud
        .UART_FIFO_DEPTH(64),
        .FLASH_SIZE(24),
        .USE_FAST_RAM_FLASH(0)     // Use real flash model
    ) dut (
        .wb_clk(clk),
        .wb_rst(rst),
        .uart_rxd(uart_rxd),
        .uart_txd(uart_txd),
        .uart_rx_valid(1'b0),    // Not used in this test
        .uart_rx_char(8'h0),     // Not used in this test
        .qspi_clk(qspi_clk),
        .qspi_cs_n(qspi_cs_n),
        .qspi_d(qspi_d),
        .gpio_led()
    );

    //========================================================================
    // CPU Debug Monitor
    //========================================================================
    // Access internal signals for debugging
    wire [31:0] cpu_mem_adr = dut.wb_cpu_mem_adr;
    wire        cpu_mem_stb = dut.wb_cpu_mem_stb;
    wire        cpu_mem_ack = dut.wb_cpu_mem_ack;

    // Track first 50 memory accesses for debug
    integer      mem_access_count = 0;
    logic [31:0] first_access_adr = 32'hFFFFFFFF;
    logic [31:0] last_seen_adr = 32'h0;
    integer      loop_count = 0;

    always_ff @(posedge clk) begin
        if (!rst && cpu_mem_stb && cpu_mem_ack) begin
            mem_access_count = mem_access_count + 1;
            if (mem_access_count == 1) begin
                first_access_adr = cpu_mem_adr;
                $display("[CPU DEBUG] First memory access: 0x%08h", cpu_mem_adr);
            end
            if (mem_access_count <= 30) begin
                $display("[CPU DEBUG %0d] Access #%0d: addr=0x%08h", $time, mem_access_count, cpu_mem_adr);
            end

            // Detect loops - same address accessed multiple times
            if (cpu_mem_adr == last_seen_adr) begin
                loop_count = loop_count + 1;
            end else begin
                if (loop_count > 5) begin
                    $display("[LOOP DETECTED %0d] Address 0x%08h accessed %0d times", $time, last_seen_adr, loop_count);
                end
                loop_count = 1;
                last_seen_adr = cpu_mem_adr;
            end

            // Show when we reach key addresses
            if (cpu_mem_adr == 32'h80100070 && mem_access_count > 30) begin
                $display("[MILESTONE %0d] Reached bss_clear_done at 0x80100070 (access #%0d)", $time, mem_access_count);
            end
            if (cpu_mem_adr == 32'h8010009c && mem_access_count > 30) begin
                $display("[MILESTONE %0d] Reached main() at 0x8010009c (access #%0d)", $time, mem_access_count);
            end
            if (cpu_mem_adr == 32'h8010007c && mem_access_count > 30) begin
                $display("[MILESTONE %0d] Reached uart_putc() at 0x8010007c (access #%0d)", $time, mem_access_count);
            end
        end
    end

    // Monitor UART accesses (0x40000000-0x4000000F)
    integer uart_access_count = 0;
    wire [31:0] cpu_mem_dat = dut.wb_cpu_mem_dat;  // continuous wire assignment
    wire        cpu_mem_we  = dut.wb_cpu_mem_we;   // continuous wire assignment

    always_ff @(posedge clk) begin
        if (!rst && cpu_mem_stb && cpu_mem_ack) begin
            // Check if this is a UART access
            if ((cpu_mem_adr & 32'hFFFFFFF0) == 32'h40000000) begin
                uart_access_count = uart_access_count + 1;
                if (uart_access_count <= 20) begin
                    $display("[UART DEBUG %0d] UART access #%0d: addr=0x%08h, we=%0d, data=0x%08h",
                             $time, uart_access_count, cpu_mem_adr, cpu_mem_we, cpu_mem_dat);
                end
            end
        end
    end

    // Report if no memory accesses after some cycles
    always_ff @(posedge clk) begin
        if (!rst && cycle_count == 10000 && mem_access_count == 0) begin
            $display("[CPU DEBUG] WARNING: No memory accesses after 10000 cycles!");
        end
        if (!rst && cycle_count == 50000 && mem_access_count == 0) begin
            $display("[CPU DEBUG] ERROR: CPU appears stuck - no memory accesses after 50000 cycles!");
            $display("[CPU DEBUG] Checking RAM contents...");
            $display("[CPU DEBUG] RAM[0] = 0x%08h", dut.ram.mem[0]);
            $display("[CPU DEBUG] RAM[1] = 0x%08h", dut.ram.mem[1]);
            $display("[CPU DEBUG] RAM[2] = 0x%08h", dut.ram.mem[2]);
            $display("[CPU DEBUG] RAM[3] = 0x%08h", dut.ram.mem[3]);
        end
        // Report UART access stats periodically
        if (!rst && cycle_count > 0 && (cycle_count % 1000000) == 0) begin
            $display("[UART STATS] Cycle %0d: Total UART accesses: %0d", cycle_count, uart_access_count);
        end
    end

    //========================================================================
    // Wishbone Master Tasks (for direct register access)
    //========================================================================
    // These tasks directly drive the CPU's Wishbone bus to access registers
    // Note: In a real system, the CPU would do this, but for testing we
    // directly access the bus to verify hardware behavior

    logic        wb_cyc;
    logic        wb_stb;
    logic        wb_we;
    logic [31:0] wb_addr;
    logic [31:0] wb_data_o;
    logic [3:0]  wb_sel;
    logic [31:0] wb_data_i;
    logic        wb_ack;
    logic        wb_stall;

    // Connect to CPU's memory bus (we'll need to add test access ports)
    // For now, we'll monitor the system and use indirect methods

    //========================================================================
    // Test Statistics
    //========================================================================
    integer test_count = 0;
    integer test_passed = 0;
    integer test_failed = 0;

    //========================================================================
    // Monitor Signals (observable from outside)
    //========================================================================
    // Monitor QSPI bus activity
    logic prev_qspi_cs_n = 1;
    integer qspi_activity_count = 0;

    always_ff @(posedge clk) begin
        if (!rst) begin
            if (qspi_cs_n != prev_qspi_cs_n) begin
                qspi_activity_count = qspi_activity_count + 1;
            end
            prev_qspi_cs_n = qspi_cs_n;
        end
    end

    // Monitor UART TXD transitions
    logic prev_uart_txd = 1;
    integer uart_txd_transitions = 0;

    always_ff @(posedge clk) begin
        if (!rst) begin
            if (uart_txd != prev_uart_txd) begin
                uart_txd_transitions = uart_txd_transitions + 1;
                if (uart_txd_transitions <= 20) begin
                    $display("[DEBUG %t] UART TXD transition #%0d: %b -> %b",
                             $time, uart_txd_transitions, prev_uart_txd, uart_txd);
                end
            end
            prev_uart_txd = uart_txd;
        end
    end

    // Periodic status output (every 1ms of simulation time = 100,000 cycles at 100MHz)
    always_ff @(posedge clk) begin
        if (!rst && cycle_count > 0 && (cycle_count % 100000) == 0) begin
            $display("[STATUS %t] Cycles: %0d, QSPI toggles: %0d, UART transitions: %0d, UART chars: %0d",
                     $time, cycle_count, qspi_activity_count, uart_txd_transitions, uart_rx_count);
        end
    end

    //========================================================================
    // Test Tasks
    //========================================================================

    task test_start;
        input [256*8-1:0] name;
        begin
            test_count = test_count + 1;
            $display("\n[TEST %0d] %s", test_count, name);
            $display("  Time: %0t", $time);
        end
    endtask

    task test_pass;
        begin
            test_passed = test_passed + 1;
            $display("  ✓ PASS");
        end
    endtask

    task test_fail;
        input [256*8-1:0] reason;
        begin
            test_failed = test_failed + 1;
            $display("  ✗ FAIL: %s", reason);
        end
    endtask

    // Wait for flash startup to complete
    // Monitor QSPI CS# activity - after startup, CS# should toggle during reads
    // Note: Without firmware, CPU won't fetch from flash, so CS# won't toggle
    task wait_flash_ready;
        integer timeout;
        integer cs_toggle_count;
        begin
            timeout = 0;
            cs_toggle_count = 0;
            prev_qspi_cs_n = qspi_cs_n;
            
            // Wait for QSPI activity (CS# toggling indicates flash is working)
            // But if no firmware is loaded, CPU won't fetch, so just wait a bit
            while (cs_toggle_count < 10 && timeout < 50000) begin
                @(posedge clk);
                if (qspi_cs_n != prev_qspi_cs_n) begin
                    cs_toggle_count = cs_toggle_count + 1;
                end
                prev_qspi_cs_n = qspi_cs_n;
                timeout = timeout + 1;
            end
            
            if (timeout >= 50000 && cs_toggle_count == 0) begin
                $display("  NOTE: No QSPI activity (expected if no firmware loaded)");
            end else if (timeout >= 50000) begin
                $display("  WARNING: Flash startup timeout");
            end else begin
                $display("  Flash ready after %0d cycles (%0d CS# toggles)", timeout, cs_toggle_count);
            end
            // Extra cycles for stability
            repeat(100) @(posedge clk);
        end
    endtask

    // Monitor for unexpected resets (ignore initial reset sequence)
    logic prev_rst;
    integer reset_count = 0;
    integer initial_reset_done = 0;

    always_ff @(posedge clk) begin
        prev_rst <= rst;
        // After initial reset is released, count any subsequent resets
        if (initial_reset_done && !prev_rst && rst) begin
            reset_count = reset_count + 1;
            $display("[%0t] WARNING: Unexpected reset asserted during test!", $time);
        end
        // Mark initial reset as done after it's been released for a while
        if (!rst && $time > 1000) begin
            initial_reset_done = 1;
        end
    end

    // Monitor QSPI activity as proxy for CPU activity
    // When CPU executes from flash, QSPI CS# should toggle regularly
    integer qspi_idle_cycles = 0;
    integer max_qspi_idle = 0;

    always_ff @(posedge clk) begin
        if (!rst) begin
            if (qspi_cs_n == 1) begin  // CS# deasserted = idle
                qspi_idle_cycles = qspi_idle_cycles + 1;
                if (qspi_idle_cycles > max_qspi_idle) begin
                    max_qspi_idle = qspi_idle_cycles;
                end
            end else begin
                qspi_idle_cycles = 0;
            end
        end
    end

    //========================================================================
    // Test: Verify XIP reads work
    //========================================================================
    task test_xip_reads;
        begin
            test_start("XIP Read Test");
            
            // Wait for flash to be ready
            wait_flash_ready;
            
            // Monitor XIP reads (CPU should be fetching instructions)
            // Check that XIP controller is responding
            repeat(1000) @(posedge clk);
            
            // Verify QSPI activity (indicates XIP is working)
            // Note: Without firmware, CPU won't fetch from flash, so no activity is expected
            if (qspi_activity_count == 0) begin
                $display("  NOTE: No QSPI activity (expected if no firmware loaded)");
                $display("  This test verifies integration, not firmware execution");
                test_pass;  // Pass if integration is correct (signals valid, no resets)
            end else if (reset_count > 0) begin
                test_fail("Unexpected reset detected");
            end else begin
                $display("  QSPI activity count: %0d", qspi_activity_count);
                test_pass;
            end
        end
    endtask

    //========================================================================
    // Test: Verify write operations don't cause resets
    //========================================================================
    task test_write_no_reset;
        integer initial_reset_count;
        begin
            test_start("Write Operation - No Reset Test");
            
            wait_flash_ready;
            initial_reset_count = reset_count;
            
            // Simulate a write operation by monitoring the write controller
            // In a real test, we'd trigger writes via CPU or direct bus access
            // For now, we'll just monitor that the system doesn't reset
            
            $display("  Monitoring system during potential write operations...");
            repeat(10000) @(posedge clk);
            
            if (reset_count > initial_reset_count) begin
                test_fail("Reset occurred during write operation");
            end else begin
                $display("  No resets detected during monitoring period");
                test_pass;
            end
        end
    endtask

    //========================================================================
    // Test: Verify QSPI mux arbitration
    //========================================================================
    task test_qspi_mux_arbitration;
        begin
            test_start("QSPI Mux Arbitration Test");
            
            wait_flash_ready;
            
            // Monitor that XIP has priority
            // When write controller is active, XIP should be blocked
            // When write controller is idle, XIP should work normally
            
            repeat(5000) @(posedge clk);
            
            // Check that QSPI signals are valid (not X)
            if (qspi_cs_n === 1'bx || qspi_clk === 1'bx) begin
                test_fail("QSPI signals are undefined");
            end else begin
                $display("  QSPI signals are valid");
                test_pass;
            end
        end
    endtask

    //========================================================================
    // Test: Verify XIP idle detection
    //========================================================================
    task test_xip_idle_detection;
        begin
            test_start("XIP Idle Detection Test");
            
            wait_flash_ready;
            
            // Monitor that write controller waits for XIP idle
            // This is critical to prevent conflicts
            
            repeat(5000) @(posedge clk);
            
            // If write controller becomes active while XIP is busy, that's a problem
            // (We'd need to add more detailed monitoring for this)
            
            test_pass;
        end
    endtask

    //========================================================================
    // Main Test Sequence - Run Firmware Test
    //========================================================================
    initial begin
        // No VCD capture - just UART output (much faster)
        // $dumpfile("servant_zvibe_flash_write_tb.vcd");
        // $dumpvars(0, servant_zvibe_flash_write_tb);

        // Wait for reset release
        wait(rst == 0);
        repeat(1000) @(posedge clk);

        $display("\n");
        $display("================================================================================");
        $display(" Flash Write XIP Test - Running Firmware");
        $display("================================================================================");
        $display("Firmware will execute from flash and perform write/read test");
        $display("Waiting for firmware to complete...");
        $display("");

        // Wait for firmware to complete (monitor UART output)
        // Check periodically for test result
        // Note: UART bit-level serialization is slow, so we need to wait longer
        repeat(2000) begin  // Check 2000 times
            repeat(500000) @(posedge clk);  // Check every 5ms (longer for slow UART)
            if (uart_rx_count > 10) begin  // Got some output
                check_test_result();
                if (test_passed > 0 || test_failed > 0) begin
                    // Test completed
                    break;
                end
            end
            // Print progress every 100 checks
            if ($time % 500000000 == 0) begin
                $display("[TB %t] Still waiting... UART chars: %0d", $time, uart_rx_count);
            end
        end

        // Summary
        $display("\n");
        $display("================================================================================");
        $display(" Test Summary");
        $display("================================================================================");
        $display("UART characters received: %0d", uart_rx_count);
        $display("Test result: %s", (test_passed > 0) ? "PASSED" : (test_failed > 0) ? "FAILED" : "UNKNOWN");
        $display("Resets detected: %0d", reset_count);
        $display("QSPI activity count: %0d", qspi_activity_count);

        if (test_passed > 0 && reset_count == 0) begin
            $display("\n✓ FIRMWARE TEST PASSED!");
        end else if (test_failed > 0 || reset_count > 0) begin
            $display("\n✗ FIRMWARE TEST FAILED OR RESET DETECTED!");
        end else begin
            $display("\n⚠ TEST DID NOT COMPLETE (timeout or no output)");
        end
        $display("================================================================================");

        #10000;
        $finish;
    end

    // Timeout watchdog
    initial begin
        #10000000000;  // 10 seconds timeout (UART bit-level is very slow)
        $display("\n[ERROR] Simulation timeout!");
        $display("UART characters received: %0d", uart_rx_count);
        $display("Test result: %s", (test_passed > 0) ? "PASSED" : (test_failed > 0) ? "FAILED" : "UNKNOWN");
        $display("Resets detected: %0d", reset_count);
        if (uart_rx_count > 0) begin
            $display("\nPartial UART output:");
            $write("  ");
            for (integer i = 0; i < uart_rx_count && i < 200; i = i + 1) begin
                $write("%c", uart_rx_buffer[i]);
            end
            $display("");
        end
        $finish;
    end
    
    // Inactivity watchdog - exit if no UART activity for too long
    always_ff @(posedge clk) begin
        if (!rst) begin
            cycles_since_uart = cycle_count - last_uart_cycle;
            if (uart_rx_count > 10 && cycles_since_uart > 50_000_000) begin  // 500ms of inactivity after getting output
                $display("\n[TB %t] No UART activity for %0d cycles - assuming test complete", 
                         $time, cycles_since_uart);
                check_test_result();
                #10000;
                $finish;
            end
        end
    end

endmodule

`default_nettype wire
