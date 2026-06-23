///////////////////////////////////////////////////////////////////////////////
// max10_bootloader_fsm.v
//
// MAX10 Bootloader FSM
//
// State machine that copies boot stub code from UFM to RAM after FPGA
// configuration. Required because M9K RAM blocks cannot be pre-initialized
// when using compressed bitstreams.
//
// Copyright (c) 2025 Martin R. Raumann
// SPDX-License-Identifier: BSD-3-Clause
//
// Boot Sequence:
// 1. FPGA configures → FSM starts, CPU reset asserted
// 2. FSM reads boot stub from UFM (stored at BOOT_STUB_ADDR)
// 3. FSM writes boot stub to RAM starting at 0x00000000
// 4. FSM releases CPU reset
// 5. CPU begins execution from RAM
//
// Boot Stub Storage:
// - Stored in UFM2 (16KB) at word address BOOT_STUB_ADDR
// - Size: BOOT_STUB_WORDS (typically ~256 words = 1KB)
//
///////////////////////////////////////////////////////////////////////////////

`default_nettype none

module max10_bootloader_fsm #(
    parameter UFM_ADDR_WIDTH = 18,        // UFM address width
    parameter RAM_ADDR_WIDTH = 15,        // RAM address width (32KB = 15 bits)
    parameter BOOT_STUB_ADDR = 18'h04000, // Boot stub UFM address (word address)
    parameter BOOT_STUB_WORDS = 256,      // Boot stub size in words (1KB)
    parameter DATA_WIDTH = 32             // Data width (32 bits)
) (
    // System interface
    input  wire                    i_clk,
    input  wire                    i_reset,        // System reset (active high)

    // UFM Avalon-MM master interface
    output reg  [UFM_ADDR_WIDTH-1:0] o_ufm_address,
    output reg                     o_ufm_read,
    input  wire [DATA_WIDTH-1:0]   i_ufm_readdata,
    input  wire                    i_ufm_waitrequest,
    input  wire                    i_ufm_readdatavalid,

    // RAM write interface (direct M9K write)
    output reg  [RAM_ADDR_WIDTH-1:0] o_ram_address,
    output reg  [DATA_WIDTH-1:0]    o_ram_data,
    output reg                     o_ram_write_enable,

    // CPU reset control
    output reg                     o_cpu_reset_n,  // CPU reset (active low)

    // Status outputs
    output reg                     o_boot_complete,
    output reg  [2:0]              o_boot_state    // Debug state output
);

    //========================================================================
    // State Machine
    //========================================================================
    localparam STATE_RESET      = 3'd0;
    localparam STATE_INIT       = 3'd1;
    localparam STATE_READ_UFM   = 3'd2;
    localparam STATE_WAIT_DATA  = 3'd3;
    localparam STATE_WRITE_RAM  = 3'd4;
    localparam STATE_NEXT_WORD  = 3'd5;
    localparam STATE_COMPLETE   = 3'd6;

    reg [2:0] state;
    reg [RAM_ADDR_WIDTH-1:0] word_counter;  // Current word being copied
    reg [DATA_WIDTH-1:0] data_buffer;       // Data read from UFM

    //========================================================================
    // State Machine
    //========================================================================
    always @(posedge i_clk) begin
        if (i_reset) begin
            state <= STATE_RESET;
            word_counter <= {RAM_ADDR_WIDTH{1'b0}};
            o_ufm_address <= BOOT_STUB_ADDR;
            o_ufm_read <= 1'b0;
            o_ram_address <= {RAM_ADDR_WIDTH{1'b0}};
            o_ram_data <= {DATA_WIDTH{1'b0}};
            o_ram_write_enable <= 1'b0;
            o_cpu_reset_n <= 1'b0;  // CPU reset asserted
            o_boot_complete <= 1'b0;
            o_boot_state <= STATE_RESET;
        end else begin
            case (state)
                STATE_RESET: begin
                    // Wait a few cycles after system reset
                    state <= STATE_INIT;
                    o_boot_state <= STATE_INIT;
                end

                STATE_INIT: begin
                    // Initialize: assert CPU reset, prepare for copy
                    o_cpu_reset_n <= 1'b0;
                    word_counter <= {RAM_ADDR_WIDTH{1'b0}};
                    o_ufm_address <= BOOT_STUB_ADDR;
                    state <= STATE_READ_UFM;
                    o_boot_state <= STATE_READ_UFM;
                end

                STATE_READ_UFM: begin
                    // Issue read request to UFM
                    o_ufm_read <= 1'b1;
                    state <= STATE_WAIT_DATA;
                    o_boot_state <= STATE_WAIT_DATA;
                end

                STATE_WAIT_DATA: begin
                    // Wait for UFM to return data
                    o_ufm_read <= 1'b0;  // Deassert read after one cycle
                    
                    // Check for read data valid
                    if (i_ufm_readdatavalid) begin
                        // Data is ready - latch it
                        data_buffer <= i_ufm_readdata;
                        state <= STATE_WRITE_RAM;
                        o_boot_state <= STATE_WRITE_RAM;
                    end
                    // If waitrequest is still asserted, UFM is busy - stay in WAIT_DATA
                    // If waitrequest deasserted but readdatavalid not yet asserted,
                    // data is coming - stay in WAIT_DATA
                    // (no explicit else needed - state machine stays in WAIT_DATA)
                end

                STATE_WRITE_RAM: begin
                    // Write data to RAM
                    o_ram_address <= word_counter;
                    o_ram_data <= data_buffer;
                    o_ram_write_enable <= 1'b1;
                    state <= STATE_NEXT_WORD;
                    o_boot_state <= STATE_NEXT_WORD;
                end

                STATE_NEXT_WORD: begin
                    // Complete write, prepare for next word
                    o_ram_write_enable <= 1'b0;
                    word_counter <= word_counter + 1'b1;
                    
                    if (word_counter >= (BOOT_STUB_WORDS - 1)) begin
                        // All words copied
                        state <= STATE_COMPLETE;
                        o_boot_state <= STATE_COMPLETE;
                    end else begin
                        // More words to copy
                        o_ufm_address <= BOOT_STUB_ADDR + (word_counter + 1'b1);
                        state <= STATE_READ_UFM;
                        o_boot_state <= STATE_READ_UFM;
                    end
                end

                STATE_COMPLETE: begin
                    // Boot complete: release CPU reset
                    o_cpu_reset_n <= 1'b1;
                    o_boot_complete <= 1'b1;
                    o_boot_state <= STATE_COMPLETE;
                    // Stay in COMPLETE state
                end

                default: begin
                    state <= STATE_RESET;
                    o_boot_state <= STATE_RESET;
                end
            endcase
        end
    end

endmodule
