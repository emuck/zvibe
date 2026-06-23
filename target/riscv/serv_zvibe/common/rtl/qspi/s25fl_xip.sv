///////////////////////////////////////////////////////////////////////////////
// s25fl_xip.sv
//
// QSPI XIP Controller for S25FL128S Flash Memory
//
// Execute-In-Place (XIP) controller for RISC-V code execution from QSPI flash.
// Read-only controller - writes handled by s25fl_write.v.
//
// Copyright (c) 2025 Martin R. Raumann
// SPDX-License-Identifier: BSD-3-Clause
//
// Features:
// - QIOR (0xEB) Quad I/O Read command
// - Automatic QUAD mode configuration (CR1[1]=1 via WREN/WRR)
// - Wishbone B4 slave interface (read-only)
// - 6 dummy cycles (S25FL128S default for LC=00)
// - Works on both FPGA hardware and Verilator simulation
// - BURST_WORDS=4 returns a 128-bit aligned fetch per transaction (for cache fills)
//
// Protocol note: S25FL128S QIOR requires a mode byte after the address.
// We always send 0x00 (non-continuous). Continuous read mode was evaluated
// but removed — unreliable on hardware, unnecessary for UART-latency games.
//
// Refactored to SystemVerilog:
// - All ports and signals use explicit logic/wire types
// - All reg/wire → logic
// - always_ff / always_comb
// - Typed enum state machine (state_t)
// - return_state [4:0] replaced with tx_is_xip / wren_sent flags
// - nibbles_to_send [3:0] replaced with hardcoded constants
// - bytes_to_send [2:0] replaced with inline expression
// - recovery_counter [7:0] replaced with single-bit recovery_done
// - ST_READ_MODE quad output drives constant 4'h0 (mode byte is always 0x00)
//
///////////////////////////////////////////////////////////////////////////////

