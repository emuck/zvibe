// Copyright (c) 2025 Martin R. Raumann
// SPDX-License-Identifier: BSD-3-Clause

//============================================================================
// MAX10 XIP Testbench
//
// Tests the full XIP boot path with UART TX output and UART RX injection.
// With FAST_UART=1, uart_wb_model handles I/O directly via uart_output.txt.
//
// Sequence:
//   1. Boot: firmware prints banner + "Ready> " (~84K cycles)
//   2. Inject "EcHo!\n" character-by-character (one per CHAR_GAP cycles)
//   3. Firmware echoes each char immediately; on \n prints "You typed: EcHo!"
//   4. Wait ECHO_WINDOW cycles for firmware to finish echoing
//   5. $finish — Makefile greps uart_output.txt for "EcHo!" to determine PASS/FAIL
//============================================================================

`timescale 1ns/1ps

module max10_xip_tb();

    // Module-scope flags (visible to final block)
    logic test_complete   = 1'b0;  // set by injection initial block before $finish
    logic inactivity_exit = 1'b0;  // set if safety watchdog fires

    //========================================================================
    // Parameters
    //========================================================================
    parameter CLK_FREQ_HZ   = 100_000_000;
    parameter UART_BAUD     = 115_200;
    parameter FAST_UART     = 1;

    // Timing (all in sys_clk cycles at 100MHz)
    localparam BOOT_THRESHOLD     = 150_000;  // wait before injecting; boot ~84K
    localparam CHAR_GAP           =  50_000;  // cycles between injected chars
    localparam ECHO_WINDOW        = 200_000;  // cycles to wait after last char
    localparam INACTIVITY_TIMEOUT = 800_000;  // safety net (> BOOT+6*GAP+ECHO=650K)
    localparam MAX_CYCLES         = 2_000_000; // hard upper bound

    //========================================================================
    // Clock and Reset
    //========================================================================
    logic clk_50mhz = 0;
    logic dip_sw1   = 1;          // start in reset

    always #10 clk_50mhz = ~clk_50mhz;   // 50 MHz

    initial begin
        #200 dip_sw1 = 0;
        $display("[TB %t] Reset released", $time);
    end

    // Cycle counter on sys_clk (100 MHz PLL output)
    integer cycle_count = 0;
    always_ff @(posedge dut.sys_clk) cycle_count <= cycle_count + 1;

    // Hard timeout
    always_ff @(posedge dut.sys_clk) begin
        if (MAX_CYCLES > 0 && cycle_count >= MAX_CYCLES) begin
            $display("\n[TB %t] Hard timeout after %0d cycles", $time, MAX_CYCLES);
            $finish;
        end
    end

    // Safety watchdog — fires if sim hangs before injection $finish (cycle 650K)
    always_ff @(posedge dut.sys_clk) begin
        if (INACTIVITY_TIMEOUT > 0 && !dip_sw1) begin
            if (cycle_count >= INACTIVITY_TIMEOUT) begin
                $display("\n[TB %t] Safety watchdog at %0d cycles", $time, cycle_count);
                inactivity_exit = 1'b1;
                $finish;
            end
        end
    end

    //========================================================================
    // UART Signals
    //========================================================================
    wire        uart_txd;
    logic       uart_rxd      = 1'b1;   // idle high (not used with FAST_UART)

    // UART RX injection (routed through board wrapper to uart_wb_model)
    logic       uart_rx_valid = 1'b0;
    logic [7:0] uart_rx_char  = 8'h00;

    //========================================================================
    // DUT
    //========================================================================
    wire [4:0] gpio_led;

    servant_zvibe_max10_08_eval_xip #(
        .FAST_UART(FAST_UART)
    ) dut (
        .clk_50mhz    (clk_50mhz),
        .dip_sw1      (dip_sw1),
        .uart_rxd     (uart_rxd),
        .uart_txd     (uart_txd),
        .gpio_led     (gpio_led),
        .uart_rx_valid(uart_rx_valid),
        .uart_rx_char (uart_rx_char)
    );

    //========================================================================
    // Test Header
    //========================================================================
    initial begin
        $display("============================================");
        $display("Servant ZVibe UART Echo Test (MAX10 XIP)");
        $display("============================================");
        $display("Clock: 50MHz -> 100MHz PLL");
        $display("UART: %0d baud, FAST_UART=%0d", UART_BAUD, FAST_UART);
        $display("Boot threshold: %0d cycles", BOOT_THRESHOLD);
        $display("Char gap:       %0d cycles", CHAR_GAP);
        $display("Echo window:    %0d cycles", ECHO_WINDOW);
        $display("============================================");
    end

    //========================================================================
    // UART RX Injection: "EcHo!\n"
    //
    // Each character is asserted for 2 clock cycles to avoid sim scheduling
    // races with the uart_wb_model's always_ff edge-sampling.
    // The #1 (1ps) delay after @(posedge) ensures we're past the active region.
    //
    // Chars: E=0x45  c=0x63  H=0x48  o=0x6F  !=0x21  \n=0x0A
    //========================================================================
    initial begin
        $display("[TB] Waiting %0d cycles for firmware boot...", BOOT_THRESHOLD);
        repeat(BOOT_THRESHOLD) @(posedge dut.sys_clk);
        $display("[TB %t] Injecting 'EcHo!\\n'", $time);

        // 'E'
        @(posedge dut.sys_clk); #1; uart_rx_char = 8'h45; uart_rx_valid = 1'b1;
        @(posedge dut.sys_clk); #1; uart_rx_valid = 1'b0;
        repeat(CHAR_GAP - 2) @(posedge dut.sys_clk);

        // 'c'
        @(posedge dut.sys_clk); #1; uart_rx_char = 8'h63; uart_rx_valid = 1'b1;
        @(posedge dut.sys_clk); #1; uart_rx_valid = 1'b0;
        repeat(CHAR_GAP - 2) @(posedge dut.sys_clk);

        // 'H'
        @(posedge dut.sys_clk); #1; uart_rx_char = 8'h48; uart_rx_valid = 1'b1;
        @(posedge dut.sys_clk); #1; uart_rx_valid = 1'b0;
        repeat(CHAR_GAP - 2) @(posedge dut.sys_clk);

        // 'o'
        @(posedge dut.sys_clk); #1; uart_rx_char = 8'h6F; uart_rx_valid = 1'b1;
        @(posedge dut.sys_clk); #1; uart_rx_valid = 1'b0;
        repeat(CHAR_GAP - 2) @(posedge dut.sys_clk);

        // '!'
        @(posedge dut.sys_clk); #1; uart_rx_char = 8'h21; uart_rx_valid = 1'b1;
        @(posedge dut.sys_clk); #1; uart_rx_valid = 1'b0;
        repeat(CHAR_GAP - 2) @(posedge dut.sys_clk);

        // '\n' — triggers "You typed: EcHo!\r\nReady> "
        @(posedge dut.sys_clk); #1; uart_rx_char = 8'h0A; uart_rx_valid = 1'b1;
        @(posedge dut.sys_clk); #1; uart_rx_valid = 1'b0;

        $display("[TB %t] Injection done. Waiting %0d cycles for echo...", $time, ECHO_WINDOW);
        repeat(ECHO_WINDOW) @(posedge dut.sys_clk);

        $display("[TB %t] Echo window done at cycle %0d. Finishing.", $time, cycle_count);
        test_complete = 1'b1;
        $finish;
    end

    //========================================================================
    // Final PASS/FAIL Report
    //========================================================================
    final begin
        if (test_complete)
            $display("PASS: max10_xip_tb");
        else if (inactivity_exit)
            $display("FAIL: max10_xip_tb (safety watchdog - boot or echo hung)");
        else
            $display("FAIL: max10_xip_tb (hard timeout)");
    end

endmodule
