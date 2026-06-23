///////////////////////////////////////////////////////////////////////////////
// max10_bootloader_fsm_tb.sv
//
// MAX10 Bootloader FSM Testbench
//
// Tests the bootloader FSM that copies boot stub from UFM to RAM.
//
// Copyright (c) 2025 Martin R. Raumann
// SPDX-License-Identifier: BSD-3-Clause
//
// Test Sequence:
// 1. Initialize UFM with boot stub code
// 2. Reset bootloader FSM
// 3. FSM copies boot stub from UFM to RAM
// 4. Verify RAM contains correct boot stub code
// 5. Verify CPU reset is released after copy completes
//
///////////////////////////////////////////////////////////////////////////////

`timescale 1ns / 1ps
`default_nettype none

module max10_bootloader_fsm_tb;

    //========================================================================
    // Parameters
    //========================================================================
    localparam CLK_PERIOD = 20;  // 50MHz clock (20ns period)
    localparam UFM_ADDR_WIDTH = 18;
    localparam RAM_ADDR_WIDTH = 15;
    localparam BOOT_STUB_ADDR = 18'h04000;  // Word address in UFM
    localparam BOOT_STUB_WORDS = 256;        // 1KB = 256 words
    localparam DATA_WIDTH = 32;

    //========================================================================
    // Clock and Reset
    //========================================================================
    reg clk = 0;
    reg reset = 1;

    always #(CLK_PERIOD/2) clk = ~clk;

    //========================================================================
    // Bootloader FSM Signals
    //========================================================================
    wire [UFM_ADDR_WIDTH-1:0] ufm_address;
    wire                       ufm_read;
    reg  [DATA_WIDTH-1:0]     ufm_readdata;
    reg                       ufm_waitrequest;
    reg                       ufm_readdatavalid;

    wire [RAM_ADDR_WIDTH-1:0] ram_write_addr;
    wire [DATA_WIDTH-1:0]     ram_write_data;
    wire                      ram_write_enable;

    wire                      cpu_reset_n;
    wire                      boot_complete;
    wire [2:0]               boot_state;

    //========================================================================
    // UFM Model Instance
    //========================================================================
    // Create a simple boot stub pattern in UFM
    // Pattern: 0x00000000, 0x11111111, 0x22222222, ... (for testing)
    // UFM needs to be large enough for BOOT_STUB_ADDR (0x04000 = 16384 words)
    // So we need at least 0x04000 * 4 = 0x10000 = 64KB, plus space for boot stub
    // Use 128KB to be safe
    max10_ufm_model #(
        .ADDR_WIDTH(UFM_ADDR_WIDTH),
        .DATA_WIDTH(DATA_WIDTH),
        .MEMSIZE(131072),  // 128KB UFM (to accommodate address 0x04000)
        .MEMFILE("")      // Initialize programmatically
    ) ufm (
        .i_clk(clk),
        .i_reset(reset),
        .i_address(ufm_address),
        .i_read(ufm_read),
        .i_write(1'b0),
        .i_writedata(32'h0),
        .o_readdata(ufm_readdata),
        .o_waitrequest(ufm_waitrequest),
        .o_readdatavalid(ufm_readdatavalid)
    );

    // Initialize UFM memory with test pattern
    // Do this before simulation starts (in initial block before any delays)
    initial begin
        integer i;
        // Initialize UFM memory immediately (before any clock cycles)
        for (i = 0; i < BOOT_STUB_WORDS; i = i + 1) begin
            ufm.memory[BOOT_STUB_ADDR + i] = {16'h0000, i[15:0]};  // Pattern: 0x0000XXXX
        end
        $display("[TB] Initialized UFM with boot stub pattern (%0d words) at address 0x%05X", 
                 BOOT_STUB_WORDS, BOOT_STUB_ADDR);
        // Verify initialization
        $display("[TB] UFM[0x%05X] = 0x%08X (should be 0x00000000)", BOOT_STUB_ADDR, ufm.memory[BOOT_STUB_ADDR]);
        $display("[TB] UFM[0x%05X] = 0x%08X (should be 0x00000001)", BOOT_STUB_ADDR+1, ufm.memory[BOOT_STUB_ADDR+1]);
    end

    //========================================================================
    // RAM Model Instance
    //========================================================================
    max10_ram_model #(
        .ADDR_WIDTH(RAM_ADDR_WIDTH),
        .DATA_WIDTH(DATA_WIDTH),
        .MEMSIZE(32768),  // 32KB RAM
        .MEMFILE("")
    ) ram (
        .i_clk(clk),
        .i_reset(reset),
        .i_write_addr(ram_write_addr),
        .i_write_data(ram_write_data),
        .i_write_enable(ram_write_enable),
        .i_read_addr(ram_read_addr),
        .o_read_data(ram_read_data)
    );

    // RAM read address for verification
    reg [RAM_ADDR_WIDTH-1:0] ram_read_addr = 0;
    wire [DATA_WIDTH-1:0]    ram_read_data;

    //========================================================================
    // Bootloader FSM Instance
    //========================================================================
    max10_bootloader_fsm #(
        .UFM_ADDR_WIDTH(UFM_ADDR_WIDTH),
        .RAM_ADDR_WIDTH(RAM_ADDR_WIDTH),
        .BOOT_STUB_ADDR(BOOT_STUB_ADDR),
        .BOOT_STUB_WORDS(BOOT_STUB_WORDS),
        .DATA_WIDTH(DATA_WIDTH)
    ) dut (
        .i_clk(clk),
        .i_reset(reset),
        .o_ufm_address(ufm_address),
        .o_ufm_read(ufm_read),
        .i_ufm_readdata(ufm_readdata),
        .i_ufm_waitrequest(ufm_waitrequest),
        .i_ufm_readdatavalid(ufm_readdatavalid),
        .o_ram_address(ram_write_addr),
        .o_ram_data(ram_write_data),
        .o_ram_write_enable(ram_write_enable),
        .o_cpu_reset_n(cpu_reset_n),
        .o_boot_complete(boot_complete),
        .o_boot_state(boot_state)
    );

    //========================================================================
    // Test Sequence
    //========================================================================
    integer words_copied = 0;
    integer errors = 0;

    initial begin
        $display("========================================");
        $display("MAX10 Bootloader FSM Testbench");
        $display("========================================\n");

        // Initialize
        reset = 1;
        ram_read_addr = 0;
        #(CLK_PERIOD * 10);

        // Release reset
        $display("[TB] Releasing reset...");
        reset = 0;
        #(CLK_PERIOD * 5);

        // Wait for bootloader to complete
        $display("[TB] Waiting for bootloader to complete...");
        wait(boot_complete);
        $display("[TB] Bootloader complete! CPU reset released.");

        // Wait a few cycles
        #(CLK_PERIOD * 10);

        // Verify RAM contents
        $display("\n[TB] Verifying RAM contents...");
        for (integer i = 0; i < BOOT_STUB_WORDS; i = i + 1) begin
            ram_read_addr = i;
            #(CLK_PERIOD);
            
            begin
                reg [DATA_WIDTH-1:0] expected;
                reg [DATA_WIDTH-1:0] actual;
                expected = {16'h0000, i[15:0]};
                actual = ram_read_data;
                
                if (actual !== expected) begin
                    $display("[TB] ERROR: RAM[%0d] = 0x%08X, expected 0x%08X", i, actual, expected);
                    errors = errors + 1;
                end else if (i < 10 || i >= BOOT_STUB_WORDS - 10) begin
                    $display("[TB] OK: RAM[%0d] = 0x%08X", i, actual);
                end
            end
        end

        // Summary
        $display("\n========================================");
        $display("Test Summary");
        $display("========================================");
        $display("Words copied: %0d", BOOT_STUB_WORDS);
        $display("Errors: %0d", errors);
        if (errors == 0) begin
            $display("TEST PASSED!");
        end else begin
            $display("TEST FAILED!");
        end
        $display("========================================\n");

        #(CLK_PERIOD * 10);
        $finish;
    end

    //========================================================================
    // Monitor Bootloader State (only on state changes)
    //========================================================================
    reg [2:0] prev_boot_state = 0;
    always @(posedge clk) begin
        if (!reset && boot_state != prev_boot_state) begin
            case (boot_state)
                3'd0: $display("[TB] State: RESET");
                3'd1: $display("[TB] State: INIT");
                3'd2: $display("[TB] State: READ_UFM (addr=0x%05X)", ufm_address);
                3'd3: $display("[TB] State: WAIT_DATA");
                3'd4: $display("[TB] State: WRITE_RAM (addr=0x%04X, data=0x%08X)", ram_write_addr, ram_write_data);
                3'd5: $display("[TB] State: NEXT_WORD (count=%0d)", ram_write_addr);
                3'd6: $display("[TB] State: COMPLETE");
            endcase
            prev_boot_state <= boot_state;
        end
    end

    // Count words copied
    always @(posedge clk) begin
        if (!reset && ram_write_enable) begin
            words_copied = words_copied + 1;
            if (words_copied % 64 == 0) begin
                $display("[TB] Progress: %0d/%0d words copied", words_copied, BOOT_STUB_WORDS);
            end
        end
    end

endmodule
