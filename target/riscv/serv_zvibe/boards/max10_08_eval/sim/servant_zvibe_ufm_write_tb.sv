// Copyright (c) 2025 Martin R. Raumann
// SPDX-License-Identifier: BSD-3-Clause

///////////////////////////////////////////////////////////////////////////////
// servant_zvibe_ufm_write_tb.sv
//
// MAX10 SoC Testbench with UFM Unified Controller Testing
//
// This testbench verifies:
// 1. CPU boots from UFM @ 0x80000100 (XIP)
// 2. UFM unified controller responds to CSR register accesses at 0x80040000
// 3. Unified controller state machine operates correctly
// 4. UFM CSR interface timing
//
// Tests firmware that attempts to read UFM CSR registers and
// reports results via UART.
//
///////////////////////////////////////////////////////////////////////////////

`timescale 1ns / 1ps
`default_nettype none

module servant_zvibe_ufm_write_tb;

    // Pass/fail flag (set by success detection; visible to final block)
    logic success = 1'b0;

    //========================================================================
    // Parameters
    //========================================================================
    localparam CLK_50MHZ_PERIOD = 20;  // 50MHz external clock (20ns)
    // USE_INTERNAL_OSC removed: RTL always uses PLL (no internal OSC option)

    //========================================================================
    // External Clock (50MHz)
    //========================================================================
    logic clk_50mhz = 0;
    always #(CLK_50MHZ_PERIOD/2) clk_50mhz = ~clk_50mhz;

    //========================================================================
    // Board Inputs
    //========================================================================
    logic dip_sw1 = 1'b0;      // PLL async reset (normally OFF)
    logic uart_rxd = 1'b1;     // UART receive (idle high)

    //========================================================================
    // Board Outputs
    //========================================================================
    wire uart_txd;           // UART transmit
    wire [4:0] gpio_led;     // LED outputs

    //========================================================================
    // Internal Probes (for monitoring)
    //========================================================================
    wire sys_clk;            // System clock
    wire reset;              // System reset

    // UFM unified controller signals (uses wb_flash interface)
    wire        wb_flash_cyc;
    wire        wb_flash_stb;
    wire [31:0] wb_flash_adr;
    wire [31:0] wb_flash_dat;
    wire        wb_flash_we;
    wire [31:0] wb_flash_rdt;
    wire        wb_flash_ack;
    wire [2:0]  unified_state;

    // UFM IP CSR signals
    wire        ufm_csr_read;
    wire        ufm_csr_write;
    wire [31:0] ufm_csr_readdata;

    // CPU signals
    wire [31:0] cpu_pc;
    wire        cpu_ibus_cyc;
    wire        cpu_dbus_cyc;

    // Access internal signals for monitoring
    assign sys_clk = dut.sys_clk;
    assign reset = dut.reset;

    // UFM unified controller signals
    assign wb_flash_cyc = dut.wb_flash_cyc;
    assign wb_flash_stb = dut.wb_flash_stb;
    assign wb_flash_adr = dut.wb_flash_adr;
    assign wb_flash_dat = dut.wb_flash_dat;
    assign wb_flash_we = dut.wb_flash_we;
    assign wb_flash_rdt = dut.wb_flash_rdt;
    assign wb_flash_ack = dut.wb_flash_ack;
    assign unified_state = dut.ufm_unified.state_q;

    // UFM CSR signals
    assign ufm_csr_read = dut.ufm_inst.avmm_csr_read;
    assign ufm_csr_write = dut.ufm_inst.avmm_csr_write;
    assign ufm_csr_readdata = dut.ufm_inst.avmm_csr_readdata;

    // CPU signals - simplified access
    wire [31:0] cpu_ibus_adr = wb_flash_adr;  // Use flash address as proxy for CPU activity
    assign cpu_pc = cpu_ibus_adr;
    assign cpu_ibus_cyc = wb_flash_cyc;
    assign cpu_dbus_cyc = wb_flash_cyc;

    //========================================================================
    // Cycle Counter
    //========================================================================
    integer cycle_count = 0;
    always_ff @(posedge sys_clk) begin
        if (!reset) cycle_count = cycle_count + 1;
    end

    //========================================================================
    // Initialize UFM with Test Firmware
    //========================================================================
    initial begin
        integer i;
        #1;  // Wait for DUT instantiation

        // NOTE: When using vendor UFM model, we can't preload memory easily
        // The vendor model doesn't expose a simple memory array
        // For this test, we're mainly interested in whether CSR reads hang or complete
        // The CPU will execute whatever is in the UFM (likely all 0xFF or 0x00)

        $display("[TB %t] Using vendor UFM IP model (no memory preload)", $time);
        $display("[TB %t] Test will check if CSR reads hang or complete", $time);
    end

    //========================================================================
    // DUT: Top-Level Board Design
    //========================================================================
    servant_zvibe_max10_08_eval_xip #(
        .FAST_UART(1)  // Use fast Wishbone UART model for simulation
    ) dut (
        .clk_50mhz(clk_50mhz),
        .dip_sw1(dip_sw1),
        .uart_rxd(uart_rxd),
        .uart_txd(uart_txd),
        .gpio_led(gpio_led)
    );

    //========================================================================
    // UART Monitor
    //========================================================================
    localparam UART_BIT_PERIOD = 864;  // Clock cycles per bit @ 100MHz, 115200 baud

    logic [7:0] uart_buffer[0:4095];
    integer uart_count = 0;

    // UART receive task
    task uart_receive;
        integer i;
        logic [7:0] rx_byte;
        begin
            // Wait for start bit
            @(negedge uart_txd);

            // Wait to center of start bit
            repeat(UART_BIT_PERIOD/2) @(posedge sys_clk);

            // Skip rest of start bit
            repeat(UART_BIT_PERIOD) @(posedge sys_clk);

            // Sample 8 data bits
            for (i = 0; i < 8; i = i + 1) begin
                rx_byte[i] = uart_txd;
                repeat(UART_BIT_PERIOD) @(posedge sys_clk);
            end

            // Store byte and display
            uart_buffer[uart_count] = rx_byte;
            uart_count = uart_count + 1;

            if (rx_byte >= 32 && rx_byte < 127) begin
                $write("%c", rx_byte);
            end else if (rx_byte == 10) begin  // LF
                $display("");
            end else if (rx_byte == 13) begin  // CR
                // Skip CR
            end else begin
                $display("[UART %t] Non-printable: 0x%02X", $time, rx_byte);
            end

            $fflush();
        end
    endtask

    // UART monitor process
    initial begin
        #1000;  // Wait for reset
        $display("[UART_MON %t] Starting UART monitor, uart_txd=%b", $time, uart_txd);
        forever begin
            $display("[UART_MON %t] Waiting for start bit (negedge uart_txd)...", $time);
            uart_receive();
            $display("[UART_MON %t] Received byte, waiting for next...", $time);
        end
    end

    //========================================================================
    // UFM Write Bridge Monitor
    //========================================================================
    always_ff @(posedge sys_clk) begin
        if (!reset && wb_flash_cyc && wb_flash_stb) begin
            if (wb_flash_we) begin
                $display("[UFM_WR %t] Write: addr=0x%08X data=0x%08X state=%0d",
                         $time, wb_flash_adr, wb_flash_dat, unified_state);
            end else begin
                $display("[UFM_RD %t] Read:  addr=0x%08X state=%0d ack=%0d",
                         $time, wb_flash_adr, unified_state, wb_flash_ack);
                if (wb_flash_ack) begin
                    $display("[UFM_RD %t]   data=0x%08X", $time, wb_flash_rdt);
                end
            end
        end
    end

    // Monitor bridge state transitions
    logic [1:0] last_unified_state = 0;
    always_ff @(posedge sys_clk) begin
        if (!reset && unified_state != last_unified_state) begin
            $display("[BRIDGE %t] State: %0d -> %0d", $time, last_unified_state, unified_state);
            last_unified_state <= unified_state;
        end
    end

    // Monitor UFM IP clock and reset
    integer ufm_clk_toggle_count = 0;
    always_ff @(posedge dut.ufm_inst.clock) begin
        ufm_clk_toggle_count = ufm_clk_toggle_count + 1;
    end

    initial begin
        #100000;  // Wait 100us
        if (ufm_clk_toggle_count == 0) begin
            $display("ERROR: UFM IP clock is NOT toggling!");
        end else begin
            $display("[UFM_CLK] Clock toggled %0d times in first 100us", ufm_clk_toggle_count);
        end

        $display("[UFM_RESET] reset_n = %0d", dut.ufm_inst.reset_n);
    end

    // Monitor UFM CSR access
    always_ff @(posedge sys_clk) begin
        if (!reset && (ufm_csr_read || ufm_csr_write)) begin
            if (ufm_csr_read) begin
                $display("[UFM_CSR %t] CSR Read:  data=0x%08X", $time, ufm_csr_readdata);
            end
            if (ufm_csr_write) begin
                $display("[UFM_CSR %t] CSR Write: addr=%b data=0x%08X",
                         $time, dut.ufm_unified.avmm_csr_addr_r, dut.ufm_unified.avmm_csr_writedata_r);
            end
        end
    end

    // Monitor UFM DATA interface (detailed)
    always_ff @(posedge sys_clk) begin
        if (!reset) begin
            // Log data_read assertion with ALL interface signals
            if (dut.ufm_unified.avmm_data_read_r && !$past(dut.ufm_unified.avmm_data_read_r)) begin
                $display("[UFM_DATA %t] data_read RISING: addr=0x%05X (word) state=%0d",
                         $time, dut.ufm_unified.avmm_data_addr_r, dut.ufm_unified.state_q);
                $display("             CSR: addr=%b read=%b write=%b writedata=0x%08X",
                         dut.ufm_unified.avmm_csr_addr_r, dut.ufm_unified.avmm_csr_read_r,
                         dut.ufm_unified.avmm_csr_write_r, dut.ufm_unified.avmm_csr_writedata_r);
                $display("             Data: write=%b writedata=0x%08X burstcount=%0d",
                         dut.ufm_unified.avmm_data_write_r, dut.ufm_unified.avmm_data_writedata_r,
                         dut.ufm_unified.avmm_data_burstcount_r);
            end
            // Log data_read deassertion
            if (!dut.ufm_unified.avmm_data_read_r && $past(dut.ufm_unified.avmm_data_read_r)) begin
                $display("[UFM_DATA %t] data_read FALLING: state=%0d",
                         $time, dut.ufm_unified.state_q);
            end
            // Log readdatavalid
            if (dut.ufm_inst.avmm_data_readdatavalid) begin
                $display("[UFM_DATA %t] readdatavalid=1: data=0x%08X state=%0d",
                         $time, dut.ufm_inst.avmm_data_readdata, dut.ufm_unified.state_q);
            end
            // Log waitrequest changes
            if (dut.ufm_inst.avmm_data_waitrequest && !$past(dut.ufm_inst.avmm_data_waitrequest)) begin
                $display("[UFM_DATA %t] waitrequest ASSERTED", $time);
            end
            if (!dut.ufm_inst.avmm_data_waitrequest && $past(dut.ufm_inst.avmm_data_waitrequest)) begin
                $display("[UFM_DATA %t] waitrequest DEASSERTED", $time);
            end
        end
    end

    // Monitor unified controller state changes
    logic [2:0] last_state;
    always_ff @(posedge sys_clk) begin
        if (!reset) begin
            if (dut.ufm_unified.state_q != last_state) begin
                $display("[UFM_STATE %t] State transition: %0d -> %0d",
                         $time, last_state, dut.ufm_unified.state_q);
                last_state <= dut.ufm_unified.state_q;
            end
        end else begin
            last_state <= 3'd0;
        end
    end

    //========================================================================
    // UART Peripheral Monitor
    //========================================================================
    // Note: With FAST_UART=1, the uart_wb_model handles output directly
    // We only monitor the Wishbone write at the peripheral level
    always_ff @(posedge sys_clk) begin
        if (!reset) begin
            // Monitor writes to UART TX DATA register (wb_uart_adr[7:0] == 0x00)
            if (dut.soc.wb_uart_ack && dut.soc.wb_uart_we &&
                (dut.soc.wb_uart_adr[7:0] == 8'h00)) begin
                $display("[UART_WB %t] TX Write: 0x%02X ('%c')",
                         $time,
                         dut.soc.wb_uart_dat[7:0],
                         (dut.soc.wb_uart_dat[7:0] >= 32 && dut.soc.wb_uart_dat[7:0] < 127) ?
                         dut.soc.wb_uart_dat[7:0] : ".");
            end
        end
    end

    //========================================================================
    // Test Control
    //========================================================================
    localparam MAX_CYCLES = 100000000;  // 1 second @ 100MHz (extended for erase/program test)

    initial begin
        $display("========================================");
        $display("MAX10 UFM Unified Controller Testbench");
        $display("========================================");
        $display("Clock: PLL 100MHz");
        $display("");

        // Wait for reset release
        wait(!reset);
        $display("[TB %t] Reset released, CPU starting", $time);

        // Wait for test completion or timeout
        repeat(MAX_CYCLES) @(posedge sys_clk);

        $display("");
        $display("========================================");
        $display("Simulation timeout after %0d cycles", MAX_CYCLES);
        $display("========================================");
        $finish;
    end

    // Success detection
    always_ff @(posedge sys_clk) begin
        if (uart_count > 0 && uart_buffer[uart_count-1] == "!" &&
            uart_buffer[uart_count-2] == "d" &&
            uart_buffer[uart_count-3] == "e" &&
            uart_buffer[uart_count-4] == "t" &&
            uart_buffer[uart_count-5] == "e" &&
            uart_buffer[uart_count-6] == "l" &&
            uart_buffer[uart_count-7] == "p" &&
            uart_buffer[uart_count-8] == "m" &&
            uart_buffer[uart_count-9] == "o" &&
            uart_buffer[uart_count-10] == "c") begin
            $display("");
            $display("========================================");
            $display("TEST PASSED - 'completed!' detected");
            $display("Cycles: %0d", cycle_count);
            $display("========================================");
            success = 1'b1;
            $finish;
        end
    end

    // Deadlock detection - CPU stuck at same PC
    logic [31:0] last_pc = 0;
    integer stuck_count = 0;
    always_ff @(posedge sys_clk) begin
        if (!reset && cpu_ibus_cyc) begin
            if (cpu_pc == last_pc) begin
                stuck_count <= stuck_count + 1;
                if (stuck_count > 10000) begin
                    $display("");
                    $display("========================================");
                    $display("ERROR: CPU stuck at PC=0x%08X for %0d cycles", cpu_pc, stuck_count);
                    $display("Bridge state: %0d", unified_state);
                    $display("UFM cyc/stb: %b/%b", wb_flash_cyc, wb_flash_stb);
                    $display("UFM ack: %b", wb_flash_ack);
                    $display("========================================");
                    $finish;
                end
            end else begin
                last_pc <= cpu_pc;
                stuck_count <= 0;
            end
        end
    end

    //========================================================================
    // Final PASS/FAIL Report
    //========================================================================
    final begin
        if (success)
            $display("PASS: servant_zvibe_ufm_write_tb");
        else
            $display("FAIL: servant_zvibe_ufm_write_tb (timeout)");
    end

endmodule

`default_nettype wire
