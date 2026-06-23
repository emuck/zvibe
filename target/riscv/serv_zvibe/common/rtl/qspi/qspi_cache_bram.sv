///////////////////////////////////////////////////////////////////////////////
// qspi_cache_bram.sv
//
// QSPI XIP Cache — BRAM-based, direct-mapped, 4-word (16-byte) lines
//
// Like qspi_cache.v but uses a sequential fill (one word per clock) so
// that the data storage infers a true single-port BRAM rather than
// registers.  This allows large caches (256+ lines = 4KB+) at minimal
// LUT cost; Vivado synthesizes the data array as BRAM18/BRAM36.
//
// Parameters:
//   NUM_LINES   — direct-mapped lines (power of 2, >= 2; default 256)
//
// Address layout (32-bit byte address):
//   [31 : 4+IDX_BITS]  tag
//   [4+IDX_BITS-1 : 4] line index
//   [3:2]              word select
//   [1:0]              byte offset (ignored)
//
// BRAM address width: IDX_BITS + 2  (line index + word select)
//
// Wishbone latency:
//   Hit:  4 cycles  (IDLE → LOOKUP → BRAM_RD → HIT_ACK)
//   Miss: 4 + burst_cycles + 4 fill  (dominated by QSPI)
//
// Copyright (c) 2026 Martin R. Raumann
// SPDX-License-Identifier: BSD-3-Clause
///////////////////////////////////////////////////////////////////////////////

