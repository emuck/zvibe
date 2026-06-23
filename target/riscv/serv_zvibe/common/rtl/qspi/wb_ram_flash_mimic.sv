/*
 * wb_ram_flash_mimic.sv : Fast RAM that mimics flash address space
 *
 * Purpose: Test whether slow flash fetch causes dbus blocking.
 * This RAM responds in 1 cycle vs flash's ~234 cycles.
 *
 * Copyright (c) 2025 Martin R. Raumann
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Address mapping:
 *   CPU address 0x80400000-0x80FFFFFF → RAM offset 0x000000-0xBFFFFF
 */


module wb_ram_flash_mimic #(
    parameter DEPTH = 16384  // 16K words = 64KB (plenty for test programs)
) (
    input  logic        i_clk,
    input  logic        i_rst,

    // Wishbone slave interface
    input  logic        i_wb_cyc,
    input  logic [31:0] i_wb_addr,
    input  logic [31:0] i_wb_data,
    input  logic [3:0]  i_wb_sel,
    input  logic        i_wb_we,
    input  logic        i_wb_stb,
    output logic [31:0] o_wb_rdt,
    output logic        o_wb_ack
);

    // RAM storage - 32-bit words
    logic [31:0] ram [0:DEPTH-1];

    // Calculate word address from byte address
    // Remove flash base offset (0x80400000 → 0x400000), then convert to words
    logic [31:0] word_addr;
    assign word_addr = {10'b0, i_wb_addr[23:2]};

    // Simple 1-cycle read/write
    always_ff @(posedge i_clk) begin
        if (i_rst) begin
            o_wb_ack <= 1'b0;
            o_wb_rdt <= 32'h0;
        end else begin
            // ACK on any strobe when cyc is active
            o_wb_ack <= i_wb_cyc && i_wb_stb;

            if (i_wb_cyc && i_wb_stb && !o_wb_ack) begin
                if (i_wb_we) begin
                    // Write with byte enables
                    if (i_wb_sel[0]) ram[word_addr][7:0]   <= i_wb_data[7:0];
                    if (i_wb_sel[1]) ram[word_addr][15:8]  <= i_wb_data[15:8];
                    if (i_wb_sel[2]) ram[word_addr][23:16] <= i_wb_data[23:16];
                    if (i_wb_sel[3]) ram[word_addr][31:24] <= i_wb_data[31:24];

                    $display("[RAM_FLASH %t] WRITE: addr=0x%08h data=0x%08h sel=%b",
                             $time, i_wb_addr, i_wb_data, i_wb_sel);
                end else begin
                    // Read
                    o_wb_rdt <= ram[word_addr];

                    if (word_addr < 10) begin  // Debug first few reads
                        $display("[RAM_FLASH %t] READ: addr=0x%08h data=0x%08h (word_addr=0x%08h)",
                                 $time, i_wb_addr, ram[word_addr], word_addr);
                    end
                end
            end
        end
    end

    // Initialize from hex file for simulation only
    // For synthesis, RAM will be uninitialized (fine for MAX10 - bootloader loads code)
    `ifdef SIMULATION
    initial begin
        integer i;
        // Initialize to NOPs
        for (i = 0; i < DEPTH; i = i + 1) begin
            ram[i] = 32'h00000013;  // NOP (addi x0, x0, 0)
        end

        // Load firmware if file exists
        if (!$test$plusargs("no_program")) begin
            $display("[RAM_FLASH] Loading firmware from test_firmware_ram.hex");
            $readmemh("test_firmware_ram.hex", ram);
        end
    end
    `endif

endmodule

