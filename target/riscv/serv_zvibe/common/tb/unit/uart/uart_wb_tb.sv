// Copyright (c) 2025 Martin R. Raumann
// SPDX-License-Identifier: BSD-3-Clause

//=============================================================================
// uart_wb_tb.sv
//
// UART Wishbone Interface Testbench
//
// Tests:
//   TEST 1: Basic TX/RX functionality
//   TEST 2: TX FIFO overflow error flag
//   TEST 3: RX FIFO overflow error flag
//   TEST 4: Error flag sticky behavior (clear on status read)
//   TEST 5: Frame error flag (via rx_core injection)
//   TEST 6: Overrun error flag (via rx_core injection)
//=============================================================================

`timescale 1ns/1ps
`default_nettype none

module uart_wb_tb;

    //=========================================================================
    // Test Identity and Timeout
    //=========================================================================
    parameter string TEST_NAME      = "uart_wb_tb";
    parameter int    MAX_SIM_CYCLES = 500_000;

    //=========================================================================
    // Clock and Reset
    //=========================================================================
    logic clk = 0;
    logic rst = 0;
    always #5 clk = ~clk;  // 100MHz clock (10ns period)

    //=========================================================================
    // Cycle Counter and Watchdog
    //=========================================================================
    int  cycle_count    = 0;
    logic timeout_occurred = 1'b0;

    always_ff @(posedge clk) cycle_count++;

    always_ff @(posedge clk) begin
        if (cycle_count >= MAX_SIM_CYCLES) begin
            $display("[TIMEOUT] %s timed out after %0d cycles", TEST_NAME, MAX_SIM_CYCLES);
            timeout_occurred = 1'b1;
            $finish;
        end
    end

    //=========================================================================
    // Test Control
    //=========================================================================
    int test_count  = 0;
    int pass_count  = 0;
    int fail_count  = 0;
    int error_count = 0;

    task test_pass;
        begin
            pass_count = pass_count + 1;
            $display("*** PASS ***\n");
        end
    endtask

    task test_fail;
        input [1023:0] msg;
        begin
            fail_count  = fail_count  + 1;
            error_count = error_count + 1;
            $display("*** FAIL: %s ***\n", msg);
        end
    endtask

    //=========================================================================
    // DUT Signals
    //=========================================================================
    logic [31:0] wb_adr;
    logic [31:0] wb_dat_i;
    logic [31:0] wb_dat_o;
    logic        wb_we;
    logic        wb_cyc;
    logic        wb_stb;
    logic        wb_ack;

    logic        uart_txd;
    logic        uart_rxd;

    //=========================================================================
    // DUT Instantiation
    //=========================================================================
    uart_wb #(
        .PRESCALE(10),      // Fast for simulation (not realistic baud)
        .FIFO_DEPTH(16)     // Smaller FIFO for easier overflow testing
    ) dut (
        .i_wb_clk(clk),
        .i_wb_rst(rst),
        .i_wb_adr(wb_adr),
        .i_wb_dat(wb_dat_i),
        .o_wb_dat(wb_dat_o),
        .i_wb_we(wb_we),
        .i_wb_cyc(wb_cyc),
        .i_wb_stb(wb_stb),
        .o_wb_ack(wb_ack),
        .uart_txd(uart_txd),
        .uart_rxd(uart_rxd)
    );

    //=========================================================================
    // Wishbone Addresses
    //=========================================================================
    localparam ADDR_TX_DATA = 32'h00000000;
    localparam ADDR_RX_DATA = 32'h00000004;
    localparam ADDR_STATUS  = 32'h00000008;

    //=========================================================================
    // Status Register Bit Positions
    //=========================================================================
    localparam BIT_TX_READY         = 0;
    localparam BIT_RX_READY         = 1;
    localparam BIT_RX_FRAME_ERROR   = 2;
    localparam BIT_RX_OVERRUN_ERROR = 3;
    localparam BIT_RX_FIFO_OVERFLOW = 4;
    localparam BIT_TX_FIFO_OVERFLOW = 5;

    //=========================================================================
    // Wishbone Helper Tasks
    //=========================================================================

    task wb_write;
        input [31:0] addr;
        input [31:0] data;
        begin
            @(posedge clk);
            #1;
            wb_adr = addr;
            wb_dat_i = data;
            wb_we = 1;
            wb_cyc = 1;
            wb_stb = 1;

            @(posedge clk);
            wait (wb_ack);
            @(posedge clk);
            #1;
            wb_cyc = 0;
            wb_stb = 0;
            wb_we = 0;
        end
    endtask

    task wb_read;
        input  [31:0] addr;
        output [31:0] data;
        begin
            @(posedge clk);
            #1;
            wb_adr = addr;
            wb_we = 0;
            wb_cyc = 1;
            wb_stb = 1;

            @(posedge clk);
            wait (wb_ack);
            data = wb_dat_o;
            @(posedge clk);
            #1;
            wb_cyc = 0;
            wb_stb = 0;
        end
    endtask

    // Non-blocking write (for overflow testing - no wait for ACK)
    task wb_write_no_wait;
        input [31:0] addr;
        input [31:0] data;
        begin
            @(posedge clk);
            #1;
            wb_adr = addr;
            wb_dat_i = data;
            wb_we = 1;
            wb_cyc = 1;
            wb_stb = 1;
            @(posedge clk);
            #1;
        end
    endtask

    //=========================================================================
    // UART RX Byte Injection (simplified - not full UART protocol)
    //=========================================================================

    // For testing, we'll directly inject data into the RX FIFO
    // by manipulating internal signals (more reliable than bit-level UART)

    task inject_rx_byte;
        input [7:0] data;
        begin
            // Direct injection into RX core (bypass actual UART protocol)
            // This requires accessing DUT internals
            force dut.rx_core_data = data;
            force dut.rx_core_valid = 1;
            @(posedge clk);
            #1;
            release dut.rx_core_valid;
            release dut.rx_core_data;
            @(posedge clk);
        end
    endtask

    task inject_rx_frame_error;
        begin
            // Inject frame error
            force dut.rx_core_frame_error = 1;
            @(posedge clk);
            #1;
            release dut.rx_core_frame_error;
            @(posedge clk);
        end
    endtask

    task inject_rx_overrun_error;
        begin
            // Inject overrun error
            force dut.rx_core_overrun_error = 1;
            @(posedge clk);
            #1;
            release dut.rx_core_overrun_error;
            @(posedge clk);
        end
    endtask

    //=========================================================================
    // Main Test Sequence
    //=========================================================================
    logic [31:0] status;
    int i;

    initial begin
        $display("========================================");
        $display("UART Wishbone Interface Error Flag Tests");
        $display("========================================\n");

        // Initialize
        wb_adr = 0;
        wb_dat_i = 0;
        wb_we = 0;
        wb_cyc = 0;
        wb_stb = 0;
        uart_rxd = 1;  // Idle high
        rst = 1;

        // Wait and release reset
        repeat(10) @(posedge clk);
        rst = 0;
        repeat(5) @(posedge clk);

        //=====================================================================
        // TEST 1: Basic TX/RX Functionality (sanity check)
        //=====================================================================
        test_count = test_count + 1;
        $display("========================================");
        $display("TEST 1: Basic TX/RX functionality");
        $display("========================================");

        // Write a byte to TX
        $display("  Writing 0x55 to TX_DATA");
        wb_write(ADDR_TX_DATA, 32'h00000055);

        // Check status - TX should still be ready (FIFO not full)
        wb_read(ADDR_STATUS, status);
        $display("  Status after TX write: 0x%08h", status);
        if (status[BIT_TX_READY]) begin
            $display("  TX_READY=1: PASS");
        end else begin
            test_fail("TX_READY should be 1");
        end

        // Inject RX byte
        $display("  Injecting 0xAA into RX");
        inject_rx_byte(8'hAA);

        // Check status - RX should be ready
        repeat(3) @(posedge clk);
        wb_read(ADDR_STATUS, status);
        $display("  Status after RX inject: 0x%08h", status);
        if (status[BIT_RX_READY]) begin
            $display("  RX_READY=1: PASS");
            test_pass();
        end else begin
            test_fail("RX_READY should be 1");
        end

        //=====================================================================
        // TEST 2: TX FIFO Overflow Error Flag
        //=====================================================================
        test_count = test_count + 1;
        $display("========================================");
        $display("TEST 2: TX FIFO overflow error flag");
        $display("========================================");
        $display("  FIFO depth = 16, will write 20 bytes rapidly");

        // Fill TX FIFO completely (16 bytes) plus extra to trigger overflow
        for (i = 0; i < 20; i = i + 1) begin
            wb_write_no_wait(ADDR_TX_DATA, i);
        end

        // End transaction
        @(posedge clk);
        #1;
        wb_cyc = 0;
        wb_stb = 0;

        // Wait a bit for status to update
        repeat(5) @(posedge clk);

        // Read status - TX_FIFO_OVERFLOW should be set
        wb_read(ADDR_STATUS, status);
        $display("  Status after overflow writes: 0x%08h", status);

        if (status[BIT_TX_FIFO_OVERFLOW]) begin
            $display("  TX_FIFO_OVERFLOW=1: PASS (overflow detected)");
        end else begin
            test_fail("TX_FIFO_OVERFLOW should be 1");
        end

        // Check sticky behavior - read status again (should be cleared)
        wb_read(ADDR_STATUS, status);
        $display("  Status after second read: 0x%08h", status);

        if (!status[BIT_TX_FIFO_OVERFLOW]) begin
            $display("  TX_FIFO_OVERFLOW=0: PASS (cleared on read)");
            test_pass();
        end else begin
            test_fail("TX_FIFO_OVERFLOW should clear on status read");
        end

        // Let TX FIFO drain
        repeat(2000) @(posedge clk);

        //=====================================================================
        // TEST 3: RX FIFO Overflow Error Flag
        //=====================================================================
        test_count = test_count + 1;
        $display("========================================");
        $display("TEST 3: RX FIFO overflow error flag");
        $display("========================================");
        $display("  FIFO depth = 16, will inject 20 bytes rapidly");

        // Inject more bytes than FIFO can hold
        for (i = 0; i < 20; i = i + 1) begin
            inject_rx_byte(8'h40 + i);
            @(posedge clk);  // Small delay between injections
        end

        // Wait for status to update
        repeat(5) @(posedge clk);

        // Read status - RX_FIFO_OVERFLOW should be set
        wb_read(ADDR_STATUS, status);
        $display("  Status after overflow injects: 0x%08h", status);

        if (status[BIT_RX_FIFO_OVERFLOW]) begin
            $display("  RX_FIFO_OVERFLOW=1: PASS (overflow detected)");
        end else begin
            test_fail("RX_FIFO_OVERFLOW should be 1");
        end

        // Check sticky behavior - read status again (should be cleared)
        wb_read(ADDR_STATUS, status);
        $display("  Status after second read: 0x%08h", status);

        if (!status[BIT_RX_FIFO_OVERFLOW]) begin
            $display("  RX_FIFO_OVERFLOW=0: PASS (cleared on read)");
            test_pass();
        end else begin
            test_fail("RX_FIFO_OVERFLOW should clear on status read");
        end

        // Drain RX FIFO
        repeat(20) wb_read(ADDR_RX_DATA, status);

        //=====================================================================
        // TEST 4: Multiple Error Flags Sticky Behavior
        //=====================================================================
        test_count = test_count + 1;
        $display("========================================");
        $display("TEST 4: Multiple error flags at once");
        $display("========================================");

        // Trigger both RX overflow and frame error
        for (i = 0; i < 20; i = i + 1) begin
            inject_rx_byte(8'h60 + i);
        end
        inject_rx_frame_error();

        repeat(5) @(posedge clk);

        // Read status - both should be set
        wb_read(ADDR_STATUS, status);
        $display("  Status after RX overflow + frame error: 0x%08h", status);

        if (status[BIT_RX_FIFO_OVERFLOW] && status[BIT_RX_FRAME_ERROR]) begin
            $display("  Both RX_FIFO_OVERFLOW and RX_FRAME_ERROR set: PASS");
        end else begin
            test_fail("Both error flags should be set");
        end

        // Read status again - both should clear
        wb_read(ADDR_STATUS, status);
        $display("  Status after second read: 0x%08h", status);

        if (!status[BIT_RX_FIFO_OVERFLOW] && !status[BIT_RX_FRAME_ERROR]) begin
            $display("  Both flags cleared: PASS");
            test_pass();
        end else begin
            test_fail("Both flags should clear on status read");
        end

        //=====================================================================
        // TEST 5: Frame Error Flag
        //=====================================================================
        test_count = test_count + 1;
        $display("========================================");
        $display("TEST 5: RX frame error flag");
        $display("========================================");

        // Inject frame error
        inject_rx_frame_error();
        repeat(3) @(posedge clk);

        // Read status
        wb_read(ADDR_STATUS, status);
        $display("  Status after frame error: 0x%08h", status);

        if (status[BIT_RX_FRAME_ERROR]) begin
            $display("  RX_FRAME_ERROR=1: PASS");
        end else begin
            test_fail("RX_FRAME_ERROR should be 1");
        end

        // Clear by reading status
        wb_read(ADDR_STATUS, status);
        if (!status[BIT_RX_FRAME_ERROR]) begin
            $display("  RX_FRAME_ERROR cleared: PASS");
            test_pass();
        end else begin
            test_fail("RX_FRAME_ERROR should clear");
        end

        //=====================================================================
        // TEST 6: Overrun Error Flag
        //=====================================================================
        test_count = test_count + 1;
        $display("========================================");
        $display("TEST 6: RX overrun error flag");
        $display("========================================");

        // Inject overrun error
        inject_rx_overrun_error();
        repeat(3) @(posedge clk);

        // Read status
        wb_read(ADDR_STATUS, status);
        $display("  Status after overrun error: 0x%08h", status);

        if (status[BIT_RX_OVERRUN_ERROR]) begin
            $display("  RX_OVERRUN_ERROR=1: PASS");
        end else begin
            test_fail("RX_OVERRUN_ERROR should be 1");
        end

        // Clear by reading status
        wb_read(ADDR_STATUS, status);
        if (!status[BIT_RX_OVERRUN_ERROR]) begin
            $display("  RX_OVERRUN_ERROR cleared: PASS");
            test_pass();
        end else begin
            test_fail("RX_OVERRUN_ERROR should clear");
        end

        //=====================================================================
        // Test Summary
        //=====================================================================
        $display("========================================");
        $display("UART Error Flag Tests Complete");
        $display("========================================");
        $display("Tests Run:    %0d", test_count);
        $display("Tests Passed: %0d", pass_count);
        $display("Tests Failed: %0d", fail_count);

        if (fail_count == 0) begin
            $display("*** ALL TESTS PASSED ***");
        end else begin
            $display("*** SOME TESTS FAILED ***");
        end
        $display("========================================");

        $finish;
    end

    //=========================================================================
    // Final PASS/FAIL Report
    //=========================================================================
    final begin
        if (timeout_occurred)
            $display("FAIL: %s (timeout)", TEST_NAME);
        else if (error_count == 0)
            $display("PASS: %s", TEST_NAME);
        else
            $display("FAIL: %s (%0d errors)", TEST_NAME, error_count);
    end

endmodule

`default_nettype wire
