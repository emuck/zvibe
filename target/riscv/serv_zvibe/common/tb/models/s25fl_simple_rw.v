// Copyright (c) 2025 Martin R. Raumann
// SPDX-License-Identifier: BSD-3-Clause
//=============================================================================
// Simplified S25FL128S QSPI Flash Model with Read/Write Support
//
// Purpose: Behavioral model for QSPI XIP and Write operations, compatible with Verilator
//
// Features:
//   READ OPERATIONS:
//   - QIOR (0xEB) Quad I/O Read command with continuous mode support
//     * Recognizes mode byte 0xA0 to enable continuous read mode
//     * Skips command byte on subsequent transactions when continuous mode active
//   - WREN (0x06) and WRR (0x01) for QUAD mode configuration
//
//   WRITE OPERATIONS:
//   - RDSR (0x05) Read Status Register (WIP, WEL bits)
//   - SE (0xD8) Sector Erase (64KB sectors)
//   - PP (0x02) Page Program (up to 256 bytes)
//   - Write enable protection (WREN required before erase/program)
//   - Configurable timing (fast for unit tests, realistic for integration)
//
//   GENERAL:
//   - Configurable memory initialization from hex or binary files
//   - Power-up timing model
//   - Compatible with both Verilator and Vivado xsim
//
// Usage:
//   - Set MEM_FILE to hex/bin filename to preload flash contents
//   - Set MEM_FILE_OFFSET to load at specific address (e.g., 0x400000 for XIP)
//   - Set DEBUG=1 to enable detailed debug messages
//   - Set FAST_SIM=1 for fast write timing (unit tests)
//   - Set FAST_SIM=0 for realistic timing (integration tests)
//
// This is a simplified model focusing on the subset of functionality needed
// for XIP and save/restore testing. It avoids specparam issues and complex
// timing that cause problems with Verilator.
//=============================================================================

`timescale 1ns/1ps
`default_nettype none