`timescale 1ns/1ps

module s25fl_xip #(
    parameter CLK_DIV = 2,              // QSPI clock divider
    parameter FLASH_ADDR_WIDTH = 24,    // 16MB flash = 24-bit address
    parameter WRR_WAIT_CYCLES = 16'd65535,  // WRR completion wait (65535 for HW, ~100 for sim)
    parameter BURST_WORDS = 1           // Words per read: 1 (single) or 4 (128-bit burst for cache)
) (
    // System interface
    input  logic        i_clk,          // System clock (100MHz)
    input  logic        i_reset,        // Synchronous reset (active high)

    // Wishbone slave interface (read-only XIP)
    // o_wb_data width is BURST_WORDS*32: [31:0]=word0 .. [127:96]=word3 for BURST_WORDS=4
    input  logic        i_wb_cyc,       // Cycle valid
    input  logic        i_wb_stb,       // Strobe
    input  logic [31:0] i_wb_addr,      // Byte address (16-byte-aligned when BURST_WORDS=4)
    output logic [BURST_WORDS*32-1:0] o_wb_data,  // Read data
    output logic        o_wb_ack,       // Acknowledge
    output logic        o_wb_stall,     // Stall (busy)

    // QSPI physical interface
    output logic        o_qspi_sck,     // Serial clock
    output logic        o_qspi_cs_n,    // Chip select (active low)
    output logic [3:0]  o_qspi_dat,     // Data to flash
    output logic [3:0]  o_qspi_oe,      // Output enable (per bit)
    input  logic [3:0]  i_qspi_dat,     // Data from flash

    // Status outputs
    output logic        o_startup_busy, // High during initialization
    output wire  [4:0]  o_state         // Current FSM state (5-bit debug)
);

    //========================================================================
    // Parameters and Constants
    //========================================================================

    // Calculate clock divider width
    // Need to count up to 2*CLK_DIV + 1
    localparam CLK_DIV_WIDTH = $clog2(2*CLK_DIV + 2);

    // Derived: nibbles to read per transaction (8 nibbles per 32-bit word)
    localparam DATA_NIBBLES = BURST_WORDS * 8;

    // SPI/QSPI Commands
    localparam logic [7:0] CMD_WREN  = 8'h06; // Write Enable
    localparam logic [7:0] CMD_WRR   = 8'h01; // Write Register
    localparam logic [7:0] CMD_RDSR1 = 8'h05; // Read Status Register 1
    localparam logic [7:0] CMD_QIOR  = 8'hEB; // Quad I/O Read

    //========================================================================
    // State Machine Encoding
    //========================================================================

    // State encoding: 5-bit sequential values.
    // NOTE: ST_READ_RECOVERY = 5'd15 (NOT 5'd12 — avoids collision with ST_SPI_TX_START).
    typedef enum logic [4:0] {
        ST_STARTUP_RESET  = 5'd0,
        ST_STARTUP_WREN   = 5'd1,
        ST_STARTUP_WRR    = 5'd2,
        ST_STARTUP_WAIT   = 5'd3,
        ST_IDLE           = 5'd4,
        ST_READ_CMD       = 5'd5,
        ST_READ_ADDR      = 5'd6,
        ST_LOAD_MODE_BYTE = 5'd7,
        ST_READ_MODE      = 5'd8,
        ST_READ_DUMMY     = 5'd9,
        ST_READ_DATA      = 5'd10,
        ST_READ_DONE      = 5'd11,
        ST_SPI_TX_START   = 5'd12,
        ST_SPI_TX_BITS    = 5'd13,
        ST_SPI_TX_END     = 5'd14,
        ST_READ_RECOVERY  = 5'd15   // was 5'd12 — collision with ST_SPI_TX_START fixed
    } state_t;

    state_t state_q, state_d;

    //========================================================================
    // Internal Signal Declarations
    // All module-level logic signals declared here.
    //========================================================================

    // Clock divider
    logic [CLK_DIV_WIDTH-1:0] clk_counter;
    logic                     qspi_clk_enable;

    // Startup / wait counter (shared between ST_STARTUP_RESET and ST_STARTUP_WAIT)
    logic [15:0] wait_counter;

    // SCK enable — REGISTERED (not combinational) to maintain SCK timing.
    // Changing sck_active to combinational would alter SCK phase and break the protocol.
    // Reading sck_active directly in always_comb is safe: it is a registered output.
    logic sck_active;      // Registered SCK enable (drives SCK toggle logic)

    // SPI bit counter and edge phase
    logic [2:0] bit_counter;      // Counts 0-7 bits in SPI byte transmission
    logic       sck_edge_toggle;  // Toggles: 0=setup edge, 1=sample edge

    // SPI multi-byte counter (used for WRR 3-byte sequence)
    logic [2:0] byte_counter;     // Counts bytes in multi-byte commands (0-7)

    // Control flags — replace 5-bit return_state register:
    //   tx_is_xip  : 1 = this SPI TX is for READ_CMD; CS# stays low, next = READ_ADDR
    //   wren_sent  : 1 = WREN SPI sequence completed; next SPI is WRR
    logic tx_is_xip;
    logic wren_sent;

    // QSPI nibble and dummy counters
    logic [4:0] nibble_counter;   // Counts nibbles in QUAD mode; 5 bits for burst (0-31)
    logic [2:0] dummy_counter;    // Counts dummy cycles (0-7) during READ_DUMMY

    // SPI/QUAD shift registers and captured address
    logic [7:0]  tx_shift_reg;    // SPI transmit shift register (drives MOSI)
    logic [23:0] quad_shift_reg;  // QUAD nibble shift register (address phase output)
    logic [23:0] addr_reg;        // Flash address captured from Wishbone request
    logic [BURST_WORDS*32-1:0] data_shift_reg;  // Data read shift register (32 or 128 bits)

    // Wishbone transaction tracking
    logic cyc_acked;        // Current i_wb_cyc has been acknowledged (prevents duplicate ACK)
    logic recovery_done;    // 1-bit flag: set after one cycle in ST_READ_RECOVERY

    //========================================================================
    // State Machine Sequential Logic — state register only
    //========================================================================

    always_ff @(posedge i_clk)
        if (i_reset) state_q <= ST_STARTUP_RESET;
        else         state_q <= state_d;

    //========================================================================
    // State Machine Combinational Logic (Next State)
    //========================================================================

    always_comb begin
        // Default: stay in current state
        state_d = state_q;

        unique case (state_q)
            ST_STARTUP_RESET: begin
                // Wait for power-on stabilization (256 cycles minimum)
                if (wait_counter >= 16'd255)
                    state_d = ST_STARTUP_WREN;
            end

            ST_STARTUP_WREN: begin
                // Start SPI transmission of WREN command
                state_d = ST_SPI_TX_START;
            end

            ST_SPI_TX_START: begin
                // Wait one cycle for CS# to assert and data setup
                state_d = ST_SPI_TX_BITS;
            end

            ST_SPI_TX_BITS: begin
                // Transmit 8 bits; done when bit 7 has been sampled
                if (qspi_clk_enable && sck_edge_toggle && bit_counter == 3'd7) begin
                    // WRR sends 3 bytes; all other commands send 1.
                    // more_bytes is true only during WRR (not xip, WREN already sent).
                    if (!tx_is_xip && wren_sent && (byte_counter < 3'd2))
                        state_d = ST_SPI_TX_BITS;  // More WRR bytes remaining
                    else
                        state_d = ST_SPI_TX_END;   // All bytes sent
                end
            end

            ST_SPI_TX_END: begin
                // tx_is_xip  → CS# stays low, go to READ_ADDR (QIOR command sent)
                // wren_sent  → WREN+WRR done, wait for WRR completion
                // !wren_sent → WREN just completed, now send WRR
                state_d = tx_is_xip  ? ST_READ_ADDR   :
                          wren_sent  ? ST_STARTUP_WAIT :
                                       ST_STARTUP_WRR;
            end

            ST_STARTUP_WRR: begin
                // Send WRR command (3 bytes)
                state_d = ST_SPI_TX_START;
            end

            ST_STARTUP_WAIT: begin
                // Wait for WRR to complete (~2ms for real hardware)
                if (wait_counter >= WRR_WAIT_CYCLES)
                    state_d = ST_IDLE;
            end

            ST_IDLE: begin
                if (i_wb_cyc && i_wb_stb && !cyc_acked)
                    state_d = ST_READ_CMD;
            end

            ST_READ_CMD: begin
                // Send QIOR command in SPI mode
                state_d = ST_SPI_TX_START;
            end

            ST_READ_ADDR: begin
                // Send 24-bit address in QUAD mode (6 nibbles).
                // Transition when all nibbles are sent and SCK has gone idle.
                // sck_active is already registered; reading it here is safe (no comb loop).
                // Use !sck_active (not !sck_edge_toggle) — unambiguous: prevents false trigger
                // when qspi_clk_enable fires on first cycle with sck_active=0 inherited.
                if (!sck_active && nibble_counter == 5'd6)
                    state_d = ST_LOAD_MODE_BYTE;
            end

            ST_LOAD_MODE_BYTE: begin
                // 1-cycle pause after address phase; mode byte output starts next cycle
                state_d = ST_READ_MODE;
            end

            ST_READ_MODE: begin
                // Send mode byte 0x00 in QUAD mode (2 nibbles)
                if (qspi_clk_enable && !sck_edge_toggle && nibble_counter == 5'd1)
                    state_d = ST_READ_DUMMY;
            end

            ST_READ_DUMMY: begin
                // 6 dummy cycles for S25FL128S (LC=0 default)
                // Transition on setup edge so next sample edge captures first data nibble
                if (qspi_clk_enable && !sck_edge_toggle && dummy_counter == 3'd5)
                    state_d = ST_READ_DATA;
            end

            ST_READ_DATA: begin
                // Read DATA_NIBBLES nibbles (8 per word) in QUAD mode
                if (qspi_clk_enable && sck_edge_toggle &&
                        nibble_counter == DATA_NIBBLES - 1)
                    state_d = ST_READ_DONE;
            end

            ST_READ_DONE: begin
                // Assert Wishbone ACK; transition to CS# recovery
                state_d = ST_READ_RECOVERY;
            end

            ST_READ_RECOVERY: begin
                // CS# recovery delay — wait one cycle (recovery_done set on first cycle here)
                state_d = recovery_done ? ST_IDLE : ST_READ_RECOVERY;
            end

            default: begin
                state_d = ST_IDLE;
            end
        endcase
    end

    //========================================================================
    // Clock Divider
    // Generates qspi_clk_enable pulse at QSPI clock rate.
    // Target: 100MHz / (2*CLK_DIV + 2) = 100MHz / 6 ≈ 16.67MHz (CLK_DIV=2)
    //========================================================================

    always_ff @(posedge i_clk) begin
        if (i_reset) begin
            clk_counter     <= 2*CLK_DIV + 1;  // Count from (2*CLK_DIV+1) down to 0
            qspi_clk_enable <= 1'b0;
        end else begin
            if (clk_counter == 0) begin
                clk_counter     <= 2*CLK_DIV + 1;  // Reload for next period
                qspi_clk_enable <= 1'b1;
            end else begin
                clk_counter     <= clk_counter - 1'b1;
                qspi_clk_enable <= 1'b0;
            end
        end
    end

    //========================================================================
    // QSPI SCK Generation
    // Toggles at half the qspi_clk_enable rate when sck_active is asserted.
    // Holds low when sck_active is deasserted (CS# idle or inter-phase gap).
    //========================================================================

    always_ff @(posedge i_clk) begin
        if (i_reset) begin
            o_qspi_sck <= 1'b0;
        end else begin
            if (qspi_clk_enable && sck_active)
                o_qspi_sck <= ~o_qspi_sck;
            else if (!sck_active)
                o_qspi_sck <= 1'b0;  // Hold low when inactive
        end
    end

    //========================================================================
    // Wait Counter
    // Used for ST_STARTUP_RESET stabilization and ST_STARTUP_WAIT (WRR completion).
    // Resets to 0 in all other states.
    //========================================================================

    always_ff @(posedge i_clk) begin
        if (i_reset) begin
            wait_counter <= 16'd0;
        end else begin
            unique case (state_q)
                ST_STARTUP_RESET: begin
                    if (wait_counter < 16'd255)
                        wait_counter <= wait_counter + 16'd1;
                end

                ST_STARTUP_WAIT: begin
                    // Wait for WRR to complete; configurable via WRR_WAIT_CYCLES
                    if (wait_counter < WRR_WAIT_CYCLES)
                        wait_counter <= wait_counter + 16'd1;
                end

                default: begin
                    wait_counter <= 16'd0;
                end
            endcase
        end
    end

    //========================================================================
    // SPI Bit Counter and SCK Edge Phase
    // bit_counter counts 0-7 within a byte transmission.
    // sck_edge_toggle alternates between setup (0) and sample (1) edges.
    //========================================================================

    always_ff @(posedge i_clk) begin
        if (i_reset) begin
            bit_counter     <= 3'd0;
            sck_edge_toggle <= 1'b0;
        end else begin
            unique case (state_q)
                ST_SPI_TX_START: begin
                    // Initialize for transmission
                    bit_counter     <= 3'd0;
                    sck_edge_toggle <= 1'b0;
                end

                ST_SPI_TX_BITS: begin
                    if (qspi_clk_enable) begin
                        // Toggle between setup (0) and sample (1) edges
                        sck_edge_toggle <= ~sck_edge_toggle;
                        // Increment bit counter on sample edge (toggle going 0→1)
                        if (sck_edge_toggle)
                            bit_counter <= bit_counter + 3'd1;
                    end
                end

                ST_READ_ADDR, ST_READ_MODE, ST_READ_DUMMY, ST_READ_DATA: begin
                    // sck_edge_toggle drives QSPI nibble and data sampling timing
                    if (qspi_clk_enable)
                        sck_edge_toggle <= ~sck_edge_toggle;
                end

                default: begin
                    bit_counter     <= 3'd0;
                    sck_edge_toggle <= 1'b0;
                end
            endcase
        end
    end

    //========================================================================
    // QSPI Nibble Counter
    // Counts nibbles transmitted/received in QUAD mode.
    // 5 bits wide to support burst of 32 nibbles (BURST_WORDS=4).
    //========================================================================

    always_ff @(posedge i_clk) begin
        if (i_reset) begin
            nibble_counter <= 5'd0;
        end else begin
            unique case (state_q)
                ST_READ_CMD: begin
                    // Initialize for address transmission (6 nibbles for 24-bit address)
                    nibble_counter <= 5'd0;
                end

                ST_READ_ADDR: begin
                    // Increment counter until all 6 nibbles are sent.
                    // Gate on sck_active: prevents spurious increment on the first clock when
                    // sck_active=0 (inherited from SPI_TX_END) but qspi_clk_enable fires.
                    if (qspi_clk_enable && sck_active && !sck_edge_toggle && nibble_counter < 5'd6)
                        nibble_counter <= nibble_counter + 5'd1;
                end

                ST_LOAD_MODE_BYTE: begin
                    // Reset counter for mode byte transmission (2 nibbles)
                    nibble_counter <= 5'd0;
                end

                ST_READ_MODE: begin
                    // Transitioning to READ_DUMMY: reset for data read phase
                    if (state_d == ST_READ_DUMMY)
                        nibble_counter <= 5'd0;
                    // Otherwise increment on setup edge
                    else if (qspi_clk_enable && !sck_edge_toggle)
                        nibble_counter <= nibble_counter + 5'd1;
                end

                ST_READ_DATA: begin
                    // Increment on sample edge (data captured from flash)
                    if (qspi_clk_enable && sck_edge_toggle)
                        nibble_counter <= nibble_counter + 5'd1;
                end

                ST_IDLE: begin
                    nibble_counter <= 5'd0;
                end

                default: begin
                    nibble_counter <= 5'd0;
                end
            endcase
        end
    end

    //========================================================================
    // Wishbone CYC Tracking + Recovery Done Flag
    //
    // cyc_acked prevents re-processing the same CYC assertion after ACK.
    // Classic Wishbone allows master to hold CYC/STB for a few cycles after ACK.
    //
    // recovery_done replaces recovery_counter [7:0]:
    //   0 → recovery not yet complete
    //   1 → one cycle elapsed in ST_READ_RECOVERY (tSHSL satisfied)
    //========================================================================

    always_ff @(posedge i_clk) begin
        if (i_reset) begin
            cyc_acked    <= 1'b0;
            recovery_done <= 1'b0;
        end else begin
            // cyc_acked: clear when CYC deasserts (allows new transaction)
            if (!i_wb_cyc)
                cyc_acked <= 1'b0;
            else if (state_q == ST_READ_DONE)
                cyc_acked <= 1'b1;  // Set when ACK is being issued

            // recovery_done: cleared when entering DONE, set after one cycle in RECOVERY
            if (state_q == ST_READ_DONE)
                recovery_done <= 1'b0;
            else if (state_q == ST_READ_RECOVERY)
                recovery_done <= 1'b1;
        end
    end

    //========================================================================
    // SPI Transmit Shift Register
    // Shifts out data on MOSI (qspi_dat[0]) MSB-first.
    //
    // tx_is_xip and wren_sent replace the 5-bit return_state register:
    //   tx_is_xip : set in ST_READ_CMD; after TX end, CS# stays low → READ_ADDR
    //   wren_sent : set in ST_SPI_TX_END when WREN just completed (!tx_is_xip && !wren_sent)
    //
    // Also captures the Wishbone address into addr_reg and drives byte_counter.
    //========================================================================

    always_ff @(posedge i_clk) begin
        if (i_reset) begin
            tx_shift_reg <= 8'h00;
            byte_counter <= 3'd0;
            tx_is_xip    <= 1'b0;
            wren_sent    <= 1'b0;
            addr_reg     <= 24'h000000;
        end else begin
            // Capture address on Wishbone request while IDLE
            if (i_wb_cyc && i_wb_stb && state_q == ST_IDLE && !cyc_acked)
                addr_reg <= {i_wb_addr[23:2], 2'b00};  // Word-align flash address

            unique case (state_q)
                ST_STARTUP_WREN: begin
                    // Load WREN command (1 byte); next SPI TX is WRR (not XIP)
                    tx_shift_reg <= CMD_WREN;  // 0x06
                    byte_counter <= 3'd0;
                    tx_is_xip   <= 1'b0;
                end

                ST_STARTUP_WRR: begin
                    // Load WRR command first byte (3 bytes total); next = STARTUP_WAIT
                    tx_shift_reg <= CMD_WRR;   // 0x01
                    byte_counter <= 3'd0;
                    tx_is_xip   <= 1'b0;
                end

                ST_READ_CMD: begin
                    // Load QIOR command (0xEB); CS# stays low after TX → READ_ADDR
                    tx_shift_reg <= CMD_QIOR;  // 0xEB
                    byte_counter <= 3'd0;
                    tx_is_xip   <= 1'b1;
                end

                ST_SPI_TX_START: begin
                    // Hold shift register value unchanged during CS# assert cycle
                    tx_shift_reg <= tx_shift_reg;
                end

                ST_SPI_TX_BITS: begin
                    // CRITICAL TIMING: Shift on SAMPLE edge (after flash has sampled MOSI).
                    // This prevents race conditions on FPGA hardware and in simulation.
                    // Do not shift on bit 7; byte-transition logic handles that separately.
                    if (qspi_clk_enable && sck_edge_toggle && bit_counter != 3'd7)
                        tx_shift_reg <= {tx_shift_reg[6:0], 1'b0};

                    // When byte is complete (sample edge of bit 7), load next byte if multi-byte
                    if (qspi_clk_enable && sck_edge_toggle && bit_counter == 3'd7) begin
                        // more_bytes: true only during WRR (3 bytes: 0x01, 0x00, 0x02)
                        if (!tx_is_xip && wren_sent && (byte_counter < 3'd2)) begin
                            byte_counter <= byte_counter + 3'd1;
                            // WRR byte sequence: byte0=0x01(CMD), byte1=0x00(SR), byte2=0x02(CR1)
                            if (byte_counter == 3'd0)
                                tx_shift_reg <= 8'h00;  // Status Register 1 value
                            else if (byte_counter == 3'd1)
                                tx_shift_reg <= 8'h02;  // Config Register 1 (QUAD bit)
                        end
                    end
                end

                ST_SPI_TX_END: begin
                    // WREN just completed when neither XIP nor already-sent
                    if (!tx_is_xip && !wren_sent)
                        wren_sent <= 1'b1;
                end

                default: begin
                    tx_shift_reg <= 8'h00;
                end
            endcase
        end
    end

    //========================================================================
    // QSPI Quad Shift Register
    // Shifts out address nibbles (4 bits at a time) in QUAD mode.
    // Mode byte (0x00) is output as a hardcoded constant in the output block —
    // no shift register assignment needed for ST_READ_MODE.
    //========================================================================

    always_ff @(posedge i_clk) begin
        if (i_reset) begin
            quad_shift_reg <= 24'h000000;
        end else begin
            unique case (state_q)
                ST_SPI_TX_END: begin
                    // Pre-load address when about to transition to READ_ADDR
                    if (state_d == ST_READ_ADDR)
                        quad_shift_reg <= addr_reg;
                end

                ST_READ_ADDR: begin
                    // Shift on setup edges (rising SCK) while sending address nibbles.
                    // Gate on sck_active to prevent spurious shift when sck_active=0
                    // (inherited from SPI_TX_END) but qspi_clk_enable fires.
                    if (qspi_clk_enable && sck_active && !sck_edge_toggle && nibble_counter < 5'd6)
                        quad_shift_reg <= {quad_shift_reg[19:0], 4'h0};
                end

                // ST_LOAD_MODE_BYTE: no assignment — mode byte is constant 4'h0
                // ST_READ_MODE:      no shift needed — output block drives 4'h0 directly

                default: begin
                    // Hold value in other states
                end
            endcase
        end
    end

    //========================================================================
    // Dummy Cycle Counter
    // Counts 6 dummy cycles required by S25FL128S QIOR (LC=00 default).
    //========================================================================

    always_ff @(posedge i_clk) begin
        if (i_reset) begin
            dummy_counter <= 3'd0;
        end else begin
            unique case (state_q)
                ST_READ_MODE: begin
                    // Reset dummy counter when transitioning to READ_DUMMY
                    if (state_d == ST_READ_DUMMY)
                        dummy_counter <= 3'd0;
                end

                ST_READ_DUMMY: begin
                    if (qspi_clk_enable && sck_edge_toggle)
                        dummy_counter <= dummy_counter + 3'd1;
                end

                default: begin
                    dummy_counter <= 3'd0;
                end
            endcase
        end
    end

    //========================================================================
    // QSPI Data Read Shift Register
    // Captures nibbles from the flash during the READ_DATA phase.
    // Shift register is 32 bits (BURST_WORDS=1) or 128 bits (BURST_WORDS=4).
    //========================================================================

    always_ff @(posedge i_clk) begin
        if (i_reset) begin
            data_shift_reg <= {BURST_WORDS*32{1'b0}};
        end else begin
            unique case (state_q)
                ST_READ_DATA: begin
                    // Sample nibbles on the sample edge
                    if (qspi_clk_enable && sck_edge_toggle)
                        data_shift_reg <= {data_shift_reg[BURST_WORDS*32-5:0], i_qspi_dat[3:0]};
                end

                default: begin
                    // Hold value in other states
                end
            endcase
        end
    end

    //========================================================================
    // Output Control: CS# and SCK Active
    //
    // sck_active is REGISTERED (not combinational) to maintain correct
    // SCK timing — changing to combinational would alter SCK phase relative
    // to CS# and data transitions, breaking the hardware protocol.
    //========================================================================

    always_ff @(posedge i_clk) begin
        if (i_reset) begin
            o_qspi_cs_n <= 1'b1;   // Deasserted (high) on reset
            sck_active  <= 1'b0;
        end else begin
            unique case (state_q)
                ST_STARTUP_RESET: begin
                    o_qspi_cs_n <= 1'b1;
                    sck_active  <= 1'b0;
                end

                ST_SPI_TX_START: begin
                    o_qspi_cs_n <= 1'b0;   // Assert CS#
                    sck_active  <= 1'b0;   // Don't start clock yet (setup cycle)
                end

                ST_SPI_TX_BITS: begin
                    o_qspi_cs_n <= 1'b0;   // Keep CS# asserted
                    sck_active  <= 1'b1;   // Activate SCK
                end

                ST_SPI_TX_END: begin
                    // tx_is_xip → CS# stays LOW (QIOR READ sequence follows)
                    // otherwise → deassert CS# (normal SPI command complete)
                    o_qspi_cs_n <= tx_is_xip ? 1'b0 : 1'b1;
                    sck_active  <= 1'b0;
                end

                ST_READ_ADDR: begin
                    o_qspi_cs_n <= 1'b0;   // Keep CS# asserted
                    // SCK active while nibbles remain; deactivates when count reaches 6
                    sck_active  <= (nibble_counter < 5'd6);
                end

                ST_LOAD_MODE_BYTE: begin
                    o_qspi_cs_n <= 1'b0;   // Keep CS# asserted
                    sck_active  <= 1'b0;   // Pause SCK for one cycle (mode byte load)
                end

                ST_READ_MODE: begin
                    o_qspi_cs_n <= 1'b0;   // Keep CS# asserted
                    sck_active  <= 1'b1;   // SCK active (drives 2 mode byte nibbles)
                end

                ST_READ_DUMMY, ST_READ_DATA: begin
                    o_qspi_cs_n <= 1'b0;   // Keep CS# asserted
                    sck_active  <= 1'b1;   // SCK active
                end

                ST_READ_DONE: begin
                    o_qspi_cs_n <= 1'b1;   // Deassert CS# after read completes
                    sck_active  <= 1'b0;
                end

                ST_READ_RECOVERY: begin
                    o_qspi_cs_n <= 1'b1;   // Keep CS# high during recovery (tSHSL)
                    sck_active  <= 1'b0;
                end

                ST_IDLE: begin
                    o_qspi_cs_n <= 1'b1;
                    sck_active  <= 1'b0;
                end

                default: begin
                    o_qspi_cs_n <= 1'b1;
                    sck_active  <= 1'b0;
                end
            endcase
        end
    end

    //========================================================================
    // Output Enable and Data Control
    //
    // CRITICAL: Driven directly from state_q to avoid pipeline delay.
    // A one-cycle pipeline delay caused the "0xCCCCCCCC bug" (reading own output).
    //
    // ST_READ_ADDR / ST_LOAD_MODE_BYTE : output quad_shift_reg[23:20] (address nibbles)
    // ST_READ_MODE                     : output constant 4'h0 (mode byte always 0x00)
    // ST_SPI_TX_* states               : output tx_shift_reg[7] on IO0 (MOSI only)
    // ST_READ_DUMMY / ST_READ_DATA     : all IOs are inputs (flash drives the bus)
    //========================================================================

    always_ff @(posedge i_clk) begin
        if (i_reset) begin
            o_qspi_oe  <= 4'b0000;
            o_qspi_dat <= 4'b0000;
        end else begin
            unique case (state_q)
                ST_READ_DUMMY, ST_READ_DATA: begin
                    // QUAD RX: All IOs are inputs (flash drives the bus)
                    o_qspi_oe  <= 4'b0000;
                    o_qspi_dat <= 4'b0000;
                end

                ST_READ_ADDR, ST_LOAD_MODE_BYTE: begin
                    // QUAD TX address phase: drive all four IOs
                    o_qspi_oe      <= 4'b1111;
                    o_qspi_dat[3:0] <= quad_shift_reg[23:20];  // MSB nibble first
                end

                ST_READ_MODE: begin
                    // QUAD TX mode byte: always 0x00 — drive constant, no shift needed
                    o_qspi_oe      <= 4'b1111;
                    o_qspi_dat[3:0] <= 4'h0;
                end

                ST_SPI_TX_START, ST_SPI_TX_BITS, ST_SPI_TX_END: begin
                    // SPI TX: IO0=MOSI (output), IO1=MISO (input), IO2/3=Hi-Z
                    o_qspi_oe     <= 4'b0001;  // Only IO0 driven
                    o_qspi_dat[0] <= tx_shift_reg[7];  // MSB to MOSI
                end

                default: begin
                    // Safe default: all inputs
                    o_qspi_oe  <= 4'b0000;
                    o_qspi_dat <= 4'b0000;
                end
            endcase
        end
    end

    //========================================================================
    // Wishbone Interface — ACK + Stall
    //========================================================================

    always_ff @(posedge i_clk) begin
        if (i_reset) begin
            o_wb_ack   <= 1'b0;
            o_wb_stall <= 1'b1;
        end else begin
            o_wb_ack <= 1'b0;  // Default: no ACK
            unique case (state_q)
                ST_IDLE: begin
                    o_wb_stall <= 1'b0;  // Ready for new transactions
                end
                ST_READ_DONE: begin
                    o_wb_ack   <= 1'b1;  // Issue ACK
                    o_wb_stall <= 1'b1;
                end
                default: begin
                    o_wb_stall <= 1'b1;  // Busy
                end
            endcase
        end
    end

    //========================================================================
    // Wishbone Data Output — byte-swap
    // Split into a generate block so the simulator never evaluates out-of-range
    // bit-selects for the inactive branch.
    //
    // BURST_WORDS=1: simple 32-bit byte-swap (little-endian RISC-V ↔ big-endian flash)
    // BURST_WORDS=4: four words, each byte-swapped; qspi_cache word-order convention:
    //   o_wb_data[31:0]=word0 (first addr), [63:32]=word1, [95:64]=word2, [127:96]=word3
    //========================================================================

    generate
        if (BURST_WORDS == 1) begin : gen_wb_data_1
            always_ff @(posedge i_clk) begin
                if (i_reset)
                    o_wb_data <= 32'h0;
                else if (state_q == ST_READ_DONE)
                    o_wb_data <= {data_shift_reg[7:0],   data_shift_reg[15:8],
                                  data_shift_reg[23:16], data_shift_reg[31:24]};
            end
        end else begin : gen_wb_data_4
            // Shift register layout after 32 nibbles:
            //   [127:96] = word0 (first received — lowest address)
            //   [95:64]  = word1
            //   [63:32]  = word2
            //   [31:0]   = word3 (last received — highest address)
            // Output convention: [31:0]=word0 .. [127:96]=word3 (qspi_cache layout)
            always_ff @(posedge i_clk) begin
                if (i_reset) begin
                    o_wb_data <= 128'h0;
                end else if (state_q == ST_READ_DONE) begin
                    o_wb_data[31:0]   <= {data_shift_reg[103:96], data_shift_reg[111:104],
                                          data_shift_reg[119:112], data_shift_reg[127:120]};
                    o_wb_data[63:32]  <= {data_shift_reg[71:64],  data_shift_reg[79:72],
                                          data_shift_reg[87:80],  data_shift_reg[95:88]};
                    o_wb_data[95:64]  <= {data_shift_reg[39:32],  data_shift_reg[47:40],
                                          data_shift_reg[55:48],  data_shift_reg[63:56]};
                    o_wb_data[127:96] <= {data_shift_reg[7:0],    data_shift_reg[15:8],
                                          data_shift_reg[23:16],  data_shift_reg[31:24]};
                end
            end
        end
    endgenerate

    //========================================================================
    // Startup Busy Flag
    // Held high from reset until the first time IDLE is reached (WRR complete).
    //========================================================================

    always_ff @(posedge i_clk) begin
        if (i_reset) begin
            o_startup_busy <= 1'b1;
        end else begin
            unique case (state_q)
                ST_IDLE:  o_startup_busy <= 1'b0;  // Ready
                default:  o_startup_busy <= 1'b1;  // Busy (startup or read in progress)
            endcase
        end
    end

    //========================================================================
    // Status Output Assignments
    // o_state reports 5'd15 for ST_READ_RECOVERY (was 5'd12 — collision now fixed).
    //========================================================================

    assign o_state = state_q;

    //========================================================================
    // Debug Output (disabled for faster simulation)
    //========================================================================
    `ifdef VERILATOR_DEBUG
    always_ff @(posedge i_clk) begin
        if (!i_reset) begin
            // Track Wishbone requests
            if (i_wb_cyc && i_wb_stb && state_q == ST_IDLE && !cyc_acked)
                $display("[XIP] T=%0t: New read request, addr=0x%08x", $time, i_wb_addr);

            // Track state transitions
            if (state_q != state_d) begin
                unique case (state_d)
                    ST_READ_CMD:      $display("[XIP] T=%0t: → ST_READ_CMD", $time);
                    ST_SPI_TX_START:  $display("[XIP] T=%0t: → ST_SPI_TX_START, tx_shift_reg=0x%02x", $time, tx_shift_reg);
                    ST_SPI_TX_BITS:   $display("[XIP] T=%0t: → ST_SPI_TX_BITS, tx_shift_reg=0x%02x", $time, tx_shift_reg);
                    ST_READ_ADDR:     $display("[XIP] T=%0t: → ST_READ_ADDR, addr_reg=0x%06x", $time, addr_reg);
                    ST_READ_MODE:     $display("[XIP] T=%0t: → ST_READ_MODE", $time);
                    ST_READ_DUMMY:    $display("[XIP] T=%0t: → ST_READ_DUMMY, nibble_counter=%0d", $time, nibble_counter);
                    ST_READ_DATA:     $display("[XIP] T=%0t: → ST_READ_DATA, nibble_counter=%0d", $time, nibble_counter);
                    ST_READ_DONE:     $display("[XIP] T=%0t: → ST_READ_DONE, data_shift_reg=0x%08x", $time, data_shift_reg);
                    ST_READ_RECOVERY: $display("[XIP] T=%0t: → ST_READ_RECOVERY", $time);
                    ST_IDLE:          $display("[XIP] T=%0t: → ST_IDLE", $time);
                    default: ;
                endcase
            end

            // Track SPI bit transmission
            if (state_q == ST_SPI_TX_BITS && qspi_clk_enable)
                $display("[XIP] T=%0t: SPI TX bit[%0d]: tx_shift_reg=0x%02x, tx_shift_reg[7]=%b, o_qspi_dat[0]=%b, sck_edge=%b",
                         $time, bit_counter, tx_shift_reg, tx_shift_reg[7], o_qspi_dat[0], sck_edge_toggle);

            // Track data shifting
            if (state_q == ST_READ_DATA && qspi_clk_enable && sck_edge_toggle)
                $display("[XIP] T=%0t: Shift nibble[%0d]=0x%01x, i_qspi_dat=0x%01x, data_shift_reg: 0x%08x → 0x%08x",
                         $time, nibble_counter, i_qspi_dat, i_qspi_dat,
                         data_shift_reg, {data_shift_reg[BURST_WORDS*32-5:0], i_qspi_dat[3:0]});

            // Track ACK
            if (o_wb_ack)
                $display("[XIP] T=%0t: ACK asserted, o_wb_data=0x%08x", $time, o_wb_data);
        end
    end
    `endif

endmodule
