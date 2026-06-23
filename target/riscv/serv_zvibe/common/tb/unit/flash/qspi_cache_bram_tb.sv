// Copyright (c) 2025 Martin R. Raumann
// SPDX-License-Identifier: BSD-3-Clause

// qspi_cache_bram_tb.sv
// Unit tests for qspi_cache_bram module.
//
// Tests mimic qspi_cache_tb.sv structure but account for the 1-extra-cycle
// BRAM read latency (hit = 4 cycles vs 3 for register cache).
//
// Uses NUM_LINES=2 for testability (eviction coverage).

`timescale 1ns/1ps
`default_nettype none

module qspi_cache_bram_tb;

    // Clock
    logic clk = 0;
    always #5 clk = ~clk;   // 100 MHz

    // DUT signals
    logic        reset;
    logic        wb_cyc, wb_stb;
    logic [31:0] wb_addr;
    logic [31:0] wb_data;
    logic        wb_ack;
    logic        wb_stall;

    logic        xip_cyc, xip_stb;
    logic [31:0] xip_addr;
    logic [127:0] xip_data;
    logic        xip_ack;
    logic        xip_stall;

    qspi_cache_bram #(.NUM_LINES(2)) dut (
        .i_clk      (clk),
        .i_reset    (reset),
        .i_wb_cyc   (wb_cyc),
        .i_wb_stb   (wb_stb),
        .i_wb_addr  (wb_addr),
        .o_wb_data  (wb_data),
        .o_wb_ack   (wb_ack),
        .o_wb_stall (wb_stall),
        .o_xip_cyc  (xip_cyc),
        .o_xip_stb  (xip_stb),
        .o_xip_addr (xip_addr),
        .i_xip_data (xip_data),
        .i_xip_ack  (xip_ack),
        .i_xip_stall(xip_stall)
    );

    // Helpers
    integer pass_cnt = 0, fail_cnt = 0;
    integer fetch_cnt = 0;
    integer saved_fetch;
    integer delta;

    task save_fetch; saved_fetch = fetch_cnt; endtask
    task get_delta; delta = fetch_cnt - saved_fetch; endtask

    always_ff @(posedge clk)
        if (xip_ack) fetch_cnt <= fetch_cnt + 1;

    task check(input pass, input [127:0] label);
        if (pass) begin
            $display("  PASS: %s", label);
            pass_cnt = pass_cnt + 1;
        end else begin
            $display("  FAIL: %s", label);
            fail_cnt = fail_cnt + 1;
        end
    endtask

    // Issue a single WB read; wait for ack; return data
    task wb_read(input [31:0] addr, output [31:0] data);
        integer timeout;
        @(posedge clk); #1;
        wb_cyc  = 1; wb_stb = 1; wb_addr = addr;
        timeout = 0;
        while (!wb_ack && timeout < 300) begin
            @(posedge clk); #1;
            timeout = timeout + 1;
        end
        data = wb_data;
        wb_cyc = 0; wb_stb = 0;
        @(posedge clk); #1;
    endtask

    // Simulate XIP burst response in background
    // Responds to any cyc/stb assertion with data = {base+12, base+8, base+4, base+0}
    // (word values derived from xip_addr)
    always_ff @(posedge clk) begin
        xip_ack   <= 1'b0;
        if (xip_cyc && xip_stb && !xip_stall && !xip_ack) begin
            xip_data[31:0]   <= xip_addr + 32'h0;
            xip_data[63:32]  <= xip_addr + 32'h4;
            xip_data[95:64]  <= xip_addr + 32'h8;
            xip_data[127:96] <= xip_addr + 32'hC;
            xip_ack <= 1'b1;
        end
    end

    logic [31:0] got;
    logic [31:0] expected;

    initial begin
        $dumpfile("build_cache_bram/dump.vcd");
        $dumpvars(0, qspi_cache_bram_tb);

        reset = 1; wb_cyc = 0; wb_stb = 0; wb_addr = 0;
        xip_data = 0; xip_ack = 0; xip_stall = 0;
        repeat(4) @(posedge clk);
        #1; reset = 0;
        repeat(2) @(posedge clk); #1;

        $display("============================================================");
        $display("qspi_cache_bram unit tests");
        $display("============================================================");

        // ------------------------------------------------------------------
        $display("\nTEST 1: Cold miss — burst fetch on first access");
        save_fetch;
        wb_read(32'h80000000, got);
        expected = 32'h80000000;   // xip_addr + 0
        check(got == expected, "cold miss word0");
        get_delta; check(delta == 1,  "cold miss triggers exactly 1 fetch");

        // ------------------------------------------------------------------
        $display("\nTEST 2: Hit — repeat access should not fetch again");
        save_fetch;
        wb_read(32'h80000000, got);
        check(got == expected,    "hit returns correct data");
        get_delta; check(delta == 0,   "hit does not trigger fetch");

        // ------------------------------------------------------------------
        $display("\nTEST 3: Sequential hits within same cache line");
        wb_read(32'h80000004, got);
        check(got == 32'h80000004, "+4 hit");
        wb_read(32'h80000008, got);
        check(got == 32'h80000008, "+8 hit");
        wb_read(32'h8000000C, got);
        check(got == 32'h8000000C, "+C hit");
        save_fetch;
        wb_read(32'h80000001, got); // byte offset in word0
        check(got == 32'h80000000, "unaligned hits word0");
        get_delta; check(delta == 0,    "no fetch for unaligned hit");

        // ------------------------------------------------------------------
        $display("\nTEST 4: Miss into second line (NUM_LINES=2, addr bit[4]=1)");
        save_fetch;
        wb_read(32'h80000010, got);
        expected = 32'h80000010;
        check(got == expected,    "second line cold miss word0");
        get_delta; check(delta == 1,   "second line miss = 1 fetch");

        // First line still valid?
        save_fetch;
        wb_read(32'h80000000, got);
        check(got == 32'h80000000, "first line survives second-line fill");
        get_delta; check(delta == 0,    "no extra fetch for first line");

        // ------------------------------------------------------------------
        $display("\nTEST 5: Eviction — third unique line evicts line0");
        save_fetch;
        wb_read(32'h80000020, got);  // tag!=line0-tag, line index=0 → evicts line0
        expected = 32'h80000020;
        check(got == expected,    "eviction: new data returned");
        get_delta; check(delta == 1,   "eviction: 1 fetch issued");

        // ------------------------------------------------------------------
        $display("\nTEST 6: Re-miss — original line0 was evicted");
        save_fetch;
        wb_read(32'h80000000, got);
        check(got == 32'h80000000, "re-miss returns correct data");
        get_delta; check(delta == 1,    "re-miss triggers 1 new fetch");

        // ------------------------------------------------------------------
        $display("\nTEST 7: Reset preserves cache (flash is read-only)");
        // Fill cache lines for two addresses
        wb_read(32'h80000000, got);
        wb_read(32'h80000010, got);
        @(posedge clk); #1;
        reset = 1;
        @(posedge clk); #1;
        reset = 0;
        repeat(2) @(posedge clk); #1;
        // After reset, 0x80000000 is still cached (flash data never changes,
        // so the RTL intentionally skips invalidation on reset).
        save_fetch;
        wb_read(32'h80000000, got);
        get_delta; check(delta == 0,    "reset preserves cached lines (no refetch)");
        check(got == 32'h80000000, "cached data still correct after reset");
        // A new address not in cache should still miss normally.
        save_fetch;
        wb_read(32'h80001000, got);
        get_delta; check(delta == 1,    "uncached address still misses after reset");
        check(got == 32'h80001000, "miss fetch returns correct data after reset");

        // ------------------------------------------------------------------
        $display("\nTEST 8: Stall asserted while miss is in flight");
        begin
            integer stall_cnt;
            stall_cnt = 0;
            fork
                begin
                    wb_read(32'h80001000, got);
                end
                begin
                    // Count stall cycles after request accepted
                    repeat(100) begin
                        @(posedge clk);
                        if (wb_stall) stall_cnt = stall_cnt + 1;
                    end
                end
            join
            check(stall_cnt > 0, "stall asserted during miss");
        end

        // ------------------------------------------------------------------
        $display("\n============================================================");
        $display("Results: %0d passed, %0d failed", pass_cnt, fail_cnt);
        if (fail_cnt == 0)
            $display("*** ALL TESTS PASSED ***");
        else
            $display("*** FAILURES DETECTED ***");
        $display("============================================================");
        $finish;
    end

endmodule

`default_nettype wire
