///////////////////////////////////////////////////////////////////////////////
// s25fl_write.sv
//
// QSPI Flash Write Controller for S25FL128S
//
// Write-only controller for flash erase and program operations.
// Separate from XIP controller to keep read path simple and fast.
//
// Copyright (c) 2025 Martin R. Raumann
// SPDX-License-Identifier: BSD-3-Clause
//
// Features:
// - WREN (0x06) Write Enable command
// - SE (0xD8) Sector Erase (64KB sectors)
// - PP (0x02) Page Program (256 bytes per page)
// - RDSR (0x05) Read Status Register (for polling WIP bit)
// - Status polling with timeout
// - SPI mode (single IO, not QUAD)
//
// Interface:
// - Register-based control (not Wishbone)
// - Separate from XIP controller
// - Shares QSPI pins via qspi_mux
//
// FSM structure: two sequential blocks sharing state_q.
//   Block 1 (state machine): manages state_q, o_active, o_busy, o_error,
//            o_status, o_data_out, pending_se, pending_pp.
//   Block 2 (SPI transmission): manages bit_counter, byte_counter,
//            sck_edge_toggle, addr_reg, data_reg, status_reg, poll_counter,
//            byte_count, o_qspi_cs_n, o_qspi_oe, o_qspi_dat.
//
///////////////////////////////////////////////////////////////////////////////

