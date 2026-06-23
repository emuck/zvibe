//============================================================================
// Wishbone UART with FIFOs
//
// Top-level UART module with Wishbone interface and TX/RX FIFOs
// Uses Servant-compatible Wishbone port naming for easy integration
//
// UART TX/RX cores based on algorithms from alexforencich/verilog-uart
// Original Copyright (c) 2015-2023 Alex Forencich - MIT License
// SPDX-License-Identifier: MIT
// https://github.com/alexforencich/verilog-uart
//
// Integration, Wishbone interface, and FIFO additions:
// Copyright (c) 2025 zvibe project - BSD-3-Clause
//
// Memory Map:
// 0x00: TX_DATA  - Write byte to transmit
// 0x04: RX_DATA  - Read received byte
// 0x08: STATUS   - Status register
//
// Status Register [31:0]:
// [0]:     tx_ready         - Can write to TX FIFO (!tx_full)
// [1]:     rx_ready         - Data available in RX FIFO (!rx_empty)
// [2]:     rx_frame_error   - RX framing error detected (sticky, clear on read)
// [3]:     rx_overrun_error - RX overrun detected (sticky, clear on read)
// [4]:     rx_fifo_overflow - RX FIFO overflow, data dropped (sticky, clear on read)
// [5]:     tx_fifo_overflow - TX write rejected, FIFO full (sticky, clear on read)
// [31:6]:  Reserved
//
// Error bits are sticky: Set on error, cleared when status register is read
//============================================================================

