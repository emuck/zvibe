// Copyright (c) 2025 Martin R. Raumann
// SPDX-License-Identifier: BSD-3-Clause

// Testbench for servant_mem_mux
// Tests pipelined ack and address decoding optimizations

`timescale 1ns/1ps
`default_nettype none

module servant_mem_mux_tb();
    // Test identity and timeout
    parameter string TEST_NAME      = "servant_mem_mux_tb";
    parameter int    MAX_SIM_CYCLES = 20_000;

    // Clock and reset
    logic clk = 0;
    logic rst = 1;

    always #5 clk = ~clk;  // 100MHz clock

    // Cycle counter and watchdog (replaces time-based timeout)
    int   cycle_count     = 0;
    logic timeout_occurred = 1'b0;

    always_ff @(posedge clk) cycle_count++;

    always_ff @(posedge clk) begin
        if (cycle_count >= MAX_SIM_CYCLES) begin
            $display("[TIMEOUT] %s timed out after %0d cycles", TEST_NAME, MAX_SIM_CYCLES);
            timeout_occurred = 1'b1;
            $finish;
        end
    end

    // DUT signals
    logic [31:0]  cpu_adr;
    logic [31:0]  cpu_dat;
    logic [3:0]   cpu_sel;
    logic         cpu_we;
    logic         cpu_stb;
    logic [31:0]  cpu_rdt;
    logic         cpu_ack;

    logic [31:0]  ram_adr;
    logic [31:0]  ram_dat;
    logic [3:0]   ram_sel;
    logic         ram_we;
    logic         ram_stb;
    logic [31:0]  ram_rdt;
    logic         ram_ack;

    logic [31:0]  flash_adr;
    logic [31:0]  flash_dat;
    logic [3:0]   flash_sel;
    logic         flash_we;
    logic         flash_cyc;
    logic         flash_stb;
    logic [31:0]  flash_rdt;
    logic         flash_ack;
    logic         flash_stall;

    // UFM write interface
    logic [31:0]  ufm_adr;
    logic [31:0]  ufm_dat;
    logic [3:0]   ufm_sel;
    logic         ufm_we;
    logic         ufm_cyc;
    logic         ufm_stb;
    logic [31:0]  ufm_rdt;
    logic         ufm_ack;

    // Instantiate DUT
    servant_mem_mux dut (
        .i_wb_cpu_adr(cpu_adr),
        .i_wb_cpu_dat(cpu_dat),
        .i_wb_cpu_sel(cpu_sel),
        .i_wb_cpu_we(cpu_we),
        .i_wb_cpu_stb(cpu_stb),
        .o_wb_cpu_rdt(cpu_rdt),
        .o_wb_cpu_ack(cpu_ack),

        .o_wb_ram_adr(ram_adr),
        .o_wb_ram_dat(ram_dat),
        .o_wb_ram_sel(ram_sel),
        .o_wb_ram_we(ram_we),
        .o_wb_ram_stb(ram_stb),
        .i_wb_ram_rdt(ram_rdt),
        .i_wb_ram_ack(ram_ack),

        .o_wb_flash_adr(flash_adr),
        .o_wb_flash_dat(flash_dat),
        .o_wb_flash_sel(flash_sel),
        .o_wb_flash_we(flash_we),
        .o_wb_flash_cyc(flash_cyc),
        .o_wb_flash_stb(flash_stb),
        .i_wb_flash_rdt(flash_rdt),
        .i_wb_flash_ack(flash_ack),
        .i_wb_flash_stall(flash_stall),

        .o_wb_ufm_adr(ufm_adr),
        .o_wb_ufm_dat(ufm_dat),
        .o_wb_ufm_sel(ufm_sel),
        .o_wb_ufm_we(ufm_we),
        .o_wb_ufm_cyc(ufm_cyc),
        .o_wb_ufm_stb(ufm_stb),
        .i_wb_ufm_rdt(ufm_rdt),
        .i_wb_ufm_ack(ufm_ack),

        // Debug outputs (leave unconnected)
        .o_debug_sel_ufm(),
        .o_debug_sel_flash(),
        .o_debug_cpu_stb()
    );

    // Test variables
    int test_count  = 0;
    int pass_count  = 0;
    int fail_count  = 0;
    int error_count = 0;

    // Task to check condition
    task check;
        input condition;
        input [256*8:1] test_name;
        begin
            test_count = test_count + 1;
            if (condition) begin
                $display("  ✓ PASS: %s", test_name);
                pass_count = pass_count + 1;
            end else begin
                $display("  ✗ FAIL: %s", test_name);
                fail_count  = fail_count  + 1;
                error_count = error_count + 1;
            end
        end
    endtask

    // Main test sequence
    initial begin
        $display("========================================");
        $display("servant_mem_mux Unit Tests");
        $display("Testing pipelined ack and address decode");
        $display("========================================");
        $display("");

        // Initialize
        cpu_adr = 0;
        cpu_dat = 0;
        cpu_sel = 4'hF;
        cpu_we = 0;
        cpu_stb = 0;
        ram_rdt = 32'hDEADBEEF;
        ram_ack = 0;
        flash_rdt = 32'hCAFEBABE;
        flash_ack = 0;
        flash_stall = 0;
        ufm_rdt = 32'h12345678;
        ufm_ack = 0;

        // Release reset
        #100 rst = 0;
        #10;

        // TEST 1: RAM address decode (address bit 31 = 0)
        $display("TEST 1: RAM address decoding");
        cpu_adr = 32'h00001000;
        cpu_stb = 1;
        #10;
        check(ram_stb == 1'b1, "RAM strobe asserted for addr 0x00001000");
        check(flash_stb == 1'b0, "Flash strobe not asserted");
        cpu_stb = 0;
        #10;

        // TEST 2: Flash address decode (address bit 31 = 1)
        $display("");
        $display("TEST 2: Flash address decoding");
        cpu_adr = 32'h80100000;
        cpu_stb = 1;
        #10;
        check(ram_stb == 1'b0, "RAM strobe not asserted");
        check(flash_stb == 1'b1, "Flash strobe asserted for addr 0x80100000");
        check(flash_cyc == 1'b1, "Flash cyc asserted");
        cpu_stb = 0;
        #10;

        // TEST 3: Combinatorial ack timing - RAM path
        $display("");
        $display("TEST 3: Combinatorial ack timing (RAM)");
        cpu_adr = 32'h00002000;
        cpu_stb = 1;
        ram_ack = 1;  // RAM responds immediately
        #10;
        check(cpu_ack == 1'b1, "CPU ack asserted (combinatorial)");
        cpu_stb = 0;
        ram_ack = 0;
        #10;
        check(cpu_ack == 1'b0, "CPU ack cleared after strobe removed");
        #10;

        // TEST 4: Combinatorial ack timing - Flash path
        $display("");
        $display("TEST 4: Combinatorial ack timing (Flash)");
        cpu_adr = 32'h80200000;
        cpu_stb = 1;
        flash_ack = 1;  // Flash responds immediately
        #10;
        check(cpu_ack == 1'b1, "CPU ack asserted (combinatorial)");
        cpu_stb = 0;
        flash_ack = 0;
        #10;
        check(cpu_ack == 1'b0, "CPU ack cleared");
        #10;

        // TEST 5: Data routing - RAM
        $display("");
        $display("TEST 5: Data routing (RAM)");
        cpu_adr = 32'h00003000;
        cpu_stb = 1;
        ram_rdt = 32'h12345678;
        ram_ack = 1;
        #20;  // Wait for pipeline
        check(cpu_rdt == 32'h12345678, "RAM data routed correctly");
        cpu_stb = 0;
        ram_ack = 0;
        #10;

        // TEST 6: Data routing - Flash
        $display("");
        $display("TEST 6: Data routing (Flash)");
        cpu_adr = 32'h80300000;
        cpu_stb = 1;
        flash_rdt = 32'h9ABCDEF0;
        flash_ack = 1;
        #20;  // Wait for pipeline
        check(cpu_rdt == 32'h9ABCDEF0, "Flash data routed correctly");
        cpu_stb = 0;
        flash_ack = 0;
        #10;

        // TEST 7: Ack passthrough when strobe active (reset handled upstream, not in mux)
        // servant_mem_mux has no reset port; strobe gating happens in servant_zvibe.v
        $display("");
        $display("TEST 7: Ack passthrough (no reset logic in mux)");
        cpu_adr = 32'h00004000;
        cpu_stb = 1;
        ram_ack = 1;
        rst = 1;
        #10;
        check(cpu_ack == 1'b1, "CPU ack passes through when strobe active (mux has no reset)");
        rst = 0;
        #10;

        // TEST 8: Address boundary (0x7FFFFFFF should be RAM, 0x80000000 should be Flash)
        $display("");
        $display("TEST 8: Address boundary conditions");
        cpu_adr = 32'h7FFFFFFF;
        cpu_stb = 1;
        #10;
        check(ram_stb == 1'b1, "0x7FFFFFFF routes to RAM");
        check(flash_stb == 1'b0, "0x7FFFFFFF does not route to Flash");
        cpu_stb = 0;
        #10;

        cpu_adr = 32'h80000000;
        cpu_stb = 1;
        #10;
        check(ram_stb == 1'b0, "0x80000000 does not route to RAM");
        check(flash_stb == 1'b1, "0x80000000 routes to Flash");
        cpu_stb = 0;
        #10;

        // TEST 9: Write enable propagation
        $display("");
        $display("TEST 9: Write enable propagation");
        cpu_adr = 32'h00005000;
        cpu_we = 1;
        cpu_stb = 1;
        #10;
        check(ram_we == 1'b1, "RAM write enable propagated");
        cpu_stb = 0;
        cpu_we = 0;
        #10;

        cpu_adr = 32'h80400000;
        cpu_we = 1;
        cpu_stb = 1;
        #10;
        check(flash_we == 1'b1, "Flash write enable propagated");
        cpu_stb = 0;
        cpu_we = 0;
        #10;

        // TEST 10: Multiple rapid requests
        $display("");
        $display("TEST 10: Back-to-back requests");
        cpu_adr = 32'h00006000;
        cpu_stb = 1;
        ram_ack = 1;
        #10;  // Cycle 1
        cpu_adr = 32'h00007000;
        #10;  // Cycle 2
        check(cpu_ack == 1'b1, "First ack arrives");
        #10;  // Cycle 3
        check(cpu_ack == 1'b1, "Second ack arrives");
        cpu_stb = 0;
        ram_ack = 0;
        #20;

        // Summary
        $display("");
        $display("========================================");
        $display("Test Summary");
        $display("========================================");
        $display("Tests run:    %0d", test_count);
        $display("Tests passed: %0d", pass_count);
        $display("Tests failed: %0d", fail_count);

        if (fail_count == 0) begin
            $display("");
            $display("*** ALL TESTS PASSED ***");
            $display("========================================");
        end else begin
            $display("");
            $display("*** SOME TESTS FAILED ***");
            $display("========================================");
        end

        $finish;
    end

    // Final PASS/FAIL report
    final begin
        if (timeout_occurred)
            $display("FAIL: %s (timeout)", TEST_NAME);
        else if (error_count == 0)
            $display("PASS: %s", TEST_NAME);
        else
            $display("FAIL: %s (%0d errors)", TEST_NAME, error_count);
    end

endmodule

`default_nettype wire
