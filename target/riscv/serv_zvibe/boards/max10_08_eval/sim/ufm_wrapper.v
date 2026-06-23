// Copyright (c) 2025 Martin R. Raumann
// SPDX-License-Identifier: BSD-3-Clause

// UFM wrapper that matches Quartus IP interface
// Uses behavioral model for simulation

`timescale 1ns/1ps

/* verilator lint_off DECLFILENAME */
/* verilator lint_off UNUSEDSIGNAL */
module ufm (
    input  wire        clock,
    input  wire        reset_n,

    // Avalon-MM Data interface (read-only in behavioral model)
    input  wire [16:0] avmm_data_addr,
    input  wire        avmm_data_read,
    input  wire        avmm_data_write,         // Stubbed (ignored)
    input  wire [31:0] avmm_data_writedata,     // Stubbed (ignored)
    input  wire [3:0]  avmm_data_burstcount,
    output wire [31:0] avmm_data_readdata,
    output wire        avmm_data_waitrequest,
    output wire        avmm_data_readdatavalid,

    // Avalon-MM CSR interface (stubbed - not implemented in behavioral model)
    input  wire        avmm_csr_addr,           // Stubbed (ignored)
    input  wire        avmm_csr_read,           // Stubbed (ignored)
    input  wire        avmm_csr_write,          // Stubbed (ignored)
    input  wire [31:0] avmm_csr_writedata,      // Stubbed (ignored)
    output wire [31:0] avmm_csr_readdata        // Stubbed (returns 0)
);
    // CSR interface stub (not implemented - behavioral model is read-only)
    assign avmm_csr_readdata = 32'h0;

    // Instantiate behavioral model (data interface only)
    // NOTE: The behavioral model expects ufm_init.dat (word-addressed, Verilog hex format)
    // Format: @<word_addr> <32-bit_data>
    // Both Verilator and Questa use .dat files (same format)
    max10_ufm_model #(
        .ADDR_WIDTH(17),                    // 17-bit addresses for word-addressed Avalon interface
        .MEMFILE("ufm_init.dat"),           // Word-addressed .dat file for simulation
        .MEMSIZE(524288),  // 512KB (oversized for simulation - prevents bounds errors)
        .ADDR_OFFSET(0)                     // No offset - code is already at correct address in file
    ) ufm_model (
        .i_clk(clock),
        .i_reset(~reset_n),
        .i_address(avmm_data_addr),
        .i_read(avmm_data_read),
        .i_write(avmm_data_write),          // Pass through (behavioral model ignores writes)
        .i_writedata(avmm_data_writedata),
        .o_readdata(avmm_data_readdata),
        .o_waitrequest(avmm_data_waitrequest),
        .o_readdatavalid(avmm_data_readdatavalid)
    );
endmodule