`timescale 1ns / 1ps

module s25fl_write #(
    parameter CLK_DIV = 2              // QSPI clock divider (same as XIP controller)
) (
    // System interface
    input  logic        i_clk,           // System clock (100MHz)
    input  logic        i_reset,         // Synchronous reset (active high)

    // Control Interface (Register-based)
    input  logic        i_cmd_wren,      // Write enable command strobe
    input  logic        i_cmd_se,        // Sector erase command strobe
    input  logic        i_cmd_pp,        // Page program command strobe
    input  logic        i_cmd_rdsr,      // Read status register command strobe
    input  logic [23:0] i_address,       // Flash address (24-bit)
    input  logic [7:0]  i_data,          // Write data (for PP)
    input  logic        i_data_valid,    // Data valid strobe (for PP)

    // Status Outputs
    output logic        o_busy,          // Controller busy (operation in progress)
    output logic        o_error,         // Error occurred
    output logic [7:0]  o_status,        // Status register value (from RDSR)
    output logic [7:0]  o_data_out,      // Read data output (for RDSR)

    // QSPI Physical Interface (shared with XIP via mux)
    output logic        o_qspi_sck,      // Serial clock
    output logic        o_qspi_cs_n,     // Chip select (active low)
    output logic [3:0]  o_qspi_dat,      // Data to flash
    output logic [3:0]  o_qspi_oe,       // Output enable (per bit)
    input  logic [3:0]  i_qspi_dat,      // Data from flash

    // Arbitration
    output logic        o_active,        // Write controller has control (for mux)
    input  logic        i_xip_idle       // XIP controller is idle (can start write)
);

    //========================================================================
    // Initial Values for FPGA Configuration
    // CRITICAL: These ensure proper startup state before reset takes effect
    //========================================================================
    initial begin
        o_busy      = 1'b0;
        o_error     = 1'b0;
        o_status    = 8'h00;
        o_data_out  = 8'h00;
        o_qspi_sck  = 1'b0;
        o_qspi_cs_n = 1'b1;    // CS# deasserted (high) at startup
        o_qspi_dat  = 4'b0000;
        o_qspi_oe   = 4'b0000; // Tristate outputs at startup
        o_active    = 1'b0;    // Not active at startup - allows XIP to work
    end

    //========================================================================
    // Parameters and Constants
    //========================================================================

    // Calculate clock divider width
    localparam CLK_DIV_WIDTH = $clog2(2*CLK_DIV + 2);

    // State machine encoding
    typedef enum logic [3:0] {
        ST_IDLE,
        ST_WREN,
        ST_WREN_END,
        ST_SE_CMD,
        ST_SE_ADDR,
        ST_PP_CMD,
        ST_PP_ADDR,
        ST_PP_DATA,
        ST_RDSR_CMD,
        ST_RDSR_DATA,
        ST_POLL_STATUS,
        ST_WAIT_COMPLETE
    } state_t;

    // SPI Commands
    localparam logic [7:0] CMD_WREN = 8'h06;       // Write Enable
    localparam logic [7:0] CMD_SE   = 8'hD8;        // Sector Erase
    localparam logic [7:0] CMD_PP   = 8'h02;        // Page Program
    localparam logic [7:0] CMD_RDSR = 8'h05;        // Read Status Register

    // Status register bits
    localparam STATUS_WIP = 0;         // Write In Progress (bit 0)

    // Timeout values (at 166MHz)
    // Sector erase: up to 2 seconds = 333M cycles, use 500M for margin
    localparam TIMEOUT_ERASE = 500000000;   // Erase timeout (~3 seconds)

    //========================================================================
    // State and Internal Registers
    //========================================================================

    state_t state_q;

    // SPI transmission registers (owned by Block 2)
    logic [2:0]  bit_counter;      // Counts 0-7 bits
    logic [2:0]  byte_counter;     // Counts 0-N bytes (for multi-byte sequences)
    logic        sck_edge_toggle;  // Toggles between setup and sample edges

    // Address and data registers (owned by Block 2)
    logic [23:0] addr_reg;         // Address register
    logic [7:0]  data_reg;         // Data register
    logic [7:0]  byte_count;       // Number of bytes to program

    // Command sequence tracking (owned by Block 1)
    logic pending_se;              // SE command pending after WREN
    logic pending_pp;              // PP command pending after WREN

    // Status polling (shared: poll_counter owned by Block 2, status_reg by Block 2)
    logic [31:0] poll_counter;    // Poll timeout counter (32 bits for long erase times)
    logic [7:0]  status_reg;      // Status register value

    //========================================================================
    // Clock Divider (Block 0)
    //========================================================================

    logic [CLK_DIV_WIDTH-1:0] clk_div_counter;
    logic qspi_clk_enable;

    always_ff @(posedge i_clk) begin
        if (i_reset) begin
            clk_div_counter <= '0;
            qspi_clk_enable <= 1'b0;
        end else begin
            if (clk_div_counter >= (2*CLK_DIV + 1)) begin
                clk_div_counter <= '0;
                qspi_clk_enable <= 1'b1;
            end else begin
                clk_div_counter <= clk_div_counter + 1;
                qspi_clk_enable <= 1'b0;
            end
        end
    end

    //========================================================================
    // QSPI Clock Generation
    //========================================================================

    always_ff @(posedge i_clk) begin
        if (i_reset) begin
            o_qspi_sck <= 1'b0;
        end else if (qspi_clk_enable && o_active) begin
            o_qspi_sck <= ~o_qspi_sck;
        end else if (~o_active) begin
            o_qspi_sck <= 1'b0;  // Keep clock low when inactive
        end
    end

    //========================================================================
    // Block 1: State Machine
    // Owns: state_q, o_active, o_busy, o_error, o_status, o_data_out,
    //       pending_se, pending_pp
    //========================================================================

    always_ff @(posedge i_clk) begin
        if (i_reset) begin
            state_q    <= ST_IDLE;
            o_active   <= 1'b0;
            o_busy     <= 1'b0;
            o_error    <= 1'b0;
            o_status   <= 8'h00;
            o_data_out <= 8'h00;
            pending_se <= 1'b0;
            pending_pp <= 1'b0;
        end else begin
            unique case (state_q)
                ST_IDLE: begin
                    o_active <= 1'b0;
                    o_busy   <= 1'b0;

                    if (i_cmd_wren && i_xip_idle) begin
                        o_active   <= 1'b1;
                        o_busy     <= 1'b1;
                        pending_se <= 1'b0;
                        pending_pp <= 1'b0;
                        state_q    <= ST_WREN;
                    end else if (i_cmd_se && i_xip_idle) begin
                        o_active   <= 1'b1;
                        o_busy     <= 1'b1;
                        pending_se <= 1'b1;
                        pending_pp <= 1'b0;
                        state_q    <= ST_WREN;  // WREN first, then SE
                    end else if (i_cmd_pp && i_xip_idle) begin
                        o_active   <= 1'b1;
                        o_busy     <= 1'b1;
                        pending_se <= 1'b0;
                        pending_pp <= 1'b1;
                        state_q    <= ST_WREN;  // WREN first, then PP
                    end else if (i_cmd_rdsr && i_xip_idle) begin
                        o_active   <= 1'b1;
                        o_busy     <= 1'b1;
                        pending_se <= 1'b0;
                        pending_pp <= 1'b0;
                        state_q    <= ST_RDSR_CMD;
                    end
                end

                ST_WREN: begin
                    if (bit_counter == 3'd7 && qspi_clk_enable && sck_edge_toggle)
                        state_q <= ST_WREN_END;
                end

                ST_WREN_END: begin
                    if (bit_counter >= 3'd3) begin
                        if (pending_se) begin
                            pending_se <= 1'b0;
                            state_q    <= ST_SE_CMD;
                        end else if (pending_pp) begin
                            pending_pp <= 1'b0;
                            state_q    <= ST_PP_CMD;
                        end else begin
                            state_q <= ST_IDLE;
                        end
                    end
                end

                ST_SE_CMD: begin
                    if (bit_counter == 3'd7 && qspi_clk_enable && sck_edge_toggle)
                        state_q <= ST_SE_ADDR;
                end

                ST_SE_ADDR: begin
                    if (bit_counter == 3'd7 && qspi_clk_enable && sck_edge_toggle) begin
                        if (byte_counter == 3'd2)
                            state_q <= ST_POLL_STATUS;
                    end
                end

                ST_PP_CMD: begin
                    if (bit_counter == 3'd7 && qspi_clk_enable && sck_edge_toggle)
                        state_q <= ST_PP_ADDR;
                end

                ST_PP_ADDR: begin
                    if (bit_counter == 3'd7 && qspi_clk_enable && sck_edge_toggle) begin
                        if (byte_counter == 3'd2)
                            state_q <= ST_PP_DATA;
                    end
                end

                ST_PP_DATA: begin
                    if (bit_counter == 3'd7 && qspi_clk_enable && sck_edge_toggle)
                        state_q <= ST_POLL_STATUS;
                end

                ST_RDSR_CMD: begin
                    o_active <= 1'b1;
                    if (bit_counter == 3'd7 && qspi_clk_enable && sck_edge_toggle)
                        state_q <= ST_RDSR_DATA;
                end

                ST_RDSR_DATA: begin
                    o_active <= 1'b1;
                    if (bit_counter == 3'd7 && qspi_clk_enable && sck_edge_toggle) begin
                        o_status   <= status_reg;
                        o_data_out <= status_reg;
                        state_q    <= ST_POLL_STATUS;
                    end
                end

                ST_POLL_STATUS: begin
                    o_active <= 1'b0;
                    if (poll_counter >= TIMEOUT_ERASE) begin
                        o_error <= 1'b1;
                        state_q <= ST_IDLE;
                    end else if (!status_reg[STATUS_WIP]) begin
                        state_q <= ST_IDLE;
                    end else begin
                        state_q <= ST_RDSR_CMD;
                    end
                end

                ST_WAIT_COMPLETE: begin
                    state_q <= ST_IDLE;
                end

                default: state_q <= ST_IDLE;
            endcase
        end
    end

    //========================================================================
    // Block 2: SPI Transmission Logic
    // Owns: bit_counter, byte_counter, sck_edge_toggle, addr_reg, data_reg,
    //       status_reg, poll_counter, byte_count, o_qspi_cs_n, o_qspi_oe,
    //       o_qspi_dat
    //========================================================================

    always_ff @(posedge i_clk) begin
        if (i_reset) begin
            bit_counter     <= 3'd0;
            sck_edge_toggle <= 1'b0;
            byte_counter    <= 3'd0;
            addr_reg        <= 24'h000000;
            data_reg        <= 8'h00;
            status_reg      <= 8'h00;
            poll_counter    <= 32'h00000000;
            byte_count      <= 8'h00;
            o_qspi_cs_n     <= 1'b1;
            o_qspi_oe       <= 4'b0000;
            o_qspi_dat      <= 4'b0000;
        end else begin
            // Toggle edge counter for SPI timing
            if (qspi_clk_enable && o_active)
                sck_edge_toggle <= ~sck_edge_toggle;

            unique case (state_q)
                ST_IDLE: begin
                    bit_counter     <= 3'd0;
                    byte_counter    <= 3'd0;
                    poll_counter    <= 32'h00000000;
                    sck_edge_toggle <= 1'b0;
                    byte_count      <= 8'h00;

                    if ((i_cmd_se || i_cmd_pp || i_cmd_wren || i_cmd_rdsr) && i_xip_idle) begin
                        o_qspi_cs_n <= 1'b0;  // Assert CS# ready for WREN
                        o_qspi_oe   <= 4'b0001; // Drive IO0 (MOSI)
                    end else begin
                        o_qspi_cs_n <= 1'b1;
                        o_qspi_oe   <= 4'b0000;
                    end
                end

                ST_WREN: begin
                    o_qspi_cs_n    <= 1'b0;
                    o_qspi_oe      <= 4'b0001;
                    o_qspi_dat[0]  <= CMD_WREN[7 - bit_counter];

                    if (bit_counter == 3'd0) begin
                        status_reg <= 8'h01;  // WIP=1 forces poll loop
                        addr_reg   <= i_address;
                        data_reg   <= i_data;
                    end

                    if (qspi_clk_enable && sck_edge_toggle) begin
                        if (bit_counter < 3'd7)
                            bit_counter <= bit_counter + 1;
                        else
                            bit_counter <= 3'd0;
                    end
                end

                ST_WREN_END: begin
                    o_qspi_cs_n <= 1'b1;
                    o_qspi_oe   <= 4'b0000;
                    if (bit_counter >= 3'd3) begin
                        bit_counter  <= 3'd0;
                        byte_counter <= 3'd0;
                    end else begin
                        bit_counter <= bit_counter + 1;
                    end
                end

                ST_SE_CMD: begin
                    o_qspi_cs_n   <= 1'b0;
                    o_qspi_oe     <= 4'b0001;
                    o_qspi_dat[0] <= CMD_SE[7 - bit_counter];

                    if (qspi_clk_enable && sck_edge_toggle) begin
                        if (bit_counter < 3'd7)
                            bit_counter <= bit_counter + 1;
                        else begin
                            bit_counter  <= 3'd0;
                            byte_counter <= 3'd0;
                        end
                    end
                end

                ST_SE_ADDR: begin
                    o_qspi_cs_n <= 1'b0;
                    o_qspi_oe   <= 4'b0001;
                    unique case (byte_counter)
                        3'd0: o_qspi_dat[0] <= addr_reg[23 - bit_counter];
                        3'd1: o_qspi_dat[0] <= addr_reg[15 - bit_counter];
                        3'd2: o_qspi_dat[0] <= addr_reg[7  - bit_counter];
                        default: o_qspi_dat[0] <= 1'b0;
                    endcase

                    if (qspi_clk_enable && sck_edge_toggle) begin
                        if (bit_counter < 3'd7)
                            bit_counter <= bit_counter + 1;
                        else begin
                            bit_counter  <= 3'd0;
                            byte_counter <= byte_counter + 1;
                        end
                    end
                end

                ST_PP_CMD: begin
                    o_qspi_cs_n   <= 1'b0;
                    o_qspi_oe     <= 4'b0001;
                    o_qspi_dat[0] <= CMD_PP[7 - bit_counter];

                    if (qspi_clk_enable && sck_edge_toggle) begin
                        if (bit_counter < 3'd7)
                            bit_counter <= bit_counter + 1;
                        else begin
                            bit_counter  <= 3'd0;
                            byte_counter <= 3'd0;
                        end
                    end
                end

                ST_PP_ADDR: begin
                    o_qspi_cs_n <= 1'b0;
                    o_qspi_oe   <= 4'b0001;
                    unique case (byte_counter)
                        3'd0: o_qspi_dat[0] <= addr_reg[23 - bit_counter];
                        3'd1: o_qspi_dat[0] <= addr_reg[15 - bit_counter];
                        3'd2: o_qspi_dat[0] <= addr_reg[7  - bit_counter];
                        default: o_qspi_dat[0] <= 1'b0;
                    endcase

                    if (qspi_clk_enable && sck_edge_toggle) begin
                        if (bit_counter < 3'd7)
                            bit_counter <= bit_counter + 1;
                        else begin
                            bit_counter  <= 3'd0;
                            byte_counter <= byte_counter + 1;
                        end
                    end
                end

                ST_PP_DATA: begin
                    o_qspi_cs_n   <= 1'b0;
                    o_qspi_oe     <= 4'b0001;
                    o_qspi_dat[0] <= data_reg[7 - bit_counter];

                    if (qspi_clk_enable && sck_edge_toggle) begin
                        if (bit_counter < 3'd7)
                            bit_counter <= bit_counter + 1;
                        else begin
                            bit_counter <= 3'd0;
                            if (i_data_valid) begin
                                data_reg   <= i_data;
                                byte_count <= byte_count + 1;
                            end
                        end
                    end else if (i_data_valid && bit_counter == 3'd0) begin
                        data_reg <= i_data;
                    end
                end

                ST_RDSR_CMD: begin
                    o_qspi_cs_n   <= 1'b0;
                    o_qspi_oe     <= 4'b0001;
                    o_qspi_dat[0] <= CMD_RDSR[7 - bit_counter];

                    if (qspi_clk_enable && sck_edge_toggle) begin
                        if (bit_counter < 3'd7)
                            bit_counter <= bit_counter + 1;
                        else
                            bit_counter <= 3'd0;
                    end
                end

                ST_RDSR_DATA: begin
                    o_qspi_cs_n <= 1'b0;
                    o_qspi_oe   <= 4'b0000;  // Input mode (read from flash)
                    // Sample on rising edge, shift on falling edge
                    if (qspi_clk_enable && !sck_edge_toggle)
                        status_reg <= {status_reg[6:0], i_qspi_dat[1]};  // MISO is IO1

                    if (qspi_clk_enable && sck_edge_toggle) begin
                        if (bit_counter < 3'd7)
                            bit_counter <= bit_counter + 1;
                        else
                            bit_counter <= 3'd0;
                    end
                end

                ST_POLL_STATUS: begin
                    o_qspi_cs_n  <= 1'b1;
                    o_qspi_oe    <= 4'b0000;
                    poll_counter <= poll_counter + 1;
                end

                default: begin
                    o_qspi_cs_n <= 1'b1;
                    o_qspi_oe   <= 4'b0000;
                end
            endcase
        end
    end

endmodule

