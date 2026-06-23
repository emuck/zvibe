// Copyright (c) 2025 Martin R. Raumann
// SPDX-License-Identifier: BSD-3-Clause

// Testbench for servant_zvibe_mux
// Tests pipelined ack and peripheral address decoding (case statement optimization)

`timescale 1ns/1ps
`default_nettype none

module servant_zvibe_mux_tb();
    // Test identity and timeout
    parameter string TEST_NAME      = "servant_zvibe_mux_tb";
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
    logic         cpu_we;
    logic         cpu_cyc;
    logic [31:0]  cpu_rdt;
    logic         cpu_ack;

    logic [31:0]  uart_adr;
    logic [31:0]  uart_dat;
    logic         uart_we;
    logic         uart_cyc;
    logic [31:0]  uart_rdt;
    logic         uart_ack;

    logic [31:0]  timer_dat;
    logic         timer_we;
    logic         timer_cyc;
    logic [31:0]  timer_rdt;

    logic [31:0]  gpio_adr;
    logic [31:0]  gpio_dat;
    logic         gpio_we;
    logic         gpio_cyc;
    logic [31:0]  gpio_rdt;
    logic         gpio_ack;

    logic         flash_status_cyc;
    logic [31:0]  flash_status_rdt;
    logic         flash_status_ack;

    // Instantiate DUT
    servant_zvibe_mux dut (
        .i_wb_cpu_adr(cpu_adr),
        .i_wb_cpu_dat(cpu_dat),
        .i_wb_cpu_we(cpu_we),
        .i_wb_cpu_cyc(cpu_cyc),
        .o_wb_cpu_rdt(cpu_rdt),
        .o_wb_cpu_ack(cpu_ack),

        .o_wb_uart_adr(uart_adr),
        .o_wb_uart_dat(uart_dat),
        .o_wb_uart_we(uart_we),
        .o_wb_uart_cyc(uart_cyc),
        .i_wb_uart_rdt(uart_rdt),
        .i_wb_uart_ack(uart_ack),

        .o_wb_timer_dat(timer_dat),
        .o_wb_timer_we(timer_we),
        .o_wb_timer_cyc(timer_cyc),
        .i_wb_timer_rdt(timer_rdt),

        .o_wb_gpio_adr(gpio_adr),
        .o_wb_gpio_dat(gpio_dat),
        .o_wb_gpio_we(gpio_we),
        .o_wb_gpio_cyc(gpio_cyc),
        .i_wb_gpio_rdt(gpio_rdt),
        .i_wb_gpio_ack(gpio_ack),

        .o_wb_flash_status_cyc(flash_status_cyc),
        .i_wb_flash_status_rdt(flash_status_rdt),
        .i_wb_flash_status_ack(flash_status_ack)
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
        $display("servant_zvibe_mux Unit Tests");
        $display("Testing pipelined ack and case statement decode");
        $display("========================================");
        $display("");

        // Initialize
        cpu_adr = 0;
        cpu_dat = 0;
        cpu_we = 0;
        cpu_cyc = 0;
        uart_rdt = 32'h11111111;
        uart_ack = 0;
        timer_rdt = 32'h22222222;
        gpio_rdt = 32'h33333333;
        gpio_ack = 0;
        flash_status_rdt = 32'h44444444;
        flash_status_ack = 0;

        // Release reset
        #100 rst = 0;
        #10;

        // TEST 1: UART address decode (bits [5:4] = 00)
        $display("TEST 1: UART address decoding (0x40000000-0x4000000F)");
        cpu_adr = 32'h40000000;
        cpu_cyc = 1;
        #10;
        check(uart_cyc == 1'b1, "UART cyc asserted for 0x40000000");
        check(timer_cyc == 1'b0, "Timer cyc not asserted");
        check(gpio_cyc == 1'b0, "GPIO cyc not asserted");
        check(flash_status_cyc == 1'b0, "Flash status cyc not asserted");
        cpu_cyc = 0;
        #10;

        cpu_adr = 32'h4000000C;  // Also UART range
        cpu_cyc = 1;
        #10;
        check(uart_cyc == 1'b1, "UART cyc asserted for 0x4000000C");
        cpu_cyc = 0;
        #10;

        // TEST 2: Timer address decode (bits [5:4] = 01)
        $display("");
        $display("TEST 2: Timer address decoding (0x40000010-0x4000001F)");
        cpu_adr = 32'h40000010;
        cpu_cyc = 1;
        #10;
        check(uart_cyc == 1'b0, "UART cyc not asserted");
        check(timer_cyc == 1'b1, "Timer cyc asserted for 0x40000010");
        check(gpio_cyc == 1'b0, "GPIO cyc not asserted");
        check(flash_status_cyc == 1'b0, "Flash status cyc not asserted");
        cpu_cyc = 0;
        #10;

        cpu_adr = 32'h4000001C;  // Also Timer range
        cpu_cyc = 1;
        #10;
        check(timer_cyc == 1'b1, "Timer cyc asserted for 0x4000001C");
        cpu_cyc = 0;
        #10;

        // TEST 3: GPIO address decode (bits [5:4] = 10)
        $display("");
        $display("TEST 3: GPIO address decoding (0x40000020-0x4000002F)");
        cpu_adr = 32'h40000020;
        cpu_cyc = 1;
        #10;
        check(uart_cyc == 1'b0, "UART cyc not asserted");
        check(timer_cyc == 1'b0, "Timer cyc not asserted");
        check(gpio_cyc == 1'b1, "GPIO cyc asserted for 0x40000020");
        check(flash_status_cyc == 1'b0, "Flash status cyc not asserted");
        cpu_cyc = 0;
        #10;

        cpu_adr = 32'h40000028;  // Also GPIO range
        cpu_cyc = 1;
        #10;
        check(gpio_cyc == 1'b1, "GPIO cyc asserted for 0x40000028");
        cpu_cyc = 0;
        #10;

        // TEST 4: Flash Status address decode (bits [5:4] = 11)
        $display("");
        $display("TEST 4: Flash Status address decoding (0x40000030-0x4000003F)");
        cpu_adr = 32'h40000030;
        cpu_cyc = 1;
        #10;
        check(uart_cyc == 1'b0, "UART cyc not asserted");
        check(timer_cyc == 1'b0, "Timer cyc not asserted");
        check(gpio_cyc == 1'b0, "GPIO cyc not asserted");
        check(flash_status_cyc == 1'b1, "Flash status cyc asserted for 0x40000030");
        cpu_cyc = 0;
        #10;

        cpu_adr = 32'h4000003C;  // Also Flash Status range
        cpu_cyc = 1;
        #10;
        check(flash_status_cyc == 1'b1, "Flash status cyc asserted for 0x4000003C");
        cpu_cyc = 0;
        #10;

        // TEST 5: Combinatorial ack - UART
        $display("");
        $display("TEST 5: Combinatorial ack timing (UART)");
        cpu_adr = 32'h40000000;
        cpu_cyc = 1;
        uart_ack = 1;
        #10;
        check(cpu_ack == 1'b1, "CPU ack asserted (combinatorial)");
        cpu_cyc = 0;
        uart_ack = 0;
        #10;
        check(cpu_ack == 1'b0, "CPU ack cleared");
        #10;

        // TEST 6: Combinatorial ack - Timer (immediate ack)
        $display("");
        $display("TEST 6: Timer immediate ack (combinatorial)");
        cpu_adr = 32'h40000010;
        cpu_cyc = 1;
        #10;
        check(cpu_ack == 1'b1, "CPU ack asserted (timer acks via cyc)");
        cpu_cyc = 0;
        #10;
        check(cpu_ack == 1'b0, "CPU ack cleared");
        #10;

        // TEST 7: Combinatorial ack - GPIO
        $display("");
        $display("TEST 7: Combinatorial ack timing (GPIO)");
        cpu_adr = 32'h40000020;
        cpu_cyc = 1;
        gpio_ack = 1;
        #10;
        check(cpu_ack == 1'b1, "CPU ack asserted (combinatorial)");
        cpu_cyc = 0;
        gpio_ack = 0;
        #10;
        #10;

        // TEST 8: Combinatorial ack - Flash Status
        $display("");
        $display("TEST 8: Combinatorial ack timing (Flash Status)");
        cpu_adr = 32'h40000030;
        cpu_cyc = 1;
        flash_status_ack = 1;
        #10;
        check(cpu_ack == 1'b1, "CPU ack asserted (combinatorial)");
        cpu_cyc = 0;
        flash_status_ack = 0;
        #10;
        #10;

        // TEST 9: Data routing - All peripherals
        $display("");
        $display("TEST 9: Data routing via case statement");

        uart_rdt = 32'hAAAAAAAA;
        cpu_adr = 32'h40000000;
        cpu_cyc = 1;
        #10;
        check(cpu_rdt == 32'hAAAAAAAA, "UART data routed");
        cpu_cyc = 0;
        #10;

        timer_rdt = 32'hBBBBBBBB;
        cpu_adr = 32'h40000010;
        cpu_cyc = 1;
        #10;
        check(cpu_rdt == 32'hBBBBBBBB, "Timer data routed");
        cpu_cyc = 0;
        #10;

        gpio_rdt = 32'hCCCCCCCC;
        cpu_adr = 32'h40000020;
        cpu_cyc = 1;
        #10;
        check(cpu_rdt == 32'hCCCCCCCC, "GPIO data routed");
        cpu_cyc = 0;
        #10;

        flash_status_rdt = 32'hDDDDDDDD;
        cpu_adr = 32'h40000030;
        cpu_cyc = 1;
        #10;
        check(cpu_rdt == 32'hDDDDDDDD, "Flash Status data routed");
        cpu_cyc = 0;
        #10;

        // TEST 10: Reset behavior
        $display("");
        $display("TEST 10: Reset behavior");
        cpu_adr = 32'h40000000;
        cpu_cyc = 1;
        uart_ack = 1;
        rst = 1;
        uart_ack = 0;  // Peripherals should deassert acks during reset
        #10;
        check(cpu_ack == 1'b0, "CPU ack cleared when peripheral acks are cleared");
        rst = 0;
        cpu_cyc = 0;
        #10;

        // TEST 11: Write enable propagation
        $display("");
        $display("TEST 11: Write enable propagation");
        cpu_we = 1;

        cpu_adr = 32'h40000000;
        cpu_cyc = 1;
        #10;
        check(uart_we == 1'b1, "UART write enable propagated");
        cpu_cyc = 0;
        #10;

        cpu_adr = 32'h40000010;
        cpu_cyc = 1;
        #10;
        check(timer_we == 1'b1, "Timer write enable propagated");
        cpu_cyc = 0;
        #10;

        cpu_adr = 32'h40000020;
        cpu_cyc = 1;
        #10;
        check(gpio_we == 1'b1, "GPIO write enable propagated");
        cpu_cyc = 0;
        cpu_we = 0;
        #10;

        // TEST 12: Rapid switching between peripherals
        $display("");
        $display("TEST 12: Rapid peripheral switching");
        uart_ack = 1;
        gpio_ack = 1;

        cpu_adr = 32'h40000000;  // UART
        cpu_cyc = 1;
        #10;
        check(uart_cyc == 1'b1, "UART selected");

        cpu_adr = 32'h40000020;  // Switch to GPIO
        #10;
        check(uart_cyc == 1'b0, "UART deselected");
        check(gpio_cyc == 1'b1, "GPIO selected");

        cpu_adr = 32'h40000010;  // Switch to Timer
        #10;
        check(gpio_cyc == 1'b0, "GPIO deselected");
        check(timer_cyc == 1'b1, "Timer selected");

        cpu_cyc = 0;
        uart_ack = 0;
        gpio_ack = 0;
        #10;

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
