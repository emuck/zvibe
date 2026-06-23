// Copyright (c) 2025 Martin R. Raumann
// SPDX-License-Identifier: BSD-3-Clause

//=============================================================================
// s25fl_rw_tb.sv
//
// Test-Driven Development (TDD) Testbench for S25FL Flash Read/Write Model
//
// Test Strategy:
//   1. Test RDSR (Read Status Register) - simplest output command
//   2. Test WREN (Write Enable) - sets WEL bit in status
//   3. Test Sector Erase (SE) - erases 64KB sector to 0xFF
//   4. Test Page Program (PP) - programs up to 256 bytes
//   5. Test error conditions (no WREN, invalid addresses, etc.)
//
//=============================================================================

`timescale 1ns/1ps
`default_nettype none

module s25fl_rw_tb;

    //=========================================================================
    // DUT Signals
    //=========================================================================

    logic        clk;
    logic        sck;
    logic        cs_n;
    logic        rst_n;
    wire         si, so, wp_n, hold_n;  // tri-state — must stay wire

    // Bidirectional control
    logic [3:0]  io_out;
    logic [3:0]  io_oe;
    wire  [3:0]  io_in;  // driven by tri-state assigns

    // Tristate drivers
    assign si     = io_oe[0] ? io_out[0] : 1'bz;
    assign so     = io_oe[1] ? io_out[1] : 1'bz;
    assign wp_n   = io_oe[2] ? io_out[2] : 1'bz;
    assign hold_n = io_oe[3] ? io_out[3] : 1'bz;

    assign io_in[0] = si;
    assign io_in[1] = so;
    assign io_in[2] = wp_n;
    assign io_in[3] = hold_n;

    //=========================================================================
    // DUT Instantiation
    //=========================================================================

    s25fl_simple_rw #(
        .POWERUP_TIME(100),        // Fast power-up for testing
        .MEM_SIZE(24'hFFFFFF),     // 16MB
        .MEM_FILE("none"),
        .DEBUG(1),                 // Enable debug output
        .ERASE_TIME_NS(500),       // Very fast for unit tests (500ns vs 500ms)
        .PROGRAM_TIME_NS(30),      // Very fast for unit tests (30ns vs 3ms)
        .FAST_SIM(1)
    ) dut (
        .SCK(sck),
        .CSNeg(cs_n),
        .RSTNeg(rst_n),
        .SI(si),
        .SO(so),
        .WPNeg(wp_n),
        .HOLDNeg(hold_n)
    );

    //=========================================================================
    // Clock Generation
    //=========================================================================

    initial begin
        clk = 0;
        forever #5 clk = ~clk;  // 100MHz system clock
    end

    // SPI clock is controlled by test tasks
    initial sck = 0;

    //=========================================================================
    // Test Statistics
    //=========================================================================

    integer test_count;
    integer test_passed;
    integer test_failed;

    //=========================================================================
    // Helper Tasks
    //=========================================================================

    // Send SPI byte (Mode 0: CPOL=0, CPHA=0)
    task spi_send_byte;
        input [7:0] data;
        integer i;
        begin
            for (i = 7; i >= 0; i = i - 1) begin
                io_out[0] = data[i];  // Output on MOSI (SI)
                #10 sck = 1;          // Clock high
                #10 sck = 0;          // Clock low
            end
        end
    endtask

    // Receive SPI byte
    task spi_recv_byte;
        output [7:0] data;
        integer i;
        begin
            data = 8'h00;
            for (i = 7; i >= 0; i = i - 1) begin
                #10 sck = 1;          // Clock high
                data[i] = io_in[1];   // Sample on MISO (SO)
                $display("[TB] Sampled bit %0d: %b (data so far: 0x%02h)", 7-i, io_in[1], data);
                #10 sck = 0;          // Clock low
            end
        end
    endtask

    // Send 24-bit address
    task spi_send_addr;
        input [23:0] addr;
        begin
            spi_send_byte(addr[23:16]);
            spi_send_byte(addr[15:8]);
            spi_send_byte(addr[7:0]);
        end
    endtask

    // Assert chip select
    task cs_assert;
        begin
            cs_n = 0;
            #20;  // Setup time
        end
    endtask

    // Deassert chip select
    task cs_deassert;
        begin
            #20;  // Hold time
            cs_n = 1;
            #50;  // Recovery time
        end
    endtask

    //=========================================================================
    // Test Framework
    //=========================================================================

    task test_start;
        input [255:0] test_name;
        begin
            test_count = test_count + 1;
            $display("\n========================================");
            $display("TEST %0d: %s", test_count, test_name);
            $display("========================================");
        end
    endtask

    task test_pass;
        begin
            test_passed = test_passed + 1;
            $display("✓ PASS");
        end
    endtask

    task test_fail;
        input [255:0] reason;
        begin
            test_failed = test_failed + 1;
            $display("✗ FAIL: %s", reason);
        end
    endtask

    //=========================================================================
    // TEST 1: Read Status Register (RDSR)
    //=========================================================================

    task test_rdsr;
        logic [7:0] status;
        begin
            test_start("Read Status Register (RDSR)");

            // Send RDSR command (0x05)
            io_oe = 4'b0001;  // MOSI as output
            cs_assert();
            spi_send_byte(8'h05);  // RDSR command

            // Switch to input mode to read status
            io_oe = 4'b0000;  // All inputs
            #5;  // Small delay to let flash transition to output mode
            spi_recv_byte(status);
            cs_deassert();

            $display("Status register: 0x%02h", status);
            $display("  WIP (bit 0): %b", status[0]);
            $display("  WEL (bit 1): %b", status[1]);

            // Initial status should be 0x00 (not busy, not write-enabled)
            if (status == 8'h00) begin
                test_pass();
            end else begin
                test_fail("Expected status 0x00, got 0x%02h");
            end
        end
    endtask

    //=========================================================================
    // TEST 2: Write Enable (WREN) Sets WEL Bit
    //=========================================================================

    task test_wren;
        logic [7:0] status;
        begin
            test_start("Write Enable (WREN) Sets WEL Bit");

            // Send WREN command
            io_oe = 4'b0001;
            cs_assert();
            spi_send_byte(8'h06);  // WREN command
            cs_deassert();

            // Read status to verify WEL bit is set
            cs_assert();
            spi_send_byte(8'h05);  // RDSR command
            io_oe = 4'b0000;
            spi_recv_byte(status);
            cs_deassert();

            $display("Status after WREN: 0x%02h", status);
            $display("  WEL (bit 1): %b", status[1]);

            if (status[1] == 1'b1) begin
                test_pass();
            end else begin
                test_fail("WEL bit not set after WREN");
            end
        end
    endtask

    //=========================================================================
    // TEST 3: Sector Erase (SE) Without WREN Should Fail
    //=========================================================================

    task test_se_no_wren;
        logic [7:0] status;
        begin
            test_start("Sector Erase Without WREN (Should Be Ignored)");

            // Clear WEL first (WEL may persist from previous test)
            // WRDI (Write Disable) command = 0x04
            io_oe = 4'b0001;
            cs_assert();
            spi_send_byte(8'h04);  // WRDI command
            cs_deassert();

            // Try to erase without WREN
            cs_assert();
            spi_send_byte(8'hD8);  // SE command
            spi_send_addr(24'h000000);  // Address 0x000000
            cs_deassert();

            // Check status - should not be busy
            cs_assert();
            spi_send_byte(8'h05);  // RDSR
            io_oe = 4'b0000;
            spi_recv_byte(status);
            cs_deassert();

            $display("Status after SE without WREN: 0x%02h", status);

            if (status[0] == 1'b0) begin  // WIP should be 0 (not busy)
                test_pass();
            end else begin
                test_fail("Erase started without WREN!");
            end
        end
    endtask

    //=========================================================================
    // TEST 4: Sector Erase with WREN
    //=========================================================================

    task test_se_with_wren;
        logic [7:0] status;
        logic [7:0] data;
        integer timeout;
        begin
            test_start("Sector Erase (SE) With WREN");

            // Write a test pattern to sector 0
            $display("Writing test pattern 0xAA to address 0x00000000");
            dut.write_mem(24'h000000, 8'hAA);
            dut.write_mem(24'h000001, 8'hBB);
            dut.write_mem(24'h00FFFF, 8'hCC);  // Last byte of 64KB sector

            // Send WREN
            io_oe = 4'b0001;
            cs_assert();
            spi_send_byte(8'h06);  // WREN
            cs_deassert();

            // Send SE command
            cs_assert();
            spi_send_byte(8'hD8);  // SE command
            spi_send_addr(24'h000000);  // Erase sector 0
            cs_deassert();

            // Poll status until not busy
            $display("Polling status register for completion...");
            timeout = 0;
            status[0] = 1'b1;  // Assume busy initially
            while (status[0] && timeout < 1000) begin
                #100;  // Wait a bit
                cs_assert();
                spi_send_byte(8'h05);  // RDSR
                io_oe = 4'b0000;
                spi_recv_byte(status);
                cs_deassert();
                timeout = timeout + 1;
            end

            if (timeout >= 1000) begin
                test_fail("Timeout waiting for erase completion");
            end else begin
                $display("Erase completed after %0d polls", timeout);

                // Verify sector was erased (all 0xFF)
                data = dut.Mem[24'h000000];
                if (data == 8'hFF) begin
                    data = dut.Mem[24'h000001];
                    if (data == 8'hFF) begin
                        data = dut.Mem[24'h00FFFF];
                        if (data == 8'hFF) begin
                            test_pass();
                        end else begin
                            test_fail("Last byte not erased");
                        end
                    end else begin
                        test_fail("Second byte not erased");
                    end
                end else begin
                    test_fail("First byte not erased");
                end
            end
        end
    endtask

    //=========================================================================
    // TEST 5: Page Program Without WREN Should Fail
    //=========================================================================

    task test_pp_no_wren;
        logic [7:0] status;
        begin
            test_start("Page Program Without WREN (Should Be Ignored)");

            // Try to program without WREN
            io_oe = 4'b0001;
            cs_assert();
            spi_send_byte(8'h02);  // PP command
            spi_send_addr(24'h000000);
            spi_send_byte(8'h55);  // Data byte
            cs_deassert();

            // Check status - should not be busy
            cs_assert();
            spi_send_byte(8'h05);  // RDSR
            io_oe = 4'b0000;
            spi_recv_byte(status);
            cs_deassert();

            $display("Status after PP without WREN: 0x%02h", status);

            if (status[0] == 1'b0) begin  // WIP should be 0
                test_pass();
            end else begin
                test_fail("Program started without WREN!");
            end
        end
    endtask

    //=========================================================================
    // TEST 6: Page Program With WREN
    //=========================================================================

    task test_pp_with_wren;
        logic [7:0] status;
        logic [7:0] data;
        integer timeout;
        integer i;
        begin
            test_start("Page Program (PP) With WREN");

            // First erase the sector (required before programming)
            io_oe = 4'b0001;
            cs_assert();
            spi_send_byte(8'h06);  // WREN
            cs_deassert();

            cs_assert();
            spi_send_byte(8'hD8);  // SE
            spi_send_addr(24'h000000);
            cs_deassert();

            // Wait for erase
            #1000;

            // Now program some data
            $display("Programming 4 bytes: 0x12, 0x34, 0x56, 0x78");
            cs_assert();
            spi_send_byte(8'h06);  // WREN
            cs_deassert();

            cs_assert();
            spi_send_byte(8'h02);  // PP command
            spi_send_addr(24'h000100);  // Address 0x000100
            spi_send_byte(8'h12);
            spi_send_byte(8'h34);
            spi_send_byte(8'h56);
            spi_send_byte(8'h78);
            cs_deassert();

            // Poll for completion
            timeout = 0;
            status[0] = 1'b1;
            while (status[0] && timeout < 100) begin
                #50;
                cs_assert();
                spi_send_byte(8'h05);  // RDSR
                io_oe = 4'b0000;
                spi_recv_byte(status);
                cs_deassert();
                io_oe = 4'b0001;
                timeout = timeout + 1;
            end

            if (timeout >= 100) begin
                test_fail("Timeout waiting for program completion");
            end else begin
                $display("Program completed after %0d polls", timeout);

                // Verify programmed data
                if (dut.Mem[24'h000100] == 8'h12 &&
                    dut.Mem[24'h000101] == 8'h34 &&
                    dut.Mem[24'h000102] == 8'h56 &&
                    dut.Mem[24'h000103] == 8'h78) begin
                    test_pass();
                end else begin
                    $display("Read back: 0x%02h 0x%02h 0x%02h 0x%02h",
                             dut.Mem[24'h000100], dut.Mem[24'h000101],
                             dut.Mem[24'h000102], dut.Mem[24'h000103]);
                    test_fail("Programmed data mismatch");
                end
            end
        end
    endtask

    //=========================================================================
    // Main Test Sequence
    //=========================================================================

    initial begin
        $dumpfile("s25fl_rw_tb.vcd");
        $dumpvars(0, s25fl_rw_tb);

        // Initialize
        test_count = 0;
        test_passed = 0;
        test_failed = 0;

        cs_n = 1;
        rst_n = 0;
        sck = 0;
        io_out = 4'h0;
        io_oe = 4'h0;

        // Wait for power-up
        #200;
        rst_n = 1;
        #200;

        $display("\n");
        $display("================================================================================");
        $display(" S25FL Simple R/W Model - TDD Test Suite");
        $display("================================================================================");

        // Run tests in order
        test_rdsr();
        test_wren();
        test_se_no_wren();
        test_se_with_wren();
        test_pp_no_wren();
        test_pp_with_wren();

        // Summary
        $display("\n");
        $display("================================================================================");
        $display(" Test Summary");
        $display("================================================================================");
        $display("Total:  %0d tests", test_count);
        $display("Passed: %0d tests", test_passed);
        $display("Failed: %0d tests", test_failed);

        if (test_failed == 0) begin
            $display("\n✓ ALL TESTS PASSED!");
        end else begin
            $display("\n✗ SOME TESTS FAILED!");
        end
        $display("================================================================================");

        $finish;
    end

endmodule

`default_nettype wire
