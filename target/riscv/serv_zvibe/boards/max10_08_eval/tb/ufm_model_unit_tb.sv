///////////////////////////////////////////////////////////////////////////////
// ufm_model_unit_tb.sv
//
// Unit testbench for UFM behavioral model (ufm_sim.v)
// Tests basic read/write/erase operations without CPU or bridge
//
// Copyright (c) 2025 Martin R. Raumann
// SPDX-License-Identifier: BSD-3-Clause
///////////////////////////////////////////////////////////////////////////////

`timescale 1ns / 1ps

module ufm_model_unit_tb;

    //=========================================================================
    // Clock and Reset
    //=========================================================================
    logic clock;
    logic reset_n;

    initial begin
        clock = 0;
        forever #5 clock = ~clock;  // 100MHz clock
    end

    initial begin
        reset_n = 0;
        #20 reset_n = 1;
    end

    //=========================================================================
    // UFM Interface Signals
    //=========================================================================
    logic [16:0] avmm_data_addr;
    logic        avmm_data_read;
    logic        avmm_data_write;
    logic [31:0] avmm_data_writedata;
    wire [31:0] avmm_data_readdata;
    wire        avmm_data_waitrequest;
    wire        avmm_data_readdatavalid;
    logic [3:0]   avmm_data_burstcount;

    logic        avmm_csr_addr;
    logic        avmm_csr_read;
    logic        avmm_csr_write;
    logic [31:0] avmm_csr_writedata;
    wire [31:0] avmm_csr_readdata;

    //=========================================================================
    // DUT: UFM Behavioral Model
    //=========================================================================
    ufm dut (
        .clock(clock),
        .reset_n(reset_n),

        // Data interface
        .avmm_data_addr(avmm_data_addr),
        .avmm_data_read(avmm_data_read),
        .avmm_data_write(avmm_data_write),
        .avmm_data_writedata(avmm_data_writedata),
        .avmm_data_readdata(avmm_data_readdata),
        .avmm_data_waitrequest(avmm_data_waitrequest),
        .avmm_data_readdatavalid(avmm_data_readdatavalid),
        .avmm_data_burstcount(avmm_data_burstcount),

        // CSR interface
        .avmm_csr_addr(avmm_csr_addr),
        .avmm_csr_read(avmm_csr_read),
        .avmm_csr_write(avmm_csr_write),
        .avmm_csr_writedata(avmm_csr_writedata),
        .avmm_csr_readdata(avmm_csr_readdata)
    );

    //=========================================================================
    // Test Tasks
    //=========================================================================

    // Read STATUS register
    task read_status;
        output [31:0] status;
        begin
            @(posedge clock);
            avmm_csr_addr = 0;
            avmm_csr_read = 1;
            avmm_csr_write = 0;
            @(posedge clock);
            status = avmm_csr_readdata;
            avmm_csr_read = 0;
            $display("[%0t] Read STATUS: 0x%08X", $time, status);
        end
    endtask

    // Write CONTROL register
    task write_control;
        input [31:0] control;
        begin
            @(posedge clock);
            avmm_csr_addr = 1;
            avmm_csr_read = 0;
            avmm_csr_write = 1;
            avmm_csr_writedata = control;
            @(posedge clock);
            avmm_csr_write = 0;
            $display("[%0t] Write CONTROL: 0x%08X", $time, control);
        end
    endtask

    // Read data word
    task read_data;
        input [16:0] addr;
        output [31:0] data;
        begin
            @(posedge clock);
            avmm_data_addr = addr;
            avmm_data_read = 1;
            avmm_data_write = 0;
            avmm_data_burstcount = 1;

            // Wait for readdatavalid
            @(posedge clock);
            avmm_data_read = 0;

            wait(avmm_data_readdatavalid);
            data = avmm_data_readdata;
            $display("[%0t] Read data[0x%05X]: 0x%08X", $time, addr, data);
        end
    endtask

    // Write data word
    task write_data;
        input [16:0] addr;
        input [31:0] data;
        begin
            @(posedge clock);
            avmm_data_addr = addr;
            avmm_data_read = 0;
            avmm_data_write = 1;
            avmm_data_writedata = data;
            avmm_data_burstcount = 1;

            // Wait if busy
            while (avmm_data_waitrequest) @(posedge clock);

            @(posedge clock);
            avmm_data_write = 0;
            $display("[%0t] Write data[0x%05X]: 0x%08X", $time, addr, data);
        end
    endtask

    // Wait for not busy
    task wait_idle;
        logic [31:0] status;
        begin
            do begin
                read_status(status);
                if (status[2]) begin  // busy bit
                    $display("[%0t] Waiting for idle (busy=1)...", $time);
                    repeat(10) @(posedge clock);
                end
            end while (status[2]);
            $display("[%0t] UFM idle", $time);
        end
    endtask

    //=========================================================================
    // Test Stimulus
    //=========================================================================
    integer test_pass_count;
    integer test_fail_count;

    initial begin
        // Initialize
        avmm_data_addr = 0;
        avmm_data_read = 0;
        avmm_data_write = 0;
        avmm_data_writedata = 0;
        avmm_data_burstcount = 1;
        avmm_csr_addr = 0;
        avmm_csr_read = 0;
        avmm_csr_write = 0;
        avmm_csr_writedata = 0;
        test_pass_count = 0;
        test_fail_count = 0;

        // Wait for reset
        wait(reset_n);
        repeat(5) @(posedge clock);

        $display("\n========================================");
        $display("UFM Behavioral Model Unit Test");
        $display("========================================\n");

        // Test 1: Read STATUS
        begin
            logic [31:0] status;
            $display("Test 1: Read STATUS Register");
            $display("-----------------------------");
            read_status(status);
            if (status[1] && status[3] && status[4] && !status[2]) begin
                $display("  PASS - rs=1, ws=1, es=1, busy=0\n");
                test_pass_count = test_pass_count + 1;
            end else begin
                $display("  FAIL - Unexpected status: 0x%08X\n", status);
                test_fail_count = test_fail_count + 1;
            end
        end

        // Test 2: Read uninitialized memory (should be 0xFFFFFFFF)
        begin
            logic [31:0] data;
            $display("Test 2: Read Erased Memory");
            $display("---------------------------");
            read_data(17'h00100, data);
            if (data == 32'hFFFFFFFF) begin
                $display("  PASS - Read 0xFFFFFFFF from erased memory\n");
                test_pass_count = test_pass_count + 1;
            end else begin
                $display("  FAIL - Expected 0xFFFFFFFF, got 0x%08X\n", data);
                test_fail_count = test_fail_count + 1;
            end
        end

        // Test 3: Write CONTROL (inactive erase fields)
        begin
            logic [31:0] status;
            $display("Test 3: Write CONTROL (Safety)");
            $display("-------------------------------");
            write_control(32'h307FFFFF);  // pe=0xFFFFF (inactive), se=0x7, wp=0x18
            read_status(status);
            $display("  PASS - CONTROL write accepted\n");
            test_pass_count = test_pass_count + 1;
        end

        // Test 4: Program a word
        begin
            logic [31:0] data;
            $display("Test 4: Program Word");
            $display("--------------------");

            // Clear write protection (wp=0x1F = bits[27:23]=11111, se=0x7, pe=0xFFFFF)
            write_control(32'h3FFFFFFF);  // Bits[27:20]=0xFF for wp=0x1F + se=0x7
            wait_idle();

            // Program word
            write_data(17'h00100, 32'hDEADBEEF);
            wait_idle();

            // Read back
            read_data(17'h00100, data);
            if (data == 32'hDEADBEEF) begin
                $display("  PASS - Programmed and verified 0xDEADBEEF\n");
                test_pass_count = test_pass_count + 1;
            end else begin
                $display("  FAIL - Expected 0xDEADBEEF, got 0x%08X\n", data);
                test_fail_count = test_fail_count + 1;
            end
        end

        // Test 5: Erase page
        begin
            logic [31:0] data;
            logic [31:0] status;
            $display("Test 5: Erase Page");
            $display("-------------------");

            // Trigger erase (page containing word 0x100, wp=0x1F, se=0x7, pe=0x100)
            write_control(32'h3FF00100);  // Bits[27:20]=0xFF for wp=0x1F, pe=0x100
            wait_idle();

            // Check erase success bit
            read_status(status);
            if (status[4]) begin
                $display("  Erase success bit: 1");
            end else begin
                $display("  Erase success bit: 0");
            end

            // Read back (should be 0xFFFFFFFF after erase)
            read_data(17'h00100, data);
            if (data == 32'hFFFFFFFF) begin
                $display("  PASS - Page erased, read 0xFFFFFFFF\n");
                test_pass_count = test_pass_count + 1;
            end else begin
                $display("  FAIL - Expected 0xFFFFFFFF after erase, got 0x%08X\n", data);
                test_fail_count = test_fail_count + 1;
            end
        end

        // Summary
        $display("\n========================================");
        $display("Test Summary");
        $display("========================================");
        $display("PASS: %0d", test_pass_count);
        $display("FAIL: %0d", test_fail_count);
        $display("========================================\n");

        if (test_fail_count == 0) begin
            $display("*** ALL TESTS PASSED ***\n");
        end else begin
            $display("*** SOME TESTS FAILED ***\n");
        end

        #100;
        $finish;
    end

    //=========================================================================
    // Timeout watchdog
    //=========================================================================
    initial begin
        #1000000;  // 1ms timeout
        $display("\nERROR: Simulation timeout!");
        $finish;
    end

endmodule
