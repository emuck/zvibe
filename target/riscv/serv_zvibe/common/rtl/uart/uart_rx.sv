//============================================================================
// UART Receiver
//
// Based on alexforencich/verilog-uart
// Original Copyright (c) 2014-2017 Alex Forencich - MIT License
// SPDX-License-Identifier: MIT
// https://github.com/alexforencich/verilog-uart
//
// Modified work:
// - Converted from Verilog to SystemVerilog
// - Changed interface from AXI-Stream to simple valid signal
// - Prescale is compile-time parameter
// Copyright (c) 2025 zvibe project - BSD-3-Clause
//
// Prescale: PRESCALE = Fclk / (baud * 8)
// For 100MHz @ 115200: PRESCALE = 108
// Clock cycles per bit = PRESCALE * 8
//============================================================================

`timescale 1ns/1ps

module uart_rx #(
    parameter int PRESCALE = 108  // Fclk/(baud*8) - e.g., 100MHz/(115200*8)=108
) (
    input  logic       clk,
    input  logic       rst,
    
    // UART input
    input  logic       rxd,
    
    // Data interface
    output logic [7:0] rx_data,
    output logic       rx_valid,
    
    // Status
    output logic       overrun_error,
    output logic       frame_error
);

    logic [7:0]  rx_data_reg;
    logic        rx_valid_reg;
    logic        rxd_reg;
    logic        overrun_error_reg;
    logic        frame_error_reg;
    logic [7:0]  data_reg;
    logic [18:0] prescale_reg;
    logic [3:0]  bit_cnt;
    
    assign rx_data = rx_data_reg;
    assign rx_valid = rx_valid_reg;
    assign overrun_error = overrun_error_reg;
    assign frame_error = frame_error_reg;
    
    always_ff @(posedge clk) begin
        if (rst) begin
            rx_data_reg       <= '0;
            rx_valid_reg      <= 1'b0;
            rxd_reg           <= 1'b1;
            prescale_reg      <= '0;
            bit_cnt           <= '0;
            overrun_error_reg <= 1'b0;
            frame_error_reg   <= 1'b0;
        end else begin
            rxd_reg <= rxd;
            overrun_error_reg <= 1'b0;  // Pulse
            frame_error_reg <= 1'b0;    // Pulse
            
            // Clear rx_valid after 1 cycle (pulse) but only if bit_cnt==0
            // (don't clear on the same cycle we're setting it!)
            if (rx_valid_reg && bit_cnt == 0) begin
                rx_valid_reg <= 1'b0;
            end
            
            if (prescale_reg > 0) begin
                prescale_reg <= prescale_reg - 1;
            end else if (bit_cnt > 0) begin
                if (bit_cnt > 4'd10) begin
                    // Waiting for start bit to be confirmed
                    if (!rxd_reg) begin
                        bit_cnt <= bit_cnt - 1;
                        prescale_reg <= (PRESCALE << 3) - 1;
                    end else begin
                        // False start bit
                        bit_cnt <= '0;
                        prescale_reg <= '0;
                    end
                end else if (bit_cnt > 1) begin
                    // Data bits
                    bit_cnt <= bit_cnt - 1;
                    prescale_reg <= (PRESCALE << 3) - 1;
                    data_reg <= {rxd_reg, data_reg[7:1]};
                end else if (bit_cnt == 1) begin
                    // Stop bit
                    bit_cnt <= bit_cnt - 1;
                    if (rxd_reg) begin
                        // Valid stop bit
                        rx_data_reg <= data_reg;
                        rx_valid_reg <= 1'b1;
                        overrun_error_reg <= rx_valid_reg;  // Set if previous data not consumed
                    end else begin
                        // Framing error
                        frame_error_reg <= 1'b1;
                    end
                end
            end else begin
                if (!rxd_reg) begin
                    // Start bit detected - sync to middle
                    prescale_reg <= (PRESCALE << 2) - 2;  // 4x prescale - 2
                    bit_cnt <= 4'd10;  // 1 start + 8 data + 1 stop
                    data_reg <= '0;
                end
            end
        end
    end

endmodule
