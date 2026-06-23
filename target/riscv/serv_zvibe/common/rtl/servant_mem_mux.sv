/*
 * Servant Memory Mux
 *
 * Address decoder for MEM interface to support RAM, Flash, and UFM Write
 *
 * Copyright (c) 2025 Martin R. Raumann
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Memory Map (MEM interface):
 * 0x00000000-0x1FFFFFFF: RAM
 * 0x80000000-0xBFFFFFFF: QSPI/UFM Flash (XIP read-only)
 * 0xC0000000-0xFFFFFFFF: UFM Write Bridge (erase/program operations)
 */

module servant_mem_mux (
    // CPU MEM interface
    input logic [31:0]  i_wb_cpu_adr,
    input logic [31:0]  i_wb_cpu_dat,
    input logic [3:0]   i_wb_cpu_sel,
    input logic         i_wb_cpu_we,
    input logic         i_wb_cpu_stb,
    output logic [31:0] o_wb_cpu_rdt,
    output logic        o_wb_cpu_ack,

    // RAM interface
    output logic [31:0] o_wb_ram_adr,
    output logic [31:0] o_wb_ram_dat,
    output logic [3:0]  o_wb_ram_sel,
    output logic        o_wb_ram_we,
    output logic        o_wb_ram_stb,
    input logic [31:0]  i_wb_ram_rdt,
    input logic         i_wb_ram_ack,

    // Flash interface (XIP read-only)
    output logic [31:0] o_wb_flash_adr,
    output logic [31:0] o_wb_flash_dat,
    output logic [3:0]  o_wb_flash_sel,
    output logic        o_wb_flash_we,
    output logic        o_wb_flash_cyc,
    output logic        o_wb_flash_stb,
    input logic [31:0]  i_wb_flash_rdt,
    input logic         i_wb_flash_ack,
    input logic         i_wb_flash_stall,

    // UFM Write interface (erase/program operations)
    output logic [31:0] o_wb_ufm_adr,
    output logic [31:0] o_wb_ufm_dat,
    output logic [3:0]  o_wb_ufm_sel,
    output logic        o_wb_ufm_we,
    output logic        o_wb_ufm_cyc,
    output logic        o_wb_ufm_stb,
    input logic [31:0]  i_wb_ufm_rdt,
    input logic         i_wb_ufm_ack,

    // Debug outputs
    output logic        o_debug_sel_ufm,
    output logic        o_debug_sel_flash,
    output logic        o_debug_cpu_stb
);

    // Address decode:
    // bit[31]=0: RAM (0x00000000-0x7FFFFFFF)
    // bit[31]=1, bit[30]=0: Flash XIP (0x80000000-0xBFFFFFFF)
    // bit[31]=1, bit[30]=1: UFM Write (0xC0000000-0xFFFFFFFF)
    // Changed from bit[25] to bit[30] for simpler, more reliable decode
    logic sel_ufm_write;
    logic sel_flash;
    logic sel_ram;

    assign sel_ufm_write = i_wb_cpu_adr[31] & i_wb_cpu_adr[30];
    assign sel_flash     = i_wb_cpu_adr[31] & !i_wb_cpu_adr[30];
    assign sel_ram       = !i_wb_cpu_adr[31];

    // Debug outputs
    assign o_debug_sel_ufm = sel_ufm_write;
    assign o_debug_sel_flash = sel_flash;
    assign o_debug_cpu_stb = i_wb_cpu_stb;

    // Route to RAM
    assign o_wb_ram_adr = i_wb_cpu_adr;
    assign o_wb_ram_dat = i_wb_cpu_dat;
    assign o_wb_ram_sel = i_wb_cpu_sel;
    assign o_wb_ram_we  = i_wb_cpu_we;
    assign o_wb_ram_stb = i_wb_cpu_stb & sel_ram;

    // Route to Flash (XIP read-only)
    assign o_wb_flash_adr = i_wb_cpu_adr;
    assign o_wb_flash_dat = i_wb_cpu_dat;
    assign o_wb_flash_sel = i_wb_cpu_sel;
    assign o_wb_flash_we  = i_wb_cpu_we;
    assign o_wb_flash_cyc = i_wb_cpu_stb & sel_flash;
    assign o_wb_flash_stb = i_wb_cpu_stb & sel_flash;

    // Route to UFM Write
    assign o_wb_ufm_adr = i_wb_cpu_adr;
    assign o_wb_ufm_dat = i_wb_cpu_dat;
    assign o_wb_ufm_sel = i_wb_cpu_sel;
    assign o_wb_ufm_we  = i_wb_cpu_we;
    assign o_wb_ufm_cyc = i_wb_cpu_stb & sel_ufm_write;
    assign o_wb_ufm_stb = i_wb_cpu_stb & sel_ufm_write;

    // Multiplex read data (3-way mux - parallel case for optimal timing)
    always_comb begin
        unique case (1'b1)
            sel_ufm_write: o_wb_cpu_rdt = i_wb_ufm_rdt;
            sel_flash:     o_wb_cpu_rdt = i_wb_flash_rdt;
            default:       o_wb_cpu_rdt = i_wb_ram_rdt;
        endcase
    end

    // Multiplex acknowledgements (3-way mux - parallel case for optimal timing)
    always_comb begin
        unique case (1'b1)
            sel_ufm_write: o_wb_cpu_ack = i_wb_ufm_ack;
            sel_flash:     o_wb_cpu_ack = i_wb_flash_ack;
            default:       o_wb_cpu_ack = i_wb_ram_ack;
        endcase
    end

endmodule