`timescale 1ns/1ps

module uart_wb #(
    parameter int PRESCALE   = 108,  // Fclk/(baud*8) - e.g., 100MHz/(115200*8)=108
    parameter int FIFO_DEPTH = 64    // TX/RX FIFO depth in bytes
) (
    // Wishbone interface (Servant-compatible naming)
    input  logic        i_wb_clk,
    input  logic        i_wb_rst,
    input  logic [31:0] i_wb_adr,
    input  logic [31:0] i_wb_dat,
    output logic [31:0] o_wb_dat,
    input  logic        i_wb_we,
    input  logic        i_wb_cyc,
    input  logic        i_wb_stb,
    output logic        o_wb_ack,
    
    // UART physical interface
    output logic        uart_txd,
    input  logic        uart_rxd
);

    //========================================================================
    // Address Decode
    //========================================================================
    
    localparam logic [2:0] ADDR_TX_DATA = 3'h0;  // 0x00
    localparam logic [2:0] ADDR_RX_DATA = 3'h1;  // 0x04
    localparam logic [2:0] ADDR_STATUS  = 3'h2;  // 0x08
    
    logic [2:0] reg_addr;
    assign reg_addr = {1'b0, i_wb_adr[3:2]};  // Word address from byte address
    
    //========================================================================
    // Internal Signals
    //========================================================================
    
    // TX FIFO signals
    logic [7:0] tx_fifo_wr_data, tx_fifo_rd_data;
    logic       tx_fifo_wr_en, tx_fifo_rd_en;
    logic       tx_fifo_full, tx_fifo_empty;
    /* verilator lint_off UNUSEDSIGNAL */
    logic [6:0] tx_fifo_count;  // Available but not used in this design
    /* verilator lint_on UNUSEDSIGNAL */

    // RX FIFO signals
    logic [7:0] rx_fifo_wr_data, rx_fifo_rd_data;
    logic       rx_fifo_wr_en, rx_fifo_rd_en;
    logic       rx_fifo_full, rx_fifo_empty;
    /* verilator lint_off UNUSEDSIGNAL */
    logic [6:0] rx_fifo_count;  // Available but not used in this design
    /* verilator lint_on UNUSEDSIGNAL */
    
    // RX staging valid flag (rx_fifo_rd_data is the staging register)
    logic       rx_staging_valid;
    
    // UART TX core signals
    logic [7:0] tx_core_data;
    logic       tx_core_valid, tx_core_ready;
    
    // UART RX core signals
    logic [7:0] rx_core_data;
    logic       rx_core_valid;
    logic       rx_core_frame_error;
    logic       rx_core_overrun_error;

    // Error status registers (sticky - cleared on status read)
    logic       rx_frame_error_sticky;
    logic       rx_overrun_error_sticky;
    logic       rx_fifo_overflow_sticky;
    logic       tx_fifo_overflow_sticky;
    
    //========================================================================
    // Wishbone Interface
    //========================================================================

    // Single-cycle acknowledge (combinational)
    // ACK only when transaction completes successfully:
    // - Reads: always succeed
    // - Writes to TX: only when FIFO not full
    assign o_wb_ack = i_wb_cyc && i_wb_stb &&
                      (!i_wb_we ||                          // Read always succeeds
                       (reg_addr != ADDR_TX_DATA) ||        // Write to non-TX reg always succeeds
                       !tx_fifo_full);                      // Write to TX only if FIFO has space
    
    // Write path: Wishbone → TX FIFO
    assign tx_fifo_wr_en = i_wb_we && i_wb_cyc && i_wb_stb && 
                           (reg_addr == ADDR_TX_DATA) && !tx_fifo_full;
    assign tx_fifo_wr_data = i_wb_dat[7:0];
    
    // RX staging state machine
    // Hides FIFO read latency by proactively popping into rx_fifo_rd_data
    typedef enum logic [1:0] {
        RX_IDLE,     // No data available
        RX_POPPING,  // Launching FIFO pop (rd_en asserted, 1 cycle delay)
        RX_VALID     // Data in rx_fifo_rd_data, ready for Wishbone
    } rx_stage_state_t;
    
    rx_stage_state_t rx_stage_state;
    
    always_ff @(posedge i_wb_clk) begin
        if (i_wb_rst) begin
            rx_stage_state <= RX_IDLE;
        end else begin
            case (rx_stage_state)
                RX_IDLE: begin
                    // No data, wait for FIFO
                    if (!rx_fifo_empty) begin
                        rx_stage_state <= RX_POPPING;
                    end
                end
                
                RX_POPPING: begin
                    // Pop launched, data will be valid next cycle
                    rx_stage_state <= RX_VALID;
                end
                
                RX_VALID: begin
                    // Data valid in rx_fifo_rd_data, rx_staging_valid=1
                    // Wait for Wishbone read
                    if (!i_wb_we && i_wb_cyc && i_wb_stb && (reg_addr == ADDR_RX_DATA)) begin
                        // Read detected, transition next cycle
                        if (!rx_fifo_empty) begin
                            rx_stage_state <= RX_POPPING;
                        end else begin
                            rx_stage_state <= RX_IDLE;
                        end
                    end
                end
                
                default: rx_stage_state <= RX_IDLE;
            endcase
        end
    end
    
    // rx_staging_valid = data ready in rx_fifo_rd_data
    assign rx_staging_valid = (rx_stage_state == RX_VALID);
    
    // Pop FIFO in POPPING state
    assign rx_fifo_rd_en = (rx_stage_state == RX_POPPING);
    
    // Read data multiplexer
    always_comb begin
        o_wb_dat = 32'h0;
        case (reg_addr)
            ADDR_RX_DATA: begin
                o_wb_dat = {24'h0, rx_fifo_rd_data};  // Direct from FIFO (staged)
            end
            
            ADDR_STATUS: begin
                o_wb_dat = {
                    26'h0,                    // [31:6] reserved
                    tx_fifo_overflow_sticky,  // [5] tx_fifo_overflow
                    rx_fifo_overflow_sticky,  // [4] rx_fifo_overflow
                    rx_overrun_error_sticky,  // [3] rx_overrun_error
                    rx_frame_error_sticky,    // [2] rx_frame_error
                    rx_staging_valid,         // [1] rx_ready
                    !tx_fifo_full             // [0] tx_ready
                };
            end
            
            default: begin
                o_wb_dat = 32'h0;
            end
        endcase
    end
    
    //========================================================================
    // TX Path: TX FIFO → TX Core → uart_txd
    //========================================================================
    
    // TX FIFO instantiation
    fifo_sync #(
        .DATA_WIDTH(8),
        .DEPTH(FIFO_DEPTH)
    ) tx_fifo_inst (
        .clk(i_wb_clk),
        .rst(i_wb_rst),
        .wr_data(tx_fifo_wr_data),
        .wr_en(tx_fifo_wr_en),
        .full(tx_fifo_full),
        .rd_data(tx_fifo_rd_data),
        .rd_en(tx_fifo_rd_en),
        .empty(tx_fifo_empty),
        .count(tx_fifo_count)
    );
    
    // TX FIFO → TX Core handshake
    // FIFO has 1-cycle read latency, so delay tx_core_valid to match
    logic tx_core_valid_reg;
    
    always_ff @(posedge i_wb_clk) begin
        if (i_wb_rst) begin
            tx_core_valid_reg <= 1'b0;
        end else begin
            if (tx_fifo_rd_en) begin
                tx_core_valid_reg <= 1'b1;  // Data will be valid next cycle
            end else if (tx_core_valid_reg && tx_core_ready) begin
                tx_core_valid_reg <= 1'b0;  // Handshake done, clear
            end
        end
    end
    
    assign tx_core_data = tx_fifo_rd_data;
    assign tx_core_valid = tx_core_valid_reg;
    assign tx_fifo_rd_en = !tx_fifo_empty && !tx_core_valid_reg;
    
    // UART TX core instantiation
    uart_tx #(
        .PRESCALE(PRESCALE)
    ) uart_tx_inst (
        .clk(i_wb_clk),
        .rst(i_wb_rst),
        .tx_data(tx_core_data),
        .tx_valid(tx_core_valid),
        .tx_ready(tx_core_ready),
        .txd(uart_txd)
    );
    
    //========================================================================
    // RX Path: uart_rxd → RX Core → RX FIFO
    //========================================================================
    
    // UART RX core instantiation
    uart_rx #(
        .PRESCALE(PRESCALE)
    ) uart_rx_inst (
        .clk(i_wb_clk),
        .rst(i_wb_rst),
        .rxd(uart_rxd),
        .rx_data(rx_core_data),
        .rx_valid(rx_core_valid),
        .frame_error(rx_core_frame_error),
        .overrun_error(rx_core_overrun_error)
    );
    
    // RX Core → RX FIFO
    // Only write to FIFO if not full (drop data if full)
    assign rx_fifo_wr_en = rx_core_valid && !rx_fifo_full;
    assign rx_fifo_wr_data = rx_core_data;
    
    // RX FIFO instantiation
    fifo_sync #(
        .DATA_WIDTH(8),
        .DEPTH(FIFO_DEPTH)
    ) rx_fifo_inst (
        .clk(i_wb_clk),
        .rst(i_wb_rst),
        .wr_data(rx_fifo_wr_data),
        .wr_en(rx_fifo_wr_en),
        .full(rx_fifo_full),
        .rd_data(rx_fifo_rd_data),
        .rd_en(rx_fifo_rd_en),
        .empty(rx_fifo_empty),
        .count(rx_fifo_count)
    );
    
    //========================================================================
    // Error Capture Logic
    //========================================================================

    // Sticky error registers - set on error, cleared on status read
    logic status_read;
    assign status_read = !i_wb_we && i_wb_cyc && i_wb_stb && (reg_addr == ADDR_STATUS);

    always_ff @(posedge i_wb_clk) begin
        if (i_wb_rst) begin
            rx_frame_error_sticky    <= 1'b0;
            rx_overrun_error_sticky  <= 1'b0;
            rx_fifo_overflow_sticky  <= 1'b0;
            tx_fifo_overflow_sticky  <= 1'b0;
        end else begin
            // RX frame error - set on error, clear on status read
            if (rx_core_frame_error) begin
                rx_frame_error_sticky <= 1'b1;
            end else if (status_read) begin
                rx_frame_error_sticky <= 1'b0;
            end

            // RX overrun error - set on error, clear on status read
            if (rx_core_overrun_error) begin
                rx_overrun_error_sticky <= 1'b1;
            end else if (status_read) begin
                rx_overrun_error_sticky <= 1'b0;
            end

            // RX FIFO overflow - set when valid data can't be written to FIFO
            if (rx_core_valid && rx_fifo_full) begin
                rx_fifo_overflow_sticky <= 1'b1;
            end else if (status_read) begin
                rx_fifo_overflow_sticky <= 1'b0;
            end

            // TX FIFO overflow - set when write attempted but FIFO full
            // Write attempted = write transaction to TX_DATA register
            if (i_wb_we && i_wb_cyc && i_wb_stb &&
                (reg_addr == ADDR_TX_DATA) && tx_fifo_full) begin
                tx_fifo_overflow_sticky <= 1'b1;
            end else if (status_read) begin
                tx_fifo_overflow_sticky <= 1'b0;
            end
        end
    end

    //========================================================================
    // Notes
    //========================================================================

    // Error handling: Error flags now exposed in status register
    // - RX frame error: Stop bit not detected (usually baud rate mismatch)
    // - RX overrun: New byte received before previous byte consumed by core
    // - RX FIFO overflow: Data dropped because RX FIFO full
    // - TX FIFO overflow: Write rejected because TX FIFO full
    //
    // All error flags are sticky (latched) and cleared on status register read

endmodule

