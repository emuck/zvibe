/*
 * Servant ZVibe Multiplexer
 *
 * Address decoder for Servant ZVibe peripherals (EXT interface only)
 *
 * Based on servant_mux from SERV Servant SoC by Olof Kindgren (ISC License)
 * Modified to support 32-bit UART instead of 1-bit GPIO
 *
 * Modifications Copyright (c) 2025 Martin R. Raumann
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Memory map (EXT interface - peripherals only):
 * 0x40000000-0x4000000F: UART (32-bit wishbone peripheral)
 * 0x40000010-0x4000001F: Timer (32-bit wishbone peripheral)
 * 0x40000020-0x4000002F: GPIO LEDs (debug)
 * 0x40000030-0x4000003F: Flash Status (read-only)
 *
 * Note: RAM and Flash are on MEM interface via servant_mem_mux
 */

module servant_zvibe_mux (
    input logic [31:0] i_wb_cpu_adr,
    input logic [31:0] i_wb_cpu_dat,
    input logic        i_wb_cpu_we,
    input logic        i_wb_cpu_cyc,
    output logic [31:0] o_wb_cpu_rdt,
    output logic       o_wb_cpu_ack,

    // UART (32-bit interface)
    output logic [31:0] o_wb_uart_adr,
    output logic [31:0] o_wb_uart_dat,
    output logic        o_wb_uart_we,
    output logic        o_wb_uart_cyc,
    input logic [31:0]  i_wb_uart_rdt,
    input logic         i_wb_uart_ack,

    // Timer (32-bit interface)
    output logic [31:0] o_wb_timer_dat,
    output logic        o_wb_timer_we,
    output logic        o_wb_timer_cyc,
    input logic [31:0]  i_wb_timer_rdt,

    // GPIO LEDs (32-bit interface)
    output logic [31:0] o_wb_gpio_adr,
    output logic [31:0] o_wb_gpio_dat,
    output logic        o_wb_gpio_we,
    output logic        o_wb_gpio_cyc,
    input logic [31:0]  i_wb_gpio_rdt,
    input logic         i_wb_gpio_ack,

    // Flash Status (read-only interface)
    output logic        o_wb_flash_status_cyc,
    input logic [31:0]  i_wb_flash_status_rdt,
    input logic         i_wb_flash_status_ack
);

    // Fine decode within peripheral space (0x40000000)
    // Use address bits [5:4] for decoding (faster than full comparison)
    logic [1:0] periph_sel;
    assign periph_sel = i_wb_cpu_adr[5:4];

    logic sel_uart;
    logic sel_timer;
    logic sel_gpio;
    logic sel_flash_status;

    assign sel_uart         = (periph_sel == 2'b00);  // 0x40000000-0x4000000F
    assign sel_timer        = (periph_sel == 2'b01);  // 0x40000010-0x4000001F
    assign sel_gpio         = (periph_sel == 2'b10);  // 0x40000020-0x4000002F
    assign sel_flash_status = (periph_sel == 2'b11);  // 0x40000030-0x4000003F

    // Route signals to UART
    assign o_wb_uart_adr = i_wb_cpu_adr;
    assign o_wb_uart_dat = i_wb_cpu_dat;
    assign o_wb_uart_we  = i_wb_cpu_we;
    assign o_wb_uart_cyc = i_wb_cpu_cyc & sel_uart;

    // Route signals to Timer
    assign o_wb_timer_dat = i_wb_cpu_dat;
    assign o_wb_timer_we  = i_wb_cpu_we;
    assign o_wb_timer_cyc = i_wb_cpu_cyc & sel_timer;

    // Route signals to GPIO
    assign o_wb_gpio_adr = i_wb_cpu_adr;
    assign o_wb_gpio_dat = i_wb_cpu_dat;
    assign o_wb_gpio_we  = i_wb_cpu_we;
    assign o_wb_gpio_cyc = i_wb_cpu_cyc & sel_gpio;

    // Route signals to Flash Status (read-only)
    assign o_wb_flash_status_cyc = i_wb_cpu_cyc & sel_flash_status;

    // Multiplex read data using case statement (parallel synthesis)
    logic [31:0] cpu_rdt;
    always_comb begin
        unique case (periph_sel)
            2'b00:   cpu_rdt = i_wb_uart_rdt;
            2'b01:   cpu_rdt = i_wb_timer_rdt;
            2'b10:   cpu_rdt = i_wb_gpio_rdt;
            2'b11:   cpu_rdt = i_wb_flash_status_rdt;
            default: cpu_rdt = i_wb_uart_rdt;
        endcase
    end
    assign o_wb_cpu_rdt = cpu_rdt;

    // Multiplex acknowledgements using case statement (combinatorial)
    // Timer acknowledges immediately (combinational)
    // UART, GPIO, and Flash Status have registered acks
    logic cpu_ack;
    always_comb begin
        unique case (periph_sel)
            2'b00:   cpu_ack = i_wb_uart_ack;
            2'b01:   cpu_ack = i_wb_cpu_cyc;  // Timer acks immediately
            2'b10:   cpu_ack = i_wb_gpio_ack;
            2'b11:   cpu_ack = i_wb_flash_status_ack;
            default: cpu_ack = 1'b0;
        endcase
    end

    assign o_wb_cpu_ack = cpu_ack;

endmodule
