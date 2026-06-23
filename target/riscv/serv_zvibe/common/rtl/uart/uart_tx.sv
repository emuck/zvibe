//============================================================================
// UART Transmitter
//
// Based on alexforencich/verilog-uart
// Original Copyright (c) 2014-2017 Alex Forencich - MIT License
// SPDX-License-Identifier: MIT
// https://github.com/alexforencich/verilog-uart
//
// Modified work:
// - Converted from Verilog to SystemVerilog
// - Changed interface from AXI-Stream to simple valid/ready
// - Prescale is compile-time parameter
// Copyright (c) 2025 zvibe project - BSD-3-Clause
//
// Prescale: PRESCALE = Fclk / (baud * 8)
// For 100MHz @ 115200: PRESCALE = 108
// Clock cycles per bit = PRESCALE * 8
//============================================================================

`timescale 1ns/1ps

module uart_tx #(
    parameter int PRESCALE = 108  // Fclk/(baud*8) - e.g., 100MHz/(115200*8)=108
) (
    input  logic       clk,
    input  logic       rst,
    
    // Data interface
    input  logic [7:0] tx_data,
    input  logic       tx_valid,
    output logic       tx_ready,
    
    // UART output
    output logic       txd
);

    logic        tx_ready_reg;
    logic        txd_reg;
    logic [8:0]  data_reg;
    logic [18:0] prescale_reg;
    logic [3:0]  bit_cnt;
    
    assign tx_ready = tx_ready_reg;
    assign txd = txd_reg;
    
    always_ff @(posedge clk) begin
        if (rst) begin
            tx_ready_reg  <= 1'b0;
            txd_reg       <= 1'b1;
            prescale_reg  <= '0;
            bit_cnt       <= '0;
        end else begin
            if (prescale_reg > 0) begin
                tx_ready_reg <= 1'b0;
                prescale_reg <= prescale_reg - 1;
            end else if (bit_cnt == 0) begin
                tx_ready_reg <= 1'b1;
                
                if (tx_valid) begin
                    tx_ready_reg <= !tx_ready_reg;
                    prescale_reg <= (PRESCALE << 3) - 1;  // 8x prescale - 1
                    bit_cnt <= 4'd9;  // 8 data bits + 1 stop bit
                    data_reg <= {1'b1, tx_data};  // Stop bit + data
                    txd_reg <= 1'b0;  // Start bit
                end
            end else begin
                if (bit_cnt > 1) begin
                    bit_cnt <= bit_cnt - 1;
                    prescale_reg <= (PRESCALE << 3) - 1;
                    {data_reg, txd_reg} <= {1'b0, data_reg};
                end else if (bit_cnt == 1) begin
                    bit_cnt <= bit_cnt - 1;
                    prescale_reg <= (PRESCALE << 3);  // Stop bit gets one extra clock
                    txd_reg <= 1'b1;
                end
            end
        end
    end

endmodule
