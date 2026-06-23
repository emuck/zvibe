///////////////////////////////////////////////////////////////////////////////
// max10_ufm_unified_tb.sv
//
// Unit testbench for max10_ufm_unified (Wishbone→Avalon-MM bridge, Verilator)
// Tests all seven WB transaction types against the behavioral UFM model.
//
// Copyright (c) 2025 Martin R. Raumann
// SPDX-License-Identifier: BSD-3-Clause
///////////////////////////////////////////////////////////////////////////////

`timescale 1ns / 1ps

module max10_ufm_unified_tb;

    //=========================================================================
    // Clock, Reset
    //=========================================================================
    logic clk;
    logic rst;

    initial clk = 0;
    always #5 clk = ~clk;  // 100 MHz

    // UFM model uses active-low reset
    logic ufm_reset_n;
    assign ufm_reset_n = !rst;

    //=========================================================================
    // Wishbone master signals
    //=========================================================================
    logic        wb_cyc;
    logic        wb_stb;
    logic [31:0] wb_addr;
    logic [31:0] wb_wdata;
    logic [3:0]  wb_sel;
    logic        wb_we;
    logic [31:0] wb_rdata;
    logic        wb_ack;
    logic        wb_stall;

    //=========================================================================
    // Avalon-MM (DUT → UFM model)
    //=========================================================================
    // CSR interface
    logic        avmm_csr_addr;
    logic        avmm_csr_read;
    logic        avmm_csr_write;
    logic [31:0] avmm_csr_writedata;
    logic [31:0] avmm_csr_readdata;

    // Data interface
    logic [16:0] avmm_data_addr;
    logic        avmm_data_read;
    logic        avmm_data_write;
    logic [31:0] avmm_data_writedata;
    logic [3:0]  avmm_data_burstcount;
    logic [31:0] avmm_data_readdata;
    logic        avmm_data_waitrequest;
    logic        avmm_data_readdatavalid;

    //=========================================================================
    // DUT: max10_ufm_unified
    //=========================================================================
    max10_ufm_unified #(
        .UFM_ADDR_WIDTH(17),
        .DATA_WIDTH(32)
    ) dut (
        .i_clk              (clk),
        .i_reset            (rst),

        // Wishbone slave
        .i_wb_cyc           (wb_cyc),
        .i_wb_stb           (wb_stb),
        .i_wb_addr          (wb_addr),
        .i_wb_data          (wb_wdata),
        .i_wb_sel           (wb_sel),
        .i_wb_we            (wb_we),
        .o_wb_data          (wb_rdata),
        .o_wb_ack           (wb_ack),
        .o_wb_stall         (wb_stall),

        // Avalon-MM CSR
        .o_avmm_csr_addr    (avmm_csr_addr),
        .o_avmm_csr_read    (avmm_csr_read),
        .o_avmm_csr_write   (avmm_csr_write),
        .o_avmm_csr_writedata(avmm_csr_writedata),
        .i_avmm_csr_readdata(avmm_csr_readdata),

        // Avalon-MM Data
        .o_avmm_data_addr       (avmm_data_addr),
        .o_avmm_data_read       (avmm_data_read),
        .o_avmm_data_write      (avmm_data_write),
        .o_avmm_data_writedata  (avmm_data_writedata),
        .o_avmm_data_burstcount (avmm_data_burstcount),
        .i_avmm_data_readdata   (avmm_data_readdata),
        .i_avmm_data_waitrequest(avmm_data_waitrequest),
        .i_avmm_data_readdatavalid(avmm_data_readdatavalid)
    );

    //=========================================================================
    // UFM behavioral model
    //=========================================================================
    ufm dut_ufm (
        .clock                  (clk),
        .reset_n                (ufm_reset_n),

        .avmm_data_addr         (avmm_data_addr),
        .avmm_data_read         (avmm_data_read),
        .avmm_data_write        (avmm_data_write),
        .avmm_data_writedata    (avmm_data_writedata),
        .avmm_data_readdata     (avmm_data_readdata),
        .avmm_data_waitrequest  (avmm_data_waitrequest),
        .avmm_data_readdatavalid(avmm_data_readdatavalid),
        .avmm_data_burstcount   (avmm_data_burstcount),

        .avmm_csr_addr          (avmm_csr_addr),
        .avmm_csr_read          (avmm_csr_read),
        .avmm_csr_write         (avmm_csr_write),
        .avmm_csr_writedata     (avmm_csr_writedata),
        .avmm_csr_readdata      (avmm_csr_readdata)
    );

    //=========================================================================
    // Pass/fail counters
    //=========================================================================
    int pass_cnt;
    int fail_cnt;

    task automatic check(input logic ok, input string label);
        if (ok) begin
            $display("  PASS: %s", label);
            pass_cnt++;
        end else begin
            $display("  FAIL: %s", label);
            fail_cnt++;
        end
    endtask

    //=========================================================================
    // WB helper tasks
    //=========================================================================

    // Issue a single WB write; waits for stall=0 before presenting request,
    // then waits for ack.
    task automatic wb_write(input logic [31:0] addr, input logic [31:0] data);
        // Wait until bus is not stalled
        while (wb_stall) @(posedge clk);
        @(posedge clk);
        wb_cyc   <= 1'b1;
        wb_stb   <= 1'b1;
        wb_addr  <= addr;
        wb_wdata <= data;
        wb_sel   <= 4'hF;
        wb_we    <= 1'b1;
        // Deassert stb after one cycle (classic WB), but keep cyc until ack
        @(posedge clk);
        wb_stb <= 1'b0;
        // Wait for ack
        while (!wb_ack) @(posedge clk);
        wb_cyc <= 1'b0;
        wb_we  <= 1'b0;
        @(posedge clk);
    endtask

    // Issue a single WB read; returns data via output argument.
    task automatic wb_read(input logic [31:0] addr, output logic [31:0] data);
        while (wb_stall) @(posedge clk);
        @(posedge clk);
        wb_cyc  <= 1'b1;
        wb_stb  <= 1'b1;
        wb_addr <= addr;
        wb_sel  <= 4'hF;
        wb_we   <= 1'b0;
        @(posedge clk);
        wb_stb <= 1'b0;
        while (!wb_ack) @(posedge clk);
        data   = wb_rdata;
        wb_cyc <= 1'b0;
        @(posedge clk);
    endtask

    // Wait until avmm_data_waitrequest deasserts (UFM erase/program complete).
    task automatic wait_not_stall;
        while (wb_stall) @(posedge clk);
    endtask

    //=========================================================================
    // Timeout watchdog
    //=========================================================================
    localparam MAX_CYCLES = 10_000;
    int cycle_cnt;

    initial begin
        cycle_cnt = 0;
        forever begin
            @(posedge clk);
            cycle_cnt++;
            if (cycle_cnt >= MAX_CYCLES) begin
                $display("ERROR: simulation timeout at cycle %0d", cycle_cnt);
                $display("FAIL: max10_ufm_unified_tb (timeout)");
                $finish;
            end
        end
    end

    //=========================================================================
    // Test stimulus
    //=========================================================================
    initial begin
        // Initialise WB bus
        wb_cyc   = 1'b0;
        wb_stb   = 1'b0;
        wb_addr  = 32'h0;
        wb_wdata = 32'h0;
        wb_sel   = 4'hF;
        wb_we    = 1'b0;
        pass_cnt = 0;
        fail_cnt = 0;

        // Reset: active-high for 5 cycles
        rst = 1'b1;
        repeat (5) @(posedge clk);
        rst = 1'b0;
        repeat (3) @(posedge clk);

        $display("\n================================================");
        $display(" max10_ufm_unified_tb — Wishbone Bridge Tests");
        $display("================================================\n");

        // -----------------------------------------------------------------
        // Test 1: XIP read
        //   Preload memory word at word address 0x0040 (= byte 0x80000100)
        //   WB read 0x80000100 → expect 0xDEADBEEF
        // -----------------------------------------------------------------
        begin
            logic [31:0] rdata;
            $display("Test 1: XIP read (WB 0x80000100)");
            dut_ufm.memory[17'h0040] = 32'hDEADBEEF;
            wb_read(32'h80000100, rdata);
            check(rdata === 32'hDEADBEEF, "XIP read 0x80000100 == 0xDEADBEEF");
        end

        // -----------------------------------------------------------------
        // Test 2: CSR STATUS read
        //   WB read 0x80040000 → expect 0x0000001A (rs=1, ws=1, es=1, busy=0)
        // -----------------------------------------------------------------
        begin
            logic [31:0] rdata;
            $display("\nTest 2: CSR STATUS read (WB 0x80040000)");
            wb_read(32'h80040000, rdata);
            check(rdata === 32'h0000001A, $sformatf("STATUS == 0x1A (got 0x%08X)", rdata));
        end

        // -----------------------------------------------------------------
        // Test 3: WRITE_ADDR round-trip
        //   Write 0x1234 to WRITE_ADDR, read back — expect 0x0001234
        // -----------------------------------------------------------------
        begin
            logic [31:0] rdata;
            $display("\nTest 3: WRITE_ADDR round-trip");
            wb_write(32'h80040008, 32'h0001234);
            wb_read(32'h80040008, rdata);
            check(rdata[16:0] === 17'h1234, $sformatf("WRITE_ADDR readback [16:0] == 0x1234 (got 0x%05X)", rdata[16:0]));
        end

        // -----------------------------------------------------------------
        // Test 4: UFM program + verify
        //   WRITE_ADDR = 0x0080 (word addr), WRITE_DATA = 0xCAFEBABE
        //   Wait stall=0, XIP-read 0x80000200 → expect 0xCAFEBABE
        //   (word addr 0x0080 == byte addr 0x200, WB 0x80000200)
        // -----------------------------------------------------------------
        begin
            logic [31:0] rdata;
            $display("\nTest 4: UFM program + verify");
            wb_write(32'h80040008, 32'h00000080);   // WRITE_ADDR = word 0x0080
            wb_write(32'h8004000C, 32'hCAFEBABE);   // WRITE_DATA → triggers program
            wait_not_stall;                           // wait for program to complete
            repeat (2) @(posedge clk);               // settle
            wb_read(32'h80000200, rdata);             // XIP read word 0x0080
            check(rdata === 32'hCAFEBABE, $sformatf("XIP read after program == 0xCAFEBABE (got 0x%08X)", rdata));
        end

        // -----------------------------------------------------------------
        // Test 5: UFM erase + verify
        //   Write CONTROL = 0x0FF00000 (page 0, all-writable, pe=0x00000)
        //   Wait stall=0 (~100 cycles), XIP-read 0x80000000 → expect 0xFFFFFFFF
        // -----------------------------------------------------------------
        begin
            logic [31:0] rdata;
            $display("\nTest 5: UFM erase + verify");
            // First plant a known value at word 0 so erase is observable
            dut_ufm.memory[17'h0000] = 32'hA5A5A5A5;
            // CONTROL: wp=5'h1F, se=3'h7, pe=20'h00000
            // Bit layout: [31:28]=0, [27:23]=wp, [22:20]=se, [19:0]=pe
            // 0b0_11111_111_00000000000000000000 = 32'h0FF00000
            wb_write(32'h80040004, 32'h0FF00000);
            wait_not_stall;                           // wait for erase (~100 cycles)
            repeat (2) @(posedge clk);
            wb_read(32'h80000000, rdata);
            check(rdata === 32'hFFFFFFFF, $sformatf("XIP read after erase == 0xFFFFFFFF (got 0x%08X)", rdata));
        end

        // -----------------------------------------------------------------
        // Test 6: Address boundary decode
        //   0x8003FFFC → last word of XIP region → avmm_data_read must fire
        //   0x80040000 → first CSR address → avmm_csr_read must fire
        //   (verified by side-effects: XIP returns erased=0xFFFFFFFF,
        //    CSR returns STATUS)
        // -----------------------------------------------------------------
        begin
            logic [31:0] rdata_xip, rdata_csr;
            $display("\nTest 6: Address boundary decode");
            // XIP boundary: 0x8003FFFC → addr[19:18]=2'b00
            dut_ufm.memory[17'h0FFFF] = 32'hBEEFCAFE;
            wb_read(32'h8003FFFC, rdata_xip);
            check(rdata_xip === 32'hBEEFCAFE, $sformatf("0x8003FFFC → XIP (got 0x%08X)", rdata_xip));
            // CSR boundary: 0x80040000 → addr[19:18]=2'b01
            wb_read(32'h80040000, rdata_csr);
            // STATUS bits[4:1] should be set (rs/ws/es=1, busy=0 → 0x1A)
            check((rdata_csr & 32'h0000001E) !== 0, $sformatf("0x80040000 → CSR STATUS (got 0x%08X)", rdata_csr));
        end

        // -----------------------------------------------------------------
        // Test 7: DEADBEEF default (WRITE_DATA read)
        //   WB read 0x8004000C → expect 0xDEADBEEF (hardcoded default)
        // -----------------------------------------------------------------
        begin
            logic [31:0] rdata;
            $display("\nTest 7: WRITE_DATA read returns 0xDEADBEEF");
            wb_read(32'h8004000C, rdata);
            check(rdata === 32'hDEADBEEF, $sformatf("WRITE_DATA read == 0xDEADBEEF (got 0x%08X)", rdata));
        end

        // -----------------------------------------------------------------
        // Summary
        // -----------------------------------------------------------------
        $display("\n================================================");
        $display(" Results: %0d passed, %0d failed", pass_cnt, fail_cnt);
        $display("================================================");
        if (fail_cnt == 0)
            $display("PASS: max10_ufm_unified_tb");
        else
            $display("FAIL: max10_ufm_unified_tb (%0d test(s) failed)", fail_cnt);

        $finish;
    end

endmodule
