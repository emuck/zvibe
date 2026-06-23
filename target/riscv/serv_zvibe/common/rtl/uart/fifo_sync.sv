//============================================================================
// Synchronous FIFO
//
// Simple synchronous FIFO for UART buffering
// Based on standard FIFO design patterns
//
// Copyright (c) 2025 Martin R. Raumann
// SPDX-License-Identifier: BSD-3-Clause
//
// Features:
// - Parameterized width and depth
// - Single clock domain
// - Full/empty flags
// - Count output
// - Registered read output (1 cycle latency, BRAM-friendly)
//============================================================================

`timescale 1ns/1ps

module fifo_sync #(
    parameter int DATA_WIDTH = 8,
    parameter int DEPTH = 64
) (
    input  logic                  clk,
    input  logic                  rst,
    
    // Write interface
    input  logic [DATA_WIDTH-1:0] wr_data,
    input  logic                  wr_en,
    output logic                  full,
    
    // Read interface
    output logic [DATA_WIDTH-1:0] rd_data,
    input  logic                  rd_en,
    output logic                  empty,
    
    // Status
    output logic [$clog2(DEPTH):0]   count
);

    // Local parameter for address width
    localparam int ADDR_WIDTH = $clog2(DEPTH);

    // Memory array (will infer BRAM)
    logic [DATA_WIDTH-1:0] mem [0:DEPTH-1];
    
    // Read and write pointers
    logic [ADDR_WIDTH:0] wr_ptr;
    logic [ADDR_WIDTH:0] rd_ptr;
    
    // Write logic
    always_ff @(posedge clk) begin
        if (rst) begin
            wr_ptr <= '0;
        end else begin
            if (wr_en && !full) begin
                mem[wr_ptr[ADDR_WIDTH-1:0]] <= wr_data;
                wr_ptr <= wr_ptr + 1'b1;
                //$display("[FIFO] Write: ptr=%0d data=0x%02x", wr_ptr, wr_data);
            end
        end
    end
    
    // Read logic (registered output for correct semantics)
    always_ff @(posedge clk) begin
        if (rst) begin
            rd_ptr <= '0;
            rd_data <= '0;
        end else begin
            if (rd_en && !empty) begin
                // Capture current data BEFORE incrementing pointer
                rd_data <= mem[rd_ptr[ADDR_WIDTH-1:0]];
                rd_ptr <= rd_ptr + 1'b1;
            end
        end
    end
    
    // Count logic
    assign count = wr_ptr - rd_ptr;
    
    // Full/empty flags
    assign full = (count == DEPTH[ADDR_WIDTH:0]);
    assign empty = (count == '0);

endmodule