module s25fl_simple_rw #(
    parameter POWERUP_TIME = 300,         // Power-up time in ns (tPU)
    parameter MEM_SIZE = 24'hFFFFFF,      // 16MB flash
    parameter MEM_FILE = "none",          // Memory initialization file (.hex or .bin)
    parameter MEM_FILE_OFFSET = 24'h0,    // Offset in flash to load file
    parameter MEM_FILE_IS_HEX = 1,        // 1=hex file (.hex), 0=binary file (.bin)
    parameter DEBUG = 0,                  // 1=enable debug messages, 0=quiet

    // Write operation timing (for simulation)
    parameter ERASE_TIME_NS = 500000,     // Sector erase time (500us for fast sim, 500ms realistic)
    parameter PROGRAM_TIME_NS = 3000,     // Page program time (3us for fast sim, 3ms realistic)
    parameter FAST_SIM = 1                // 1=use fast timing, 0=realistic timing
) (
    // SPI/QSPI Interface
    input  wire       SCK,      // Serial clock
    input  wire       CSNeg,    // Chip select (active low)
    input  wire       RSTNeg,   // Reset (active low)
    inout  wire       SI,       // Serial input / IO0
    inout  wire       SO,       // Serial output / IO1
    inout  wire       WPNeg,    // Write protect / IO2
    inout  wire       HOLDNeg   // Hold / IO3
);

    //=========================================================================
    // Internal Registers and Memory
    //=========================================================================

    logic PoweredUp;
    logic [7:0] Mem [0:MEM_SIZE];  // Flash memory array
    logic [7:0] Config_reg1;       // Configuration register (bit 1 = QUAD enable)
    wire  [7:0] Status_reg1;       // Status register (combinational, built from WIP/WEL)

    // Status register bit definitions (S25FL128S datasheet)
    logic WIP;   // Bit 0: Write In Progress (1=busy, 0=ready)
    logic WEL;   // Bit 1: Write Enable Latch (1=write enabled)

    // Write operation tracking
    logic [23:0] write_addr;       // Address for erase/program operation
    logic [7:0]  program_data;     // Data byte for programming
    integer    erase_delay;      // Remaining erase time
    integer    program_delay;    // Remaining program time

    // Task for testbench to write memory
    task write_mem;
        input [23:0] addr;
        input [7:0] data;
        begin
            Mem[addr] = data;
        end
    endtask

    //=========================================================================
    // Command State Machine
    //=========================================================================

    localparam STATE_IDLE       = 4'd0;
    localparam STATE_CMD        = 4'd1;
    localparam STATE_ADDR       = 4'd2;
    localparam STATE_MODE       = 4'd3;
    localparam STATE_DUMMY      = 4'd4;
    localparam STATE_DATA_OUT   = 4'd5;
    localparam STATE_WRR_SR     = 4'd6;  // Write Register - Status byte
    localparam STATE_WRR_CR     = 4'd7;  // Write Register - Config byte
    localparam STATE_RDSR       = 4'd8;  // Read Status Register - output byte
    localparam STATE_SE_ADDR    = 4'd9;  // Sector Erase - receive address
    localparam STATE_PP_ADDR    = 4'd10; // Page Program - receive address
    localparam STATE_PP_DATA    = 4'd11; // Page Program - receive data

    logic [3:0]  state;
    logic [7:0]  command;
    logic [23:0] address;
    logic [7:0]  mode_byte;
    logic [7:0]  bit_count;
    logic [7:0]  data_out;
    logic        QUAD_mode;
    logic        continuous_mode;  // Set when mode byte 0xA0 received

    // Command codes
    localparam CMD_WREN = 8'h06;  // Write Enable
    localparam CMD_WRDI = 8'h04;  // Write Disable (clears WEL)
    localparam CMD_WRR  = 8'h01;  // Write Registers
    localparam CMD_QIOR = 8'hEB;  // Quad I/O Read
    localparam CMD_RDSR = 8'h05;  // Read Status Register
    localparam CMD_SE   = 8'hD8;  // Sector Erase (64KB)
    localparam CMD_PP   = 8'h02;  // Page Program (256 bytes)

    // IO control
    logic [3:0] io_out;
    logic [3:0] io_oe;
    wire  [3:0] io_in;  // driven by tri-state inouts

    // Bidirectional IO assignment
    assign SI      = io_oe[0] ? io_out[0] : 1'bz;
    assign SO      = io_oe[1] ? io_out[1] : 1'bz;
    assign WPNeg   = io_oe[2] ? io_out[2] : 1'bz;
    assign HOLDNeg = io_oe[3] ? io_out[3] : 1'bz;

    assign io_in[0] = SI;
    assign io_in[1] = SO;
    assign io_in[2] = WPNeg;
    assign io_in[3] = HOLDNeg;

    //=========================================================================
    // Power-Up Initialization
    //=========================================================================

    initial begin
        PoweredUp = 1'b0;
        #POWERUP_TIME PoweredUp = 1'b1;
    end

    initial begin
        Config_reg1 = 8'h00;  // QUAD disabled by default
        WIP = 1'b0;          // Not busy
        WEL = 1'b0;          // Write disabled
        state = STATE_IDLE;
        command = 8'h00;
        address = 24'h000000;
        mode_byte = 8'h00;
        bit_count = 8'h00;
        data_out = 8'h00;
        QUAD_mode = 1'b0;
        continuous_mode = 1'b0;
        io_out = 4'h0;
        io_oe = 4'h0;
        write_addr = 24'h000000;
        program_data = 8'h00;
        erase_delay = 0;
        program_delay = 0;
    end

    //=========================================================================
    // Memory Initialization
    //=========================================================================
    integer mem_init_i;
    integer mem_fd, mem_read_val, mem_bytes_loaded;

    initial begin
        // Initialize all memory to 0xFF (erased flash state)
        for (mem_init_i = 0; mem_init_i <= MEM_SIZE; mem_init_i = mem_init_i + 1) begin
            Mem[mem_init_i] = 8'hFF;
        end

        // Load memory file if specified
        if (MEM_FILE != "none" && MEM_FILE != "") begin
            if (MEM_FILE_IS_HEX) begin
                // Load hex file using $readmemh
                // Note: $readmemh loads data starting at offset, format is standard Verilog hex
                $display("[FLASH] Loading hex file '%s' at offset 0x%06h", MEM_FILE, MEM_FILE_OFFSET);
                $readmemh(MEM_FILE, Mem, MEM_FILE_OFFSET);
                $display("[FLASH] Hex file loaded successfully");
            end else begin
                // Load binary file byte-by-byte
                $display("[FLASH] Loading binary file '%s' at offset 0x%06h", MEM_FILE, MEM_FILE_OFFSET);
                mem_fd = $fopen(MEM_FILE, "rb");
                if (mem_fd == 0) begin
                    $display("[FLASH] ERROR: Cannot open file '%s'", MEM_FILE);
                    $finish;
                end

                mem_init_i = MEM_FILE_OFFSET;
                mem_bytes_loaded = 0;
                while (!$feof(mem_fd)) begin
                    mem_read_val = $fgetc(mem_fd);
                    if (mem_read_val != -1) begin
                        Mem[mem_init_i] = mem_read_val[7:0];
                        mem_init_i = mem_init_i + 1;
                        mem_bytes_loaded = mem_bytes_loaded + 1;
                    end
                end
                $fclose(mem_fd);
                $display("[FLASH] Binary file loaded: %0d bytes", mem_bytes_loaded);
            end
        end else begin
            $display("[FLASH] No memory file specified, flash initialized to 0xFF (erased)");
        end
    end

    //=========================================================================
    // QUAD Mode Control
    //=========================================================================

    always_comb begin
        QUAD_mode = Config_reg1[1];  // Bit 1 of Config_reg1 enables QUAD
    end

    //=========================================================================
    // Status Register Assembly
    //=========================================================================

    assign Status_reg1 = {6'b000000, WEL, WIP};  // Bits [7:2]=0, Bit 1=WEL, Bit 0=WIP

    //=========================================================================
    // Write Operation Handling (Erase and Program)
    // Triggered when CS# goes high after SE or PP command
    //=========================================================================

    // Track which operation to perform when CS# deasserts
    logic        pending_erase;
    logic        pending_program;
    logic [23:0] pending_addr;
    logic [7:0]  pending_data [0:255];  // Buffer for page program data
    integer      pending_byte_count;

    // Flags to track when SE or PP operations complete (set before state transition)
    // These flags also track that WEL was set when the command was received
    logic se_addr_complete;
    logic se_wel_was_set;  // Track that SE was accepted with WEL set
    logic pp_data_active;

    initial begin
        pending_erase = 1'b0;
        pending_program = 1'b0;
        pending_addr = 24'h000000;
        pending_byte_count = 0;
        se_addr_complete = 1'b0;
        se_wel_was_set = 1'b0;
        pp_data_active = 1'b0;
    end

    // Capture write operations when in SE_ADDR or PP_DATA states and CS# goes high
    always @(posedge CSNeg) begin
        if (PoweredUp) begin
            // Set WEL after WREN command completes
            if (state == STATE_IDLE && command == CMD_WREN) begin
                WEL <= 1'b1;
                if (DEBUG) $display("  [FLASH] WEL set to 1 after WREN completion");
            end

            // Clear WEL after WRDI command completes
            if (state == STATE_IDLE && command == CMD_WRDI) begin
                WEL <= 1'b0;
                if (DEBUG) $display("  [FLASH] WEL cleared to 0 after WRDI completion");
            end

            // Check for SE completion - either in SE_ADDR state or just completed
            // According to vendor model: WEL is checked when CS# goes high (posedge CSNeg)
            // If WEL is not set, SE is completely ignored (no WIP, no operation)
            if (((state == STATE_SE_ADDR && bit_count >= 23) || se_addr_complete) && WEL) begin
                // Sector erase command complete - WEL is checked here (when CS# goes high)
                pending_erase <= 1'b1;
                pending_addr <= write_addr;
                WIP <= 1'b1;  // Set Write In Progress
                WEL <= 1'b0;  // Clear Write Enable Latch (will be cleared when operation completes)
                se_addr_complete <= 1'b0;  // Clear flag
                se_wel_was_set <= 1'b0;  // Clear flag
                if (DEBUG) $display("  [FLASH] SE operation queued for sector 0x%06h", write_addr);
            end else if ((state == STATE_SE_ADDR && bit_count >= 23) || se_addr_complete) begin
                // SE address completed but WEL is not set - ignore operation completely
                // (Matches vendor model behavior: if WEL==0 when CS# goes high, SE is ignored)
                se_addr_complete <= 1'b0;  // Clear flag
                se_wel_was_set <= 1'b0;  // Clear flag
                if (DEBUG) $display("  [FLASH] SE operation ignored - WEL not set when CS# goes high");
            end else if (state == STATE_PP_DATA && pp_data_active) begin
                // Page program command complete
                // Only queue if WEL was set when PP command was received (pp_data_active flag)
                pending_program <= 1'b1;
                pending_addr <= write_addr;
                pending_byte_count <= (bit_count + 1) / 8;  // Number of bytes received
                WIP <= 1'b1;  // Set Write In Progress
                WEL <= 1'b0;  // Clear Write Enable Latch
                pp_data_active <= 1'b0;  // Clear flag
                if (DEBUG) $display("  [FLASH] PP operation queued: %0d bytes at 0x%06h",
                                   pending_byte_count, pending_addr);
            end
        end
    end

    // Capture program data bytes as they arrive
    integer pp_byte_index;
    always @(posedge SCK) begin
        if (state == STATE_PP_DATA && bit_count[2:0] == 3'd7) begin
            // Complete byte received
            pp_byte_index = bit_count / 8;
            pending_data[pp_byte_index] = {program_data[6:0], io_in[0]};
        end
    end

    // Perform actual erase/program operations with timing delays
    always @(posedge CSNeg) begin
        if (pending_erase) begin
            pending_erase <= 1'b0;
            fork
                begin
                    integer erase_addr;
                    integer sector_base;
                    // Calculate sector base address (align to 64KB boundary)
                    sector_base = (pending_addr / 65536) * 65536;
                    if (DEBUG) $display("  [FLASH] Erasing sector at 0x%06h (64KB from 0x%06h to 0x%06h)",
                                       pending_addr, sector_base, sector_base + 65535);

                    // Simulate erase delay
                    #(FAST_SIM ? 500 : ERASE_TIME_NS);

                    // Erase sector (set all bytes to 0xFF)
                    for (erase_addr = sector_base; erase_addr < sector_base + 65536; erase_addr = erase_addr + 1) begin
                        Mem[erase_addr] = 8'hFF;
                    end

                    if (DEBUG) $display("  [FLASH] Sector erase complete");
                    WIP <= 1'b0;  // Clear Write In Progress
                end
            join_none
        end

        if (pending_program) begin
            pending_program <= 1'b0;
            fork
                begin
                    integer prog_addr;
                    integer prog_i;
                    if (DEBUG) $display("  [FLASH] Programming %0d bytes at 0x%06h",
                                       pending_byte_count, pending_addr);

                    // Simulate program delay (per byte)
                    #(FAST_SIM ? (30 * pending_byte_count) : (PROGRAM_TIME_NS * pending_byte_count));

                    // Program data (AND with existing data - can only change 1->0)
                    for (prog_i = 0; prog_i < pending_byte_count; prog_i = prog_i + 1) begin
                        prog_addr = pending_addr + prog_i;
                        Mem[prog_addr] = Mem[prog_addr] & pending_data[prog_i];
                        if (DEBUG) $display("  [FLASH] Programmed 0x%02h at 0x%06h",
                                           Mem[prog_addr], prog_addr);
                    end

                    if (DEBUG) $display("  [FLASH] Page program complete");
                    WIP <= 1'b0;  // Clear Write In Progress
                end
            join_none
        end
    end

    //=========================================================================
    // Command Processing State Machine
    // Sample inputs on falling edge of SCK (standard SPI Mode 0)
    //=========================================================================

    // Reset state on CS# edges
    always @(negedge CSNeg) begin
        if (PoweredUp) begin
            // Clear operation flags when starting new command
            se_addr_complete <= 1'b0;
            se_wel_was_set <= 1'b0;
            pp_data_active <= 1'b0;
            
            // Check if we're in continuous read mode (skip command byte)
            if (continuous_mode && QUAD_mode) begin
                state <= STATE_ADDR;
                bit_count <= 8'h00;
                address <= 24'h000000;  // MUST reset - address is a shift register!
                if (DEBUG) $display("  [FLASH] CS# asserted, continuous mode active - skip to ADDR");
            end else begin
                state <= STATE_CMD;
                bit_count <= 8'h00;
                command <= 8'h00;
                address <= 24'h000000;  // Reset address for new non-continuous read
                if (DEBUG) $display("  [FLASH] CS# asserted, ready for command byte");
            end
        end
    end

    always @(posedge SCK or posedge CSNeg) begin
        if (CSNeg) begin
            // CS# deasserted - reset to idle
            state <= STATE_IDLE;
        end else if (PoweredUp) begin
            // Sample on rising SCK edge (data is stable after DUT setup phase)
            case (state)
                //-------------------------------------------------------------
                // IDLE: Shouldn't get here with CS# low
                //-------------------------------------------------------------
                STATE_IDLE: begin
                    // Stay in idle
                end

                //-------------------------------------------------------------
                // CMD: Receive 8-bit command (SPI mode, IO0 only)
                //-------------------------------------------------------------
                STATE_CMD: begin
                    command <= {command[6:0], io_in[0]};  // Shift in from IO0 (SI)
                    bit_count <= bit_count + 1;
                    if (DEBUG) $display("  [FLASH] CMD bit %0d: %b (command so far: 0x%02h)",
                                       bit_count, io_in[0], {command[6:0], io_in[0]});

                    if (bit_count == 7) begin
                        // Command byte complete
                        case ({command[6:0], io_in[0]})
                            CMD_WREN: begin
                                if (DEBUG) $display("  [FLASH] Received WREN command");
                                // WEL will be set when CS# goes high (see posedge CSNeg block)
                                state <= STATE_IDLE;  // WREN is single byte, return to idle
                            end

                            CMD_WRDI: begin
                                if (DEBUG) $display("  [FLASH] Received WRDI command");
                                // WEL will be cleared when CS# goes high (see posedge CSNeg block)
                                state <= STATE_IDLE;  // WRDI is single byte, return to idle
                            end

                            CMD_RDSR: begin
                                if (DEBUG) $display("  [FLASH] Received RDSR command");
                                state <= STATE_RDSR;
                                bit_count <= 8'hFF;  // Will increment to 0 on first negedge, output Status[7]
                            end

                            CMD_WRR: begin
                                if (DEBUG) $display("  [FLASH] Received WRR command");
                                state <= STATE_WRR_SR;
                                bit_count <= 8'h00;
                            end

                            CMD_SE: begin
                                if (DEBUG) $display("  [FLASH] Received SE (Sector Erase) command");
                                // Always enter SE_ADDR state to receive address
                                // WEL will be checked when CS# goes high (posedge CSNeg)
                                // This matches vendor model behavior
                                state <= STATE_SE_ADDR;
                                bit_count <= 8'h00;
                                write_addr <= 24'h000000;
                                se_wel_was_set <= 1'b1;  // Flag that we're in SE sequence
                            end

                            CMD_PP: begin
                                if (DEBUG) $display("  [FLASH] Received PP (Page Program) command");
                                if (WEL) begin
                                    state <= STATE_PP_ADDR;
                                    bit_count <= 8'h00;
                                    write_addr <= 24'h000000;
                                    pp_data_active <= 1'b1;  // Flag that PP is active (WEL was set)
                                end else begin
                                    if (DEBUG) $display("  [FLASH] PP rejected - WEL not set");
                                    state <= STATE_IDLE;
                                    pp_data_active <= 1'b0;  // Clear flag if rejected
                                end
                            end

                            CMD_QIOR: begin
                                if (QUAD_mode) begin
                                    if (DEBUG) $display("  [FLASH] Received QIOR command (QUAD mode enabled)");
                                    state <= STATE_ADDR;
                                    bit_count <= 8'h00;
                                    address <= 24'h000000;
                                end else begin
                                    if (DEBUG) $display("  [FLASH] WARNING: QIOR command rejected (QUAD mode disabled)");
                                    state <= STATE_IDLE;  // QIOR requires QUAD mode
                                end
                            end

                            default: begin
                                if (DEBUG) $display("  [FLASH] Unknown command: 0x%02h", {command[6:0], io_in[0]});
                                state <= STATE_IDLE;  // Unknown command
                            end
                        endcase
                    end
                end

                //-------------------------------------------------------------
                // WRR: Write Registers (2 bytes: Status, Config)
                //-------------------------------------------------------------
                STATE_WRR_SR: begin
                    // Receive status register byte (but don't store it - status is read-only)
                    // Status bits are controlled by other commands (WREN sets WEL, etc.)
                    bit_count <= bit_count + 1;
                    if (bit_count == 7) begin
                        state <= STATE_WRR_CR;
                        bit_count <= 8'h00;
                    end
                end

                STATE_WRR_CR: begin
                    Config_reg1 <= {Config_reg1[6:0], io_in[0]};
                    bit_count <= bit_count + 1;
                    if (bit_count == 7) begin
                        if (DEBUG) $display("  [FLASH] WRR complete: Config_reg1=0x%02h (QUAD=%b)",
                                           {Config_reg1[6:0], io_in[0]}, {Config_reg1[6:1], io_in[0]} != 0);
                        state <= STATE_IDLE;
                    end
                end

                //-------------------------------------------------------------
                // QIOR: Quad I/O Read Sequence
                //-------------------------------------------------------------

                // ADDRESS: 24 bits in QUAD mode (6 nibbles, 2 bits per IO)
                STATE_ADDR: begin
                    address <= {address[19:0], io_in[3:0]};  // Shift in 4 bits (nibble)
                    bit_count <= bit_count + 4;
                    if (bit_count >= 20) begin  // 24 bits = 6 nibbles
                        state <= STATE_MODE;
                        bit_count <= 8'h00;
                        mode_byte <= 8'h00;
                    end
                end

                // MODE: 8 bits in QUAD mode (2 nibbles)
                STATE_MODE: begin
                    mode_byte <= {mode_byte[3:0], io_in[3:0]};
                    bit_count <= bit_count + 4;
                    if (bit_count >= 4) begin  // 8 bits = 2 nibbles
                        if (DEBUG) $display("  [FLASH] Mode byte: 0x%02h", {mode_byte[3:0], io_in[3:0]});
                        // Check for continuous read mode (mode_byte[7:4] == 0xA)
                        if (mode_byte[3:0] == 4'hA) begin  // Upper nibble is mode_byte[3:0] after shift
                            continuous_mode <= 1'b1;
                            if (DEBUG) $display("  [FLASH] Continuous read mode ENABLED");
                        end else begin
                            continuous_mode <= 1'b0;
                            if (DEBUG) $display("  [FLASH] Continuous read mode DISABLED (mode=0x%02h)",
                                              {mode_byte[3:0], io_in[3:0]});
                        end
                        state <= STATE_DUMMY;
                        bit_count <= 8'h00;
                    end
                end

                // DUMMY: 6 cycles (S25FL128S default for LC=00)
                // CRITICAL TIMING: Transition to DATA_OUT after 5 cycles (not 6) for simulation sync
                //
                // S25FL128S requires 6 dummy cycles before outputting data.
                // Controller samples on posedge SCK #6, so flash must output data by then.
                //
                // Hardware timing: Flash transitions on posedge #6, data ready immediately
                // Simulation timing: Zero-delay means flash must transition BEFORE posedge #6
                //
                // Solution: Transition after 5 cycles (bit_count 0-4) so data is ready on posedge #5,
                // giving simulation time to update output before controller samples on posedge #6.
                STATE_DUMMY: begin
                    bit_count <= bit_count + 1;
                    if (bit_count >= 4) begin  // Transition after 5 cycles (0-4), data ready on posedge #5
                        if (DEBUG) begin
                            $display("  [FLASH] DUMMY complete, transitioning to DATA_OUT");
                            $display("  [FLASH] Loading data from address 0x%06h: 0x%02h",
                                     address, Mem[address]);
                        end
                        state <= STATE_DATA_OUT;
                        // Set to 254 so first sample gets HIGH nibble ([1:0]=10), then LOW ([1:0]=11)
                        // Then address increments at 255, and bit_count wraps to 0 for next byte
                        // FIX: Was 253 which output nibbles in wrong order (LOW then HIGH)
                        bit_count <= 8'hFE;  // 254
                        // Load first data byte from memory
                        data_out <= Mem[address];
                    end
                end

                //-------------------------------------------------------------
                // SE: Sector Erase - receive 24-bit address
                //-------------------------------------------------------------
                STATE_SE_ADDR: begin
                    write_addr <= {write_addr[22:0], io_in[0]};  // Shift in address MSB first
                    bit_count <= bit_count + 1;
                    if (bit_count == 23) begin
                        // Address complete, start erase on CS# deassert
                        if (DEBUG) $display("  [FLASH] SE address complete: 0x%06h", {write_addr[22:0], io_in[0]});
                        se_addr_complete <= 1'b1;  // Flag that SE address is complete
                        state <= STATE_IDLE;
                    end
                end

                //-------------------------------------------------------------
                // PP: Page Program - receive 24-bit address
                //-------------------------------------------------------------
                STATE_PP_ADDR: begin
                    write_addr <= {write_addr[22:0], io_in[0]};  // Shift in address MSB first
                    bit_count <= bit_count + 1;
                    if (bit_count == 23) begin
                        if (DEBUG) $display("  [FLASH] PP address complete: 0x%06h", {write_addr[22:0], io_in[0]});
                        state <= STATE_PP_DATA;
                        bit_count <= 8'h00;
                        program_data <= 8'h00;
                    end
                end

                //-------------------------------------------------------------
                // PP: Page Program - receive data bytes
                //-------------------------------------------------------------
                STATE_PP_DATA: begin
                    program_data <= {program_data[6:0], io_in[0]};  // Shift in data byte MSB first
                    bit_count <= bit_count + 1;
                    if (bit_count[2:0] == 3'd7) begin
                        // Byte complete - program it immediately
                        // (In real flash, programming happens when CS# deasserts, but for simplicity we program on the fly)
                        if (DEBUG) $display("  [FLASH] PP data byte: 0x%02h at address 0x%06h",
                                           {program_data[6:0], io_in[0]}, write_addr);
                        // Stay in PP_DATA state, can receive more bytes until CS# goes high
                    end
                end
            endcase
        end
    end

    //=========================================================================
    // Data Output Control (combinational)
    //=========================================================================

    always_comb begin
        if (PoweredUp && state == STATE_DATA_OUT && ~CSNeg) begin
            // QIOR data output - QUAD mode
            io_oe = 4'hF;  // All IOs as outputs in QUAD mode
            case (bit_count[1:0])  // Which nibble within the byte?
                2'b00: io_out = data_out[7:4];  // High nibble first
                2'b01: io_out = data_out[3:0];  // Low nibble
                2'b10: io_out = data_out[7:4];  // Next byte high nibble
                2'b11: io_out = data_out[3:0];  // Next byte low nibble
            endcase
        end else if (PoweredUp && state == STATE_RDSR && ~CSNeg) begin
            // RDSR output - SPI mode (only MISO/SO/IO1)
            io_oe = 4'b0010;  // Only IO1 (SO/MISO) as output
            // Use current bit_count but ensure stable output
            io_out[1] = Status_reg1[7 - bit_count[2:0]];
            io_out[0] = 1'b0;
            io_out[2] = 1'b0;
            io_out[3] = 1'b0;
        end else begin
            io_oe = 4'h0;  // IOs as inputs when not outputting
            io_out = 4'h0;
        end
    end

    //=========================================================================
    // Bit Counter and Data Register Update (on negedge SCK)
    // Update on falling edge so data is stable during high SCK for sampling
    //=========================================================================

    always @(negedge SCK or posedge CSNeg) begin
        if (CSNeg) begin
            // Reset on CS# deassert
        end else if (PoweredUp && state == STATE_DATA_OUT) begin
            if (DEBUG) $display("  [FLASH] DATA OUT nibble %0d: 0x%01h", bit_count, io_out);
            bit_count <= bit_count + 1;

            // Load next byte every 2 nibbles (8 bits)
            // Allow bit_count < 128 (normal operation) or bit_count == 255 (first byte transition)
            // Exclude 253, 254 which are only used for initial state setup
            if (bit_count[0] && (bit_count[7] == 0 || bit_count == 255)) begin
                address <= address + 1;
                data_out <= Mem[address + 1];
            end
        end else if (PoweredUp && state == STATE_RDSR) begin
            // RDSR output - increment bit counter and wrap after 8 bits
            if (DEBUG) $display("  [FLASH] RDSR bit %0d: %b (status=0x%02h)", bit_count[2:0], io_out[1], Status_reg1);
            bit_count <= bit_count + 1;
            // After 8 bits, wrap around and keep outputting status
        end
    end

    //=========================================================================
    // Debug: Report when not powered up
    //=========================================================================

    always @(negedge CSNeg) begin
        if (~PoweredUp)
            $display("WARNING: Flash accessed before power-up complete (t=%0t)", $time);
    end

endmodule

`default_nettype wire