`timescale 1ns/1ps

module qspi_cache_bram #(
    parameter NUM_LINES = 256       // direct-mapped lines (power of 2, >= 2)
) (
    input  logic        i_clk,
    input  logic        i_reset,

    // Upstream Wishbone slave (from CPU / SoC, read-only)
    input  logic        i_wb_cyc,
    input  logic        i_wb_stb,
    input  logic [31:0] i_wb_addr,
    output logic [31:0] o_wb_data,
    output logic        o_wb_ack,
    output logic        o_wb_stall,

    // Downstream burst interface (to s25fl_xip BURST_WORDS=4)
    output logic        o_xip_cyc,
    output logic        o_xip_stb,
    output logic [31:0] o_xip_addr,   // 16-byte-aligned base address
    input  logic [127:0] i_xip_data,  // 4-word burst: [31:0]=word0 .. [127:96]=word3
    input  logic        i_xip_ack,
    input  logic        i_xip_stall
);

    //=========================================================================
    // Address decomposition
    //=========================================================================
    localparam IDX_BITS  = $clog2(NUM_LINES);       // e.g. 8 for 256 lines
    localparam TAG_LSB   = 4 + IDX_BITS;
    localparam TAG_WIDTH = 32 - TAG_LSB;             // bits [31 : TAG_LSB]
    localparam BA_WIDTH  = IDX_BITS + 2;             // BRAM address width

    //=========================================================================
    // BRAM data array
    // SDP pattern: separate read and write always blocks → Vivado infers BRAM.
    // Depth: NUM_LINES*4 words.  Width: 32 bits.
    // 256 lines: 1024×32 = 32Kbits → 1× BRAM36.
    //=========================================================================
    // Quartus: add `(* ramstyle = "M9K" *)` above if not auto-inferred
    (* ram_style = "block" *) // Vivado: infer BRAM36/BRAM18
    logic [31:0] bram [0:NUM_LINES*4-1];

    logic [BA_WIDTH-1:0] bram_rd_addr;
    logic [31:0]         bram_rd_data;

    always_ff @(posedge i_clk)
        bram_rd_data <= bram[bram_rd_addr];

    logic              bram_wr_en;
    logic [BA_WIDTH-1:0] bram_wr_addr;
    logic [31:0]       bram_wr_data;

    always_ff @(posedge i_clk)
        if (bram_wr_en) bram[bram_wr_addr] <= bram_wr_data;

    //=========================================================================
    // Tag + valid (register array — small)
    //=========================================================================
    logic [TAG_WIDTH-1:0] cache_tag   [0:NUM_LINES-1];
    logic                 cache_valid [0:NUM_LINES-1];

    // Pipelined tag-lookup registers.
    //
    // The tag array is 256 entries of distributed RAM (RAMS64E × 4 per bit,
    // with MUXF7+MUXF8 selectors).  Reading and comparing in the same cycle
    // creates an 8-level logic chain that fails timing at 166 MHz.
    //
    // Fix: use S_LOOKUP — already a do-nothing wait cycle — to register the
    // raw LUTRAM output.  S_BRAM_RD then compares registered values only,
    // reducing the critical path to 3–4 levels.  Hit latency is unchanged
    // (S_IDLE → S_LOOKUP → S_BRAM_RD → S_HIT_ACK = 4 cycles).
    logic [TAG_WIDTH-1:0] lkup_tag;    // cache_tag[req_line] latched in S_LOOKUP
    logic                 lkup_valid;  // cache_valid[req_line] latched in S_LOOKUP

    //=========================================================================
    // State machine
    //
    // S_IDLE     stall=0  Accept request; start BRAM read; go to S_LOOKUP.
    // S_LOOKUP   stall=1  Wait for BRAM read latency; stay one cycle.
    // S_BRAM_RD  stall=1  bram_rd_data valid; decide hit or miss.
    // S_HIT_ACK  stall=1  Ack with bram_rd_data; back to IDLE.
    // S_MISS     stall=1  Drive burst to XIP; wait for i_xip_ack.
    // S_FILL     stall=1  Sequential write of 4 burst words into BRAM (4 cyc).
    // S_FILL_ACK stall=1  Ack with requested word from burst; back to IDLE.
    //=========================================================================
    typedef enum logic [2:0] {
        S_IDLE     = 3'd0,
        S_LOOKUP   = 3'd1,
        S_BRAM_RD  = 3'd2,
        S_HIT_ACK  = 3'd3,
        S_MISS     = 3'd4,
        S_FILL     = 3'd5,
        S_FILL_ACK = 3'd6
    } cache_state_t;

    cache_state_t state;

    assign o_wb_stall = (state != S_IDLE);

    //=========================================================================
    // Latched request fields
    //=========================================================================
    logic [IDX_BITS-1:0]  req_line;
    logic [1:0]           req_word;
    logic [TAG_WIDTH-1:0] req_tag;

    // Fill word counter
    logic [1:0] fill_cnt;

    // Ghost-request guard: set after ACK, cleared when STB deasserts.
    // Mirrors s25fl_xip's cyc_acked mechanism.  Without this, SERV holds
    // STB=1 for one extra synchronous cycle after the ACK, causing the
    // cache to re-enter S_LOOKUP on the same address and fire a spurious
    // ACK four cycles later — which corrupts serv_bufreg2 via the arbiter.
    logic was_acked;

    //=========================================================================
    // State machine + BRAM write control
    //=========================================================================
    always_ff @(posedge i_clk) begin
        // BRAM write defaults (disabled)
        // bram_wr_addr and bram_wr_data intentionally omitted: when
        // bram_wr_en=0 the BRAM ignores them, so driving them to zero
        // every cycle would add ~30-40 LUTs (mux-to-zero on each FF
        // D-input) for no functional benefit.  They hold their previous
        // value until the next S_FILL write, which is safe.
        bram_wr_en <= 1'b0;

        if (i_reset) begin
            state      <= S_IDLE;
            o_xip_cyc  <= 1'b0;
            o_xip_stb  <= 1'b0;
            o_xip_addr <= 32'h0;
            o_wb_ack   <= 1'b0;
            o_wb_data  <= 32'h0;
            fill_cnt   <= 2'd0;
            lkup_tag   <= {TAG_WIDTH{1'b0}};
            lkup_valid <= 1'b0;
            was_acked  <= 1'b0;
            // cache_tag/cache_valid are BRAM — rely on power-on zero initialisation.
            // Stale entries after runtime reset are safe: flash is read-only, so any
            // cached data is still the correct value for that address.
        end else begin
            o_wb_ack <= 1'b0;   // default

            // Clear ghost guard once STB deasserts (CPU has moved on)
            if (!i_wb_stb) was_acked <= 1'b0;

            unique case (state)

                S_IDLE: begin
                    if (i_wb_cyc && i_wb_stb && !was_acked) begin
                        // Latch address fields
                        req_line <= i_wb_addr[4+IDX_BITS-1:4];
                        req_word <= i_wb_addr[3:2];
                        req_tag  <= i_wb_addr[31:TAG_LSB];
                        // Issue BRAM read for the requested word
                        bram_rd_addr <= {i_wb_addr[4+IDX_BITS-1:4], i_wb_addr[3:2]};
                        state <= S_LOOKUP;
                    end
                end

                S_LOOKUP: begin
                    // Pipeline the LUTRAM tag/valid read into registers.
                    // Breaks the 8-level RAMS64E→compare→FSM chain that fails
                    // timing at 166 MHz: S_BRAM_RD now compares registered values
                    // (3–4 levels) rather than async LUTRAM outputs (8 levels).
                    lkup_tag   <= cache_tag  [req_line];
                    lkup_valid <= cache_valid[req_line];
                    state <= S_BRAM_RD;
                end

                S_BRAM_RD: begin
                    // bram_rd_data is now valid; evaluate hit/miss using
                    // registered lkup_tag/lkup_valid (latched in S_LOOKUP).
                    if (lkup_valid && (lkup_tag == req_tag)) begin
                        state <= S_HIT_ACK;
                    end else begin
                        // Miss: send 16-byte-aligned burst request to XIP
                        // Reconstruct address: {tag, line_index, 4'b0}
                        o_xip_addr <= {req_tag, req_line, 4'b0};
                        o_xip_cyc  <= 1'b1;
                        o_xip_stb  <= 1'b1;
                        state      <= S_MISS;
                    end
                end

                S_HIT_ACK: begin
                    o_wb_ack  <= 1'b1;
                    o_wb_data <= bram_rd_data;
                    was_acked <= 1'b1;
                    state     <= S_IDLE;
                end

                S_MISS: begin
                    if (i_xip_ack) begin
                        o_xip_cyc  <= 1'b0;
                        o_xip_stb  <= 1'b0;
                        cache_tag  [req_line] <= req_tag;
                        cache_valid[req_line] <= 1'b1;
                        fill_cnt   <= 2'd0;
                        state      <= S_FILL;
                    end
                end

                S_FILL: begin
                    // Write one word per cycle into BRAM
                    bram_wr_en   <= 1'b1;
                    bram_wr_addr <= {req_line, fill_cnt};
                    bram_wr_data <= i_xip_data[fill_cnt*32 +: 32];
                    if (fill_cnt == 2'd3) begin
                        state <= S_FILL_ACK;
                    end else begin
                        fill_cnt <= fill_cnt + 1'd1;
                    end
                end

                S_FILL_ACK: begin
                    o_wb_ack  <= 1'b1;
                    // i_xip_data (s25fl_xip.o_wb_data) holds its value until the next
                    // READ_DONE, so it is stable throughout S_FILL and S_FILL_ACK.
                    o_wb_data <= i_xip_data[req_word*32 +: 32];
                    was_acked <= 1'b1;
                    state     <= S_IDLE;
                end

                default: state <= S_IDLE;

            endcase
        end
    end

endmodule
