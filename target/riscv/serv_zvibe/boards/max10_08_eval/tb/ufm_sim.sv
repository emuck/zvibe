///////////////////////////////////////////////////////////////////////////////
// ufm_sim.v
//
// Behavioral Simulation Model for Intel MAX10 UFM IP Core
//
// This module mimics the interface and behavior of the Intel 'ufm' IP core
// for both read (XIP) and write (erase/program) operations.
//
// Interface matches: ip/ufm/synthesis/ufm.v (Intel Platform Designer)
//
// Features:
// - Data interface: Read with 1-cycle latency, Write with timing simulation
// - CSR interface: STATUS (addr 0) and CONTROL (addr 1) registers
// - Erase operation: Clear 2KB page to 0xFFFFFFFF
// - Program operation: Write single 32-bit word
// - Write protection: Checks wp bits before erase/program
// - Timing simulation: Configurable delays for erase/program
//
// Copyright (c) 2025 Martin R. Raumann
// SPDX-License-Identifier: BSD-3-Clause
//
///////////////////////////////////////////////////////////////////////////////

`timescale 1ps / 1ps
`default_nettype none

/* verilator lint_off DECLFILENAME */
module ufm (
    input  wire        clock,                   // clk
    input  wire [16:0] avmm_data_addr,          // address (word address, 17-bit for 256KB)
    input  wire        avmm_data_read,          // read request
    input  wire        avmm_data_write,         // write request (for data programming)
    input  wire [31:0] avmm_data_writedata,     // write data
    output logic [31:0] avmm_data_readdata,      // read data
    output logic        avmm_data_waitrequest,   // wait request (busy)
    output logic        avmm_data_readdatavalid, // read data valid
    input  wire [3:0]  avmm_data_burstcount,    // burst count (unused)

    // CSR interface
    input  wire        avmm_csr_addr,           // CSR address (0=STATUS, 1=CONTROL)
    input  wire        avmm_csr_read,           // CSR read
    input  wire        avmm_csr_write,          // CSR write
    input  wire [31:0] avmm_csr_writedata,      // CSR write data
    output logic [31:0] avmm_csr_readdata,       // CSR read data

    input  wire        reset_n                  // active-low reset
);

    //========================================================================
    // Parameters
    //========================================================================
    // UFM2 region: 172KB = 44032 words (32-bit)
    // Use 64K words (256KB) for simplicity in simulation
    localparam MEM_DEPTH = 65536;  // 64K words = 256KB

    // Page size: 2KB = 512 words
    localparam PAGE_SIZE = 512;

    // Timing parameters (in clock cycles @ 100MHz)
    // Real hardware: Erase ~100ms, Program ~10ms
    // Simulation: Much faster for practicality
    localparam ERASE_DELAY = 100;   // ~1us in sim (vs 100ms real)
    localparam PROGRAM_DELAY = 10;  // ~100ns in sim (vs 10ms real)

    //========================================================================
    // STATUS Register Bits (CSR address 0)
    //========================================================================
    // Bit 2: busy - Operation in progress
    // Bit 1: read_success (rs) - Last read succeeded
    // Bit 3: write_success (ws) - Last program succeeded
    // Bit 4: erase_success (es) - Last erase succeeded

    localparam STATUS_BUSY_BIT = 2;
    localparam STATUS_RS_BIT = 1;
    localparam STATUS_WS_BIT = 3;
    localparam STATUS_ES_BIT = 4;

    //========================================================================
    // CONTROL Register Fields (CSR address 1)
    //========================================================================
    // Bits [19:0]: pe - Page erase address (0xFFFFF = inactive)
    // Bits [22:20]: se - Sector erase ID (0x7 = inactive)
    // Bits [27:23]: wp - Write protection bits (5 bits)

    localparam PE_INACTIVE = 20'hFFFFF;
    localparam SE_INACTIVE = 3'h7;

    //========================================================================
    // Memory Array
    //========================================================================
    logic [31:0] memory [0:MEM_DEPTH-1];

    //========================================================================
    // CSR Registers
    //========================================================================
    logic [31:0] status_reg;
    logic [31:0] control_reg;

    // Decoded control fields
    wire [19:0] pe_addr;
    wire [2:0]  se_id;
    wire [4:0]  wp_bits;

    assign pe_addr = control_reg[19:0];
    assign se_id = control_reg[22:20];
    assign wp_bits = control_reg[27:23];

    //========================================================================
    // Operation State Machine
    //========================================================================
    logic [2:0] state;
    localparam STATE_IDLE = 3'd0;
    localparam STATE_ERASE = 3'd1;
    localparam STATE_PROGRAM = 3'd2;

    logic [15:0] delay_counter;
    logic [16:0] erase_addr;      // Current address being erased
    logic [16:0] program_addr;    // Address to program
    logic [31:0] program_data;    // Data to program

    //========================================================================
    // Data Interface - Read Logic
    //========================================================================
    logic [31:0] read_data_reg;
    logic read_valid_reg;

    always_ff @(posedge clock or negedge reset_n) begin
        if (!reset_n) begin
            read_data_reg <= 32'h0;
            read_valid_reg <= 1'b0;
        end else begin
            // Default: no valid data
            read_valid_reg <= 1'b0;

            // Handle read request (1-cycle latency)
            if (avmm_data_read && !avmm_data_waitrequest) begin
                if (avmm_data_addr < MEM_DEPTH) begin
                    read_data_reg <= memory[avmm_data_addr];
                end else begin
                    read_data_reg <= 32'hFFFFFFFF;  // Out of bounds
                end
                read_valid_reg <= 1'b1;
                // Set read success bit
                status_reg[STATUS_RS_BIT] <= 1'b1;
            end
        end
    end

    // Data output assignments
    always_comb begin
        avmm_data_readdata = read_data_reg;
        avmm_data_readdatavalid = read_valid_reg;
        // Note: avmm_data_waitrequest is managed in sequential blocks
    end

    //========================================================================
    // Data Interface - Write/Program Logic
    //========================================================================
    always_ff @(posedge clock or negedge reset_n) begin
        if (!reset_n) begin
            state <= STATE_IDLE;
            delay_counter <= 16'd0;
            erase_addr <= 17'd0;
            program_addr <= 17'd0;
            program_data <= 32'd0;
        end else begin
            case (state)
                STATE_IDLE: begin
                    // Handle data write (program trigger)
                    if (avmm_data_write && !avmm_data_waitrequest) begin
                        // Check write protection
                        if (!is_write_protected(avmm_data_addr)) begin
                            program_addr <= avmm_data_addr;
                            program_data <= avmm_data_writedata;
                            state <= STATE_PROGRAM;
                            delay_counter <= PROGRAM_DELAY;
                            status_reg[STATUS_BUSY_BIT] <= 1'b1;
                            status_reg[STATUS_WS_BIT] <= 1'b0;  // Clear until done
                            avmm_data_waitrequest <= 1'b1;  // Block during program
                            $display("[UFM_SIM] Starting program: addr=0x%05X data=0x%08X", avmm_data_addr, avmm_data_writedata);
                        end else begin
                            // Write protected - fail immediately
                            status_reg[STATUS_WS_BIT] <= 1'b0;
                            $display("[UFM_SIM] Write protected: addr=0x%05X sector=%0d wp_bits=0x%02X control=0x%08X",
                                     avmm_data_addr, avmm_data_addr[16:14], wp_bits, control_reg);
                        end
                    end
                end

                STATE_PROGRAM: begin
                    if (delay_counter > 0) begin
                        delay_counter <= delay_counter - 1;
                    end else begin
                        // Program the word
                        if (program_addr < MEM_DEPTH) begin
                            memory[program_addr] <= program_data;
                            status_reg[STATUS_WS_BIT] <= 1'b1;  // Success
                            $display("[UFM_SIM] Programmed word[0x%05X] = 0x%08X", program_addr, program_data);
                        end else begin
                            $display("[UFM_SIM] ERROR: Program address 0x%05X out of range!", program_addr);
                        end
                        status_reg[STATUS_BUSY_BIT] <= 1'b0;
                        avmm_data_waitrequest <= 1'b0;  // Clear busy
                        state <= STATE_IDLE;
                    end
                end

                STATE_ERASE: begin
                    if (delay_counter > 0) begin
                        delay_counter <= delay_counter - 1;
                        if (delay_counter == ERASE_DELAY) begin
                            $display("[UFM_SIM] Starting erase countdown: delay=%0d", ERASE_DELAY);
                        end
                    end else begin
                        // Erase the page (512 words starting at page boundary)
                        erase_page(erase_addr);
                        status_reg[STATUS_ES_BIT] <= 1'b1;  // Success
                        status_reg[STATUS_BUSY_BIT] <= 1'b0;
                        avmm_data_waitrequest <= 1'b0;  // Clear busy
                        state <= STATE_IDLE;
                        $display("[UFM_SIM] Erase complete");
                    end
                end

                default: begin
                    state <= STATE_IDLE;
                end
            endcase
        end
    end

    //========================================================================
    // CSR Interface
    //========================================================================
    always_ff @(posedge clock or negedge reset_n) begin
        if (!reset_n) begin
            status_reg <= 32'h0000001A;  // rs=1, ws=1, es=1, busy=0 (bits 1,3,4 = 0x1A)
            control_reg <= {5'h1F, SE_INACTIVE, PE_INACTIVE};  // All inactive, wp all set
            avmm_csr_readdata <= 32'h0;
        end else begin
            // CSR read
            if (avmm_csr_read) begin
                avmm_csr_readdata <= avmm_csr_addr ? control_reg : status_reg;
            end

            // CSR write (CONTROL register only)
            if (avmm_csr_write && avmm_csr_addr) begin
                control_reg <= avmm_csr_writedata;
                $display("[UFM_SIM] Write CONTROL: 0x%08X wp_bits_next=0x%02X", avmm_csr_writedata, avmm_csr_writedata[27:23]);

                // Check for erase trigger (pe_addr != 0xFFFFF)
                if (avmm_csr_writedata[19:0] != PE_INACTIVE && state == STATE_IDLE) begin
                    // Start page erase
                    if (!is_page_protected(avmm_csr_writedata[19:0])) begin
                        erase_addr <= {avmm_csr_writedata[16:0]};
                        state <= STATE_ERASE;
                        delay_counter <= ERASE_DELAY;
                        status_reg[STATUS_BUSY_BIT] <= 1'b1;
                        status_reg[STATUS_ES_BIT] <= 1'b0;  // Clear until done
                        avmm_data_waitrequest <= 1'b1;  // Block during erase
                        $display("[UFM_SIM] Erase triggered: page=0x%05X", avmm_csr_writedata[19:0]);
                    end else begin
                        // Page protected - fail immediately
                        status_reg[STATUS_ES_BIT] <= 1'b0;
                        $display("[UFM_SIM] Erase blocked - page protected");
                    end
                end
            end
        end
    end

    //========================================================================
    // Write Protection Check Functions
    //========================================================================
    function is_write_protected;
        input [16:0] addr;
        logic [2:0] sector;
        begin
            // Determine sector (8 sectors, ~21KB each for 172KB total)
            // Sector = addr[16:14] for 64K address space
            sector = addr[16:14];
            // Check if wp bit for this sector is set (0 = protected, 1 = writable)
            is_write_protected = (sector < 5) ? !wp_bits[sector] : 1'b0;
        end
    endfunction

    function is_page_protected;
        input [19:0] page_addr;
        logic [16:0] word_addr;
        begin
            // Convert page address to word address (page = 512 words)
            word_addr = page_addr[16:0];
            is_page_protected = is_write_protected(word_addr);
        end
    endfunction

    //========================================================================
    // Erase Page Task
    //========================================================================
    integer erase_idx;
    task erase_page;
        input [16:0] start_addr;
        logic [16:0] page_base;
        begin
            // Calculate page-aligned base address (clear lower 9 bits for 512-word page)
            page_base = {start_addr[16:9], 9'd0};

            // Erase 512 words starting at page boundary
            for (erase_idx = 0; erase_idx < PAGE_SIZE; erase_idx = erase_idx + 1) begin
                if ((page_base + erase_idx) < MEM_DEPTH) begin
                    memory[page_base + erase_idx] = 32'hFFFFFFFF;
                end
            end

            $display("[UFM_SIM] Erased page at word address 0x%05x (512 words)", page_base);
        end
    endtask

    //========================================================================
    // Initialization (for testbench access)
    //========================================================================
    integer i;
    logic [255:0] firmware_file;
    initial begin
        // Initialize all memory to erased state (0xFFFFFFFF)
        for (i = 0; i < MEM_DEPTH; i = i + 1) begin
            memory[i] = 32'hFFFFFFFF;
        end
        $display("[UFM_SIM] Initialized %0d words (256KB) to erased state", MEM_DEPTH);

        // Load firmware if hex file specified
        if ($value$plusargs("ufm_hex=%s", firmware_file)) begin
            $readmemh(firmware_file, memory);
            $display("[UFM_SIM] Loaded UFM memory from: %s", firmware_file);
        end

        // Or load from .dat file (word address + data format)
        if ($value$plusargs("ufm_dat=%s", firmware_file)) begin
            $readmemh(firmware_file, memory);
            $display("[UFM_SIM] Loaded UFM memory from .dat: %s", firmware_file);
        end
    end

endmodule

`default_nettype wire
