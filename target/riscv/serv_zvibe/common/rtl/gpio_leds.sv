/*
 * Simple GPIO module for debug LEDs
 *
 * Copyright (c) 2025 Martin R. Raumann
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Address 0x00: LED control (write) / LED readback (read)
 */


module gpio_leds (
    input  logic        i_wb_clk,
    input  logic        i_wb_rst,

    // Wishbone interface
    input  logic [31:0] i_wb_adr,
    input  logic [31:0] i_wb_dat,
    input  logic        i_wb_we,
    input  logic        i_wb_cyc,
    input  logic        i_wb_stb,
    output logic        o_wb_ack,
    output logic [31:0] o_wb_dat,

    // GPIO outputs
    output logic [2:0]  o_gpio_led
);

    // LED register (bits 0-2 control LD3, LD4, LD5)
    logic [2:0] led_reg;

    always_ff @(posedge i_wb_clk) begin
        if (i_wb_rst) begin
            led_reg  <= 3'b000;
            o_wb_ack <= 1'b0;
        end else begin
            o_wb_ack <= i_wb_cyc && i_wb_stb && !o_wb_ack;
            if (i_wb_cyc && i_wb_stb && i_wb_we)
                led_reg <= i_wb_dat[2:0];
        end
    end

    always_comb begin
        o_wb_dat   = {29'b0, led_reg};
        o_gpio_led = led_reg;
    end

endmodule

