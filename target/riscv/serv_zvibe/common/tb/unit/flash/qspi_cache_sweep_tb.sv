// Copyright (c) 2026 Martin R. Raumann
// SPDX-License-Identifier: BSD-3-Clause

// qspi_cache_sweep_tb.sv
// Full address sweep of qspi_cache_bram with NUM_LINES=256 (hardware config).
// Reads every cache line, verifies cold miss, hit, and eviction correctness.

`timescale 1ns/1ps
`default_nettype none

module qspi_cache_sweep_tb;

    logic clk = 0;
    always #5 clk = ~clk;

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

    qspi_cache_bram #(.NUM_LINES(256)) dut (
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

    integer pass_cnt = 0, fail_cnt = 0;
    integer fetch_cnt = 0;

    always_ff @(posedge clk)
        if (xip_ack) fetch_cnt <= fetch_cnt + 1;

    task wb_read(input [31:0] addr, output [31:0] data);
        integer timeout;
        @(posedge clk); #1;
        wb_cyc = 1; wb_stb = 1; wb_addr = addr;
        timeout = 0;
        while (!wb_ack && timeout < 300) begin
            @(posedge clk); #1;
            timeout = timeout + 1;
        end
        data = wb_data;
        wb_cyc = 0; wb_stb = 0;
        @(posedge clk); #1;
    endtask

    // XIP burst responder: data = address-derived pattern
    always_ff @(posedge clk) begin
        xip_ack <= 1'b0;
        if (xip_cyc && xip_stb && !xip_stall && !xip_ack) begin
            xip_data[31:0]   <= xip_addr + 32'h0;
            xip_data[63:32]  <= xip_addr + 32'h4;
            xip_data[95:64]  <= xip_addr + 32'h8;
            xip_data[127:96] <= xip_addr + 32'hC;
            xip_ack <= 1'b1;
        end
    end

    logic [31:0] got;
    logic [31:0] base;
    integer i, w;
    integer saved_fetch;

    initial begin
        reset = 1; wb_cyc = 0; wb_stb = 0; wb_addr = 0;
        xip_data = 0; xip_ack = 0; xip_stall = 0;
        repeat(4) @(posedge clk);
        #1; reset = 0;
        repeat(2) @(posedge clk); #1;

        $display("============================================================");
        $display("Cache sweep: NUM_LINES=256, 4 words/line");
        $display("============================================================");

        // ---- Phase 1: Cold fill all 256 lines, check all 4 words each ----
        $display("\nPhase 1: Cold-fill all 256 lines (1024 reads)");
        for (i = 0; i < 256; i = i + 1) begin
            base = 32'h80000000 + (i * 16);
            for (w = 0; w < 4; w = w + 1) begin
                wb_read(base + w*4, got);
                if (got !== base + w*4) begin
                    $display("  FAIL line %0d word %0d: addr=%08x exp=%08x got=%08x",
                             i, w, base+w*4, base+w*4, got);
                    fail_cnt = fail_cnt + 1;
                end else begin
                    pass_cnt = pass_cnt + 1;
                end
            end
        end
        $display("  Phase 1 done: %0d pass, %0d fail", pass_cnt, fail_cnt);

        // ---- Phase 2: Re-read all (should be hits, no fetches) ----
        $display("\nPhase 2: Re-read all 256 lines (expect all hits)");
        saved_fetch = fetch_cnt;
        for (i = 0; i < 256; i = i + 1) begin
            base = 32'h80000000 + (i * 16);
            for (w = 0; w < 4; w = w + 1) begin
                wb_read(base + w*4, got);
                if (got !== base + w*4) begin
                    $display("  FAIL hit line %0d word %0d: exp=%08x got=%08x",
                             i, w, base+w*4, got);
                    fail_cnt = fail_cnt + 1;
                end else begin
                    pass_cnt = pass_cnt + 1;
                end
            end
        end
        $display("  Fetches during phase 2: %0d (expected 0)", fetch_cnt - saved_fetch);
        if (fetch_cnt - saved_fetch != 0) begin
            $display("  FAIL: unexpected fetches during hit phase");
            fail_cnt = fail_cnt + 1;
        end else begin
            pass_cnt = pass_cnt + 1;
        end

        // ---- Phase 3: Evict every line with tag=1, verify ----
        $display("\nPhase 3: Evict all lines (different tag), verify new data");
        for (i = 0; i < 256; i = i + 1) begin
            base = 32'h80001000 + (i * 16);  // tag differs from phase 1
            for (w = 0; w < 4; w = w + 1) begin
                wb_read(base + w*4, got);
                if (got !== base + w*4) begin
                    $display("  FAIL evict line %0d word %0d: exp=%08x got=%08x",
                             i, w, base+w*4, got);
                    fail_cnt = fail_cnt + 1;
                end else begin
                    pass_cnt = pass_cnt + 1;
                end
            end
        end

        // ---- Phase 4: Re-read original addresses (re-miss, refill) ----
        $display("\nPhase 4: Re-read original addresses (refill after eviction)");
        for (i = 0; i < 256; i = i + 1) begin
            base = 32'h80000000 + (i * 16);
            wb_read(base, got);
            if (got !== base) begin
                $display("  FAIL refill line %0d: exp=%08x got=%08x", i, base, got);
                fail_cnt = fail_cnt + 1;
            end else begin
                pass_cnt = pass_cnt + 1;
            end
        end

        // ---- Phase 5: Interleaved access pattern (stride = 37 lines) ----
        $display("\nPhase 5: Non-sequential stride access (stride=37 lines)");
        for (i = 0; i < 256; i = i + 1) begin
            integer line_idx;
            line_idx = (i * 37) % 256;
            base = 32'h80002000 + (line_idx * 16);
            wb_read(base, got);
            if (got !== base) begin
                $display("  FAIL stride line %0d (idx=%0d): exp=%08x got=%08x",
                         i, line_idx, base, got);
                fail_cnt = fail_cnt + 1;
            end else begin
                pass_cnt = pass_cnt + 1;
            end
        end

        $display("\n============================================================");
        $display("Results: %0d passed, %0d failed", pass_cnt, fail_cnt);
        if (fail_cnt == 0)
            $display("PASS: cache_sweep_256");
        else
            $display("FAIL: cache_sweep_256");
        $display("============================================================");
        $finish;
    end

endmodule

`default_nettype wire
