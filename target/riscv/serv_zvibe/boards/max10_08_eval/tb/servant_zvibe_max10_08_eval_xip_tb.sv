///////////////////////////////////////////////////////////////////////////////
// servant_zvibe_max10_08_eval_xip_tb.sv
//
// Board-Level Testbench for MAX10 08 Evaluation Board (XIP Configuration)
//
// This testbench simulates the complete synthesizable top-level design:
// - External 50MHz clock input
// - PLL (50MHz → 100MHz) or Internal OSC (116MHz)
// - Power-on reset sequencing
// - Complete UFM integration path
// - Pin-level UART and GPIO interfaces
//
// Verifies:
// 1. Clock generation (PLL vs internal oscillator)
// 2. Reset sequencing with POR counter
// 3. CPU boots from UFM @ 0x80000100
// 4. Complete path: clock → reset → UFM → CPU → UART
//
// Copyright (c) 2025 Martin R. Raumann
// SPDX-License-Identifier: BSD-3-Clause
//
///////////////////////////////////////////////////////////////////////////////

`timescale 1ns / 1ps
`default_nettype none

module servant_zvibe_max10_08_eval_xip_tb;

    //========================================================================
    // Parameters
    //========================================================================
    localparam CLK_50MHZ_PERIOD = 20;   // 50MHz external clock (20ns period)
    localparam int MAX_SIM_CYCLES = 300_000; // 3ms at 100MHz (replaces #3ms timeout)

    // Pass/fail state visible to final block
    int   error_count     = 0;
    logic timeout_occurred = 1'b0;

    //========================================================================
    // External Clock (50MHz)
    //========================================================================
    logic clk_50mhz = 0;
    always #(CLK_50MHZ_PERIOD/2) clk_50mhz = ~clk_50mhz;

    //========================================================================
    // Board Inputs
    //========================================================================
    logic dip_sw1 = 1'b0;      // PLL async reset (normally OFF)
    logic uart_rxd = 1'b1;     // UART receive (idle high)

    //========================================================================
    // Board Outputs
    //========================================================================
    wire uart_txd;           // UART transmit

    //========================================================================
    // Internal Probes (for monitoring)
    //========================================================================
    wire sys_clk;            // System clock (100MHz or 116MHz)
    wire pll_locked;         // PLL lock status
    wire reset;              // System reset
    wire [15:0] ufm_address; // UFM word address

    // Access internal signals for monitoring
    assign sys_clk = dut.sys_clk;
    assign pll_locked = dut.pll_locked;
    assign reset = dut.reset;
    assign ufm_address = dut.ufm_address;

    //========================================================================
    // UFM Model Access
    //========================================================================
    // The DUT instantiates ufm_sim.v (simulation model) instead of real IP core
    // We can access its memory array to pre-load test programs

    //========================================================================
    // Initialize UFM with Test Program
    //========================================================================
    // CPU reset_pc = 0x80000000 (byte address)
    // This maps directly to UFM word address 0x000 (start of UFM)
    //
    // Simple program at UFM word address 0x000:
    //   lui a0, 0x40000       # a0 = UART base
    //   li  a1, 0x41          # a1 = 'A'
    //   sw  a1, 0(a0)         # Send 'A' to UART
    //   li  a0, 0x7           # LED pattern
    //   lui a1, 0x40000
    //   sw  a0, 32(a1)        # Write to GPIO
    // loop:
    //   j loop
    //
    initial begin
        integer i;
        // Wait for DUT to be instantiated
        #1;

        // Pre-load UFM simulation model with test program
        // Test program at word address 0x000 (start of UFM)
        // CPU reset_pc = 0x80000000 → UFM word address 0x000
        //
        // Program to send "Hello!\n" via UART:
        //   li   a0, 0x40000000  # UART base
        //   li   a1, 'H'
        //   sw   a1, 0(a0)       # Send 'H'
        //   li   a1, 'e'
        //   sw   a1, 0(a0)       # Send 'e'
        //   li   a1, 'l'
        //   sw   a1, 0(a0)       # Send 'l'
        //   sw   a1, 0(a0)       # Send 'l' (reuse a1)
        //   li   a1, 'o'
        //   sw   a1, 0(a0)       # Send 'o'
        //   li   a1, '!'
        //   sw   a1, 0(a0)       # Send '!'
        //   li   a1, '\n'
        //   sw   a1, 0(a0)       # Send newline
        // loop:
        //   j loop

        // NOTE: When using vendor UFM IP, the memory is initialized via
        // INIT_FILENAME_SIM parameter in vsim command (e.g., ufm_hello_test.dat)
        // The .dat file should contain the test program starting at word address 0x40

        // For behavioral model (ufm_sim.v), we can directly access memory:
        `ifdef UFM_BEHAVIORAL_MODEL
            // Load at UFM byte offset 0x100 (word address 0x40)
            // CPU reset_pc = 0x80000100 → UFM word address 0x40
            dut.ufm_inst.memory[16'h0040] = 32'h40000537;  // lui a0, 0x40000  (a0 = 0x40000000)
            dut.ufm_inst.memory[16'h0041] = 32'h04800593;  // li  a1, 0x48     ('H')
            dut.ufm_inst.memory[16'h0042] = 32'h00B52023;  // sw  a1, 0(a0)
            dut.ufm_inst.memory[16'h0043] = 32'h06500593;  // li  a1, 0x65     ('e')
            dut.ufm_inst.memory[16'h0044] = 32'h00B52023;  // sw  a1, 0(a0)
            dut.ufm_inst.memory[16'h0045] = 32'h06C00593;  // li  a1, 0x6C     ('l')
            dut.ufm_inst.memory[16'h0046] = 32'h00B52023;  // sw  a1, 0(a0)
            dut.ufm_inst.memory[16'h0047] = 32'h00B52023;  // sw  a1, 0(a0)    ('l' again)
            dut.ufm_inst.memory[16'h0048] = 32'h06F00593;  // li  a1, 0x6F     ('o')
            dut.ufm_inst.memory[16'h0049] = 32'h00B52023;  // sw  a1, 0(a0)
            dut.ufm_inst.memory[16'h004A] = 32'h02100593;  // li  a1, 0x21     ('!')
            dut.ufm_inst.memory[16'h004B] = 32'h00B52023;  // sw  a1, 0(a0)
            dut.ufm_inst.memory[16'h004C] = 32'h00A00593;  // li  a1, 0x0A     ('\n')
            dut.ufm_inst.memory[16'h004D] = 32'h00B52023;  // sw  a1, 0(a0)
            dut.ufm_inst.memory[16'h004E] = 32'h0000006F;  // j   0            (loop forever)

            $display("[TB] Pre-loaded UFM with 'Hello!' test program at byte offset 0x100 (word addr 0x40)");
            $display("[TB] UFM[0x0040] = 0x%08X (lui a0, 0x40000)", dut.ufm_inst.memory[16'h0040]);
            $display("[TB] UFM[0x0041] = 0x%08X (li a1, 'H')", dut.ufm_inst.memory[16'h0041]);
            $display("[TB] UFM[0x0042] = 0x%08X (sw a1, 0(a0))", dut.ufm_inst.memory[16'h0042]);
        `else
            $display("[TB] UFM initialized from .dat file via INIT_FILENAME_SIM parameter");
            $display("[TB] Test program should be at byte offset 0x100 (word addr 0x40)");
        `endif
    end

    //========================================================================
    // DUT: Top-Level Board Design
    //========================================================================
    servant_zvibe_max10_08_eval_xip dut (
        .clk_50mhz    (clk_50mhz),
        .dip_sw1      (dip_sw1),
        .uart_rxd     (uart_rxd),
        .uart_txd     (uart_txd),
        .gpio_led     (),
        .uart_rx_valid(1'b0),
        .uart_rx_char (8'h00)
    );

    //========================================================================
    // Cycle Counter and Watchdog (replaces time-based timeout)
    //========================================================================
    int cycle_count = 0;
    always_ff @(posedge sys_clk) cycle_count++;

    always_ff @(posedge sys_clk) begin
        if (cycle_count >= MAX_SIM_CYCLES) begin
            $display("[TB] ✗ TIMEOUT: Simulation ran too long (%0d cycles)", MAX_SIM_CYCLES);
            timeout_occurred = 1'b1;
            $finish;
        end
    end

    //========================================================================
    // Test Variables
    //========================================================================
    integer uart_count = 0;
    logic [31:0] first_pc = 32'h0;
    logic first_fetch = 1'b0;
    logic pll_lock_seen = 1'b0;

    // Buffer to collect UART characters
    logic [7:0] uart_buffer [0:31];
    integer uart_idx = 0;

    //========================================================================
    // Test Sequence
    //========================================================================
    initial begin
        $dumpfile("servant_zvibe_max10_08_eval_xip_tb.vcd");
        $dumpvars(0, servant_zvibe_max10_08_eval_xip_tb);

        $display("========================================");
        $display("MAX10 Board-Level XIP Testbench");
        $display("========================================");
        $display("Clock source: PLL (100MHz from 50MHz)");
        $display("Testing complete synthesizable design\n");

        // Wait for PLL lock
        $display("[TB] Waiting for PLL to lock...");
        wait(pll_locked);
        pll_lock_seen = 1'b1;
        $display("[TB] ✓ PLL locked");

        // Wait for POR counter to finish
        $display("[TB] Waiting for power-on reset to complete...");
        wait(!reset);
        $display("[TB] ✓ Reset released");

        // Wait for first instruction fetch
        $display("[TB] Waiting for first instruction fetch...");
        wait(first_fetch);
        $display("[TB] First instruction fetch at PC = 0x%08X", first_pc);

        if (first_pc == 32'h80000100) begin
            $display("[TB] ✓ SUCCESS: CPU started at correct address 0x80000100");
        end else begin
            $display("[TB] ✗ ERROR: CPU started at wrong address 0x%08X (expected 0x80000100)", first_pc);
        end

        // Wait for UART transmission
        // 7 characters at 115200 baud = ~609us
        // Add margin for CPU execution time
        $display("[TB] Waiting for UART transmission...");
        #(CLK_50MHZ_PERIOD * 50000);  // 50000 * 20ns = 1ms

        if (uart_count >= 7) begin
            $display("[TB] ✓ SUCCESS: Received %0d UART characters (expected 7)", uart_count);
        end else if (uart_count > 0) begin
            $display("[TB] ⚠ WARNING: Received only %0d UART characters (expected 7)", uart_count);
        end else begin
            $display("[TB] ✗ ERROR: No UART characters received");
        end

        $display("\n========================================");
        $display("Test Summary");
        $display("========================================");
        $display("PLL lock:     %s", pll_lock_seen ? "✓" : "✗");
        $display("Reset release: ✓");
        $display("Boot address: 0x%08X %s", first_pc,
                 (first_pc == 32'h80000100) ? "✓" : "✗");
        $display("UART chars:   %0d %s", uart_count,
                 (uart_count >= 7) ? "✓" : (uart_count > 0) ? "⚠" : "✗");

        if (first_pc == 32'h80000100 && uart_count >= 7) begin
            $display("\n✓✓✓ ALL TESTS PASSED ✓✓✓");
        end else if (first_pc == 32'h80000100 && uart_count > 0) begin
            $display("\n⚠⚠⚠ TESTS MOSTLY PASSED (partial UART) ⚠⚠⚠");
        end else begin
            $display("\n✗✗✗ SOME TESTS FAILED ✗✗✗");
        end
        $display("========================================\n");

        // Compute error_count for final block
        if (first_pc != 32'h80000100) error_count++;
        if (uart_count < 7) error_count++;
        $finish;
    end

    //========================================================================
    // Monitor First Instruction Fetch
    //========================================================================
    always_ff @(posedge sys_clk) begin
        if (!reset && !first_fetch) begin
            // Check for instruction fetch (cyc && stb on flash interface)
            if (dut.wb_flash_cyc && dut.wb_flash_stb) begin
                first_pc = dut.wb_flash_adr;
                first_fetch = 1'b1;
                $display("[TB] First fetch detected: addr=0x%08X", dut.wb_flash_adr);
            end
        end
    end

    //========================================================================
    // Monitor UART Transmissions
    //========================================================================
    always_ff @(posedge sys_clk) begin
        if (!reset) begin
            // Monitor writes to UART (0x40000000)
            if (dut.soc.wb_uart_ack && dut.soc.wb_uart_we &&
                (dut.soc.wb_uart_adr[7:0] == 8'h00)) begin
                $display("[TB] UART TX: 0x%02X ('%c')",
                         dut.soc.wb_uart_dat[7:0],
                         (dut.soc.wb_uart_dat[7:0] >= 32 && dut.soc.wb_uart_dat[7:0] < 127) ?
                         dut.soc.wb_uart_dat[7:0] : ".");
                // Store in buffer
                if (uart_idx < 32) begin
                    uart_buffer[uart_idx] = dut.soc.wb_uart_dat[7:0];
                    uart_idx = uart_idx + 1;
                end
                uart_count = uart_count + 1;
            end
        end
    end

    //========================================================================
    // Monitor Clock and Reset Signals
    //========================================================================
    // (Removed excessive clk_50mhz cycle prints)

    initial begin
        // Monitor PLL lock transition
        @(posedge pll_locked);
        $display("[TB] PLL locked at time %0t", $time);

        // Monitor reset release
        @(negedge reset);
        $display("[TB] Reset released at time %0t", $time);
    end

    //========================================================================
    // Final PASS/FAIL Report
    //========================================================================
    final begin
        if (timeout_occurred)
            $display("FAIL: max10_board_xip_tb (timeout)");
        else if (error_count == 0)
            $display("PASS: max10_board_xip_tb");
        else
            $display("FAIL: max10_board_xip_tb (%0d errors)", error_count);
    end

endmodule

`default_nettype wire
