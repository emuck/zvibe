///////////////////////////////////////////////////////////////////////////////
// max10_ufm_unified.sv
//
// Unified MAX10 UFM Controller - XIP + CSR in single address space
//
// Handles both Execute-in-Place (XIP) reads and Control/Status/Write operations
// in a unified 0x80000000 address range, eliminating the need for 0xC0000000.
//
// This design solves the SERV CPU limitation where addresses 0xC0000000+
// don't generate valid strobe signals.
//
// Memory Map:
//   0x80000000-0x8002AFFF: UFM data (XIP read-only) - 172KB
//   0x80040000: STATUS register (read-only)
//   0x80040004: CONTROL register (read/write)
//   0x80040008: WRITE_ADDR register (write-only)
//   0x8004000C: WRITE_DATA register (write-only, triggers program)
//
// Copyright (c) 2025 Martin R. Raumann
// SPDX-License-Identifier: BSD-3-Clause
///////////////////////////////////////////////////////////////////////////////

`timescale 1ns / 1ps

module max10_ufm_unified #(
    parameter UFM_ADDR_WIDTH = 17,    // UFM IP uses 17-bit word addresses
    parameter DATA_WIDTH = 32
)(
    // System
    input  logic                      i_clk,
    input  logic                      i_reset,

    // Wishbone slave interface (CPU side)
    input  logic                      i_wb_cyc,
    input  logic                      i_wb_stb,
    input  logic [31:0]               i_wb_addr,
    input  logic [DATA_WIDTH-1:0]     i_wb_data,
    input  logic [3:0]                i_wb_sel,
    input  logic                      i_wb_we,
    output logic [DATA_WIDTH-1:0]     o_wb_data,
    output logic                      o_wb_ack,
    output logic                      o_wb_stall,

    // Avalon-MM master to UFM IP (CSR interface)
    output logic                      o_avmm_csr_addr,
    output logic                      o_avmm_csr_read,
    output logic                      o_avmm_csr_write,
    output logic [31:0]               o_avmm_csr_writedata,
    input  logic [31:0]               i_avmm_csr_readdata,

    // Avalon-MM master to UFM IP (Data interface)
    output logic [UFM_ADDR_WIDTH-1:0] o_avmm_data_addr,
    output logic                      o_avmm_data_read,
    output logic                      o_avmm_data_write,
    output logic [31:0]               o_avmm_data_writedata,
    output logic [3:0]                o_avmm_data_burstcount,
    input  logic [31:0]               i_avmm_data_readdata,
    input  logic                      i_avmm_data_waitrequest,
    input  logic                      i_avmm_data_readdatavalid
);

    //========================================================================
    // Address Decode
    //========================================================================
    // Bits [19:18] determine region:
    //   00: XIP data (0x80000000-0x8003FFFF)
    //   01: CSR/Write (0x80040000-0x8007FFFF)

    logic [1:0] region_sel;
    logic       sel_xip, sel_csr;
    assign region_sel = i_wb_addr[19:18];
    assign sel_xip    = (region_sel == 2'b00);
    assign sel_csr    = (region_sel == 2'b01);

    // CSR register decode (bits [3:2] when in CSR region)
    logic [1:0] csr_reg;
    assign csr_reg = i_wb_addr[3:2];
    localparam logic [1:0] CSR_STATUS     = 2'd0;  // 0x80040000
    localparam logic [1:0] CSR_CONTROL    = 2'd1;  // 0x80040004
    localparam logic [1:0] CSR_WRITE_ADDR = 2'd2;  // 0x80040008
    localparam logic [1:0] CSR_WRITE_DATA = 2'd3;  // 0x8004000C

    //========================================================================
    // State Machine
    //========================================================================
    typedef enum logic [2:0] {
        ST_IDLE,
        ST_XIP_READ,
        ST_XIP_WAIT,
        ST_CSR_READ,
        ST_CSR_WAIT,
        ST_CSR_WRITE,
        ST_DATA_WRITE
    } state_t;

    state_t state_q, state_d;

    //========================================================================
    // Registered Outputs and Internal State
    //========================================================================
    logic [UFM_ADDR_WIDTH-1:0] write_addr_reg;

    // Avalon-MM CSR registered outputs (connected to module ports via assigns)
    logic avmm_csr_read_r;
    logic avmm_csr_write_r;
    logic [31:0] avmm_csr_writedata_r;
    logic avmm_csr_addr_r;

    // Avalon-MM Data registered outputs
    logic avmm_data_read_r;
    logic avmm_data_write_r;
    logic [UFM_ADDR_WIDTH-1:0] avmm_data_addr_r;
    logic [31:0] avmm_data_writedata_r;
    logic [3:0] avmm_data_burstcount_r;

    // "Next" combinational values
    state_t                      state_d_fsm;  // (alias used below)
    logic [31:0]                 next_wb_data;
    logic                        next_wb_ack;
    logic [UFM_ADDR_WIDTH-1:0]   next_write_addr;
    logic                        next_csr_read;
    logic                        next_csr_write;
    logic [31:0]                 next_csr_writedata;
    logic                        next_csr_addr;
    logic                        next_data_read;
    logic                        next_data_write;
    logic [UFM_ADDR_WIDTH-1:0]   next_data_addr;
    logic [31:0]                 next_data_writedata;
    logic [3:0]                  next_data_burstcount;

    //========================================================================
    // Avalon-MM Port Assignments (registered → ports)
    //========================================================================
    assign o_avmm_csr_addr      = avmm_csr_addr_r;
    assign o_avmm_csr_read      = avmm_csr_read_r;
    assign o_avmm_csr_write     = avmm_csr_write_r;
    assign o_avmm_csr_writedata = avmm_csr_writedata_r;

    assign o_avmm_data_addr       = avmm_data_addr_r;
    assign o_avmm_data_read       = avmm_data_read_r;
    assign o_avmm_data_write      = avmm_data_write_r;
    assign o_avmm_data_writedata  = avmm_data_writedata_r;
    assign o_avmm_data_burstcount = avmm_data_burstcount_r;

    assign o_wb_stall = i_avmm_data_waitrequest;

    //========================================================================
    // Sequential: Register State and All Outputs
    //========================================================================
    always_ff @(posedge i_clk) begin
        if (i_reset) begin
            state_q              <= ST_IDLE;
            o_wb_ack             <= 1'b0;
            o_wb_data            <= 32'h0;

            avmm_csr_read_r      <= 1'b0;
            avmm_csr_write_r     <= 1'b0;
            avmm_csr_writedata_r <= 32'h0;
            avmm_csr_addr_r      <= 1'b0;

            avmm_data_read_r     <= 1'b0;
            avmm_data_write_r    <= 1'b0;
            avmm_data_addr_r     <= '0;
            avmm_data_writedata_r  <= 32'h0;
            avmm_data_burstcount_r <= 4'd1;

            write_addr_reg <= '0;
        end else begin
            state_q              <= state_d;
            o_wb_ack             <= next_wb_ack;
            o_wb_data            <= next_wb_data;

            avmm_csr_read_r      <= next_csr_read;
            avmm_csr_write_r     <= next_csr_write;
            avmm_csr_writedata_r <= next_csr_writedata;
            avmm_csr_addr_r      <= next_csr_addr;

            avmm_data_read_r     <= next_data_read;
            avmm_data_write_r    <= next_data_write;
            avmm_data_addr_r     <= next_data_addr;
            avmm_data_writedata_r  <= next_data_writedata;
            avmm_data_burstcount_r <= next_data_burstcount;

            write_addr_reg <= next_write_addr;
        end
    end

    //========================================================================
    // Combinational: Next-State and Next-Output Logic
    //========================================================================
    always_comb begin
        // Defaults: hold current values; clear one-shot signals
        state_d              = state_q;
        next_wb_ack          = 1'b0;                              // one-shot: clear each cycle
        next_wb_data         = o_wb_data;                         // hold
        next_write_addr      = write_addr_reg;                    // hold
        next_csr_read        = 1'b0;                              // one-shot: clear each cycle
        next_csr_write       = avmm_csr_write_r;                  // hold
        next_csr_writedata   = avmm_csr_writedata_r;              // hold
        next_csr_addr        = avmm_csr_addr_r;                   // hold
        // Keep data_read asserted while waitrequest holds (all states not overridden)
        next_data_read       = i_avmm_data_waitrequest & avmm_data_read_r;
        next_data_write      = avmm_data_write_r;                 // hold
        next_data_addr       = avmm_data_addr_r;                  // hold
        next_data_writedata  = avmm_data_writedata_r;             // hold
        next_data_burstcount = avmm_data_burstcount_r;            // hold

        unique case (state_q)
            ST_IDLE: begin
                if (i_wb_cyc && i_wb_stb && !o_wb_ack) begin
                    if (sel_xip) begin
                        // XIP read: latch address and go to XIP_READ
                        next_data_addr       = i_wb_addr[UFM_ADDR_WIDTH+1:2];
                        next_data_burstcount = 4'd1;
                        next_csr_addr        = 1'b0;
                        next_csr_writedata   = 32'h0;
                        state_d              = ST_XIP_READ;
                    end else if (sel_csr) begin
                        if (i_wb_we) begin
                            unique case (csr_reg)
                                CSR_CONTROL: begin
                                    next_csr_addr      = 1'b1;
                                    next_csr_write     = 1'b1;
                                    next_csr_writedata = i_wb_data;
                                    state_d            = ST_CSR_WRITE;
                                end
                                CSR_WRITE_ADDR: begin
                                    next_write_addr = i_wb_data[UFM_ADDR_WIDTH-1:0];
                                    next_wb_ack     = 1'b1;
                                    // state_d stays ST_IDLE
                                end
                                CSR_WRITE_DATA: begin
                                    next_data_addr       = write_addr_reg;
                                    next_data_write      = 1'b1;
                                    next_data_writedata  = i_wb_data;
                                    next_data_burstcount = 4'd1;
                                    next_csr_addr        = 1'b0;
                                    next_csr_writedata   = 32'h0;
                                    state_d              = ST_DATA_WRITE;
                                end
                                default: begin
                                    next_wb_ack = 1'b1;
                                end
                            endcase
                        end else begin
                            // CSR read
                            unique case (csr_reg)
                                CSR_STATUS: begin
                                    next_csr_addr = 1'b0;
                                    next_csr_read = 1'b1;
                                    state_d       = ST_CSR_READ;
                                end
                                CSR_CONTROL: begin
                                    next_csr_addr = 1'b1;
                                    next_csr_read = 1'b1;
                                    state_d       = ST_CSR_READ;
                                end
                                CSR_WRITE_ADDR: begin
                                    next_wb_data = {{(32-UFM_ADDR_WIDTH){1'b0}}, write_addr_reg};
                                    next_wb_ack  = 1'b1;
                                end
                                default: begin
                                    next_wb_data = 32'hDEADBEEF;
                                    next_wb_ack  = 1'b1;
                                end
                            endcase
                        end
                    end else begin
                        // Invalid address
                        next_wb_data = 32'h00000000;
                        next_wb_ack  = 1'b1;
                    end
                end
            end

            ST_XIP_READ: begin
                // Assert read for 1 cycle; wait for acceptance (waitrequest low)
                next_data_read = 1'b1;
                if (avmm_data_read_r && !i_avmm_data_waitrequest)
                    state_d = ST_XIP_WAIT;
            end

            ST_XIP_WAIT: begin
                // Clear read strobe; wait for data return
                next_data_read = 1'b0;
                if (i_avmm_data_readdatavalid && !i_avmm_data_waitrequest) begin
                    next_wb_data = i_avmm_data_readdata;
                    next_wb_ack  = 1'b1;
                    state_d      = ST_IDLE;
                end
            end

            ST_CSR_READ: begin
                // Read strobe was asserted last cycle; clear and wait for data
                next_csr_read = 1'b0;
                state_d       = ST_CSR_WAIT;
            end

            ST_CSR_WAIT: begin
                // CSR data now valid; capture and ack
                next_wb_data       = i_avmm_csr_readdata;
                next_wb_ack        = 1'b1;
                next_csr_addr      = 1'b0;
                next_csr_writedata = 32'h0;
                state_d            = ST_IDLE;
            end

            ST_CSR_WRITE: begin
                // Clear write strobe after 1 cycle; ack
                next_csr_write     = 1'b0;
                next_wb_ack        = 1'b1;
                next_csr_addr      = 1'b0;
                next_csr_writedata = 32'h0;
                state_d            = ST_IDLE;
            end

            ST_DATA_WRITE: begin
                next_data_read = 1'b0;
                if (!i_avmm_data_waitrequest) begin
                    next_data_write = 1'b0;
                    next_wb_ack     = 1'b1;
                    state_d         = ST_IDLE;
                end
            end

            default: state_d = ST_IDLE;
        endcase
    end

endmodule

