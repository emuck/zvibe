// Copyright (c) 2025 Martin R. Raumann
// SPDX-License-Identifier: BSD-3-Clause

//============================================================================
// Servant ZVibe SoC XIP Testbench
//
// Comprehensive verification of QSPI XIP integration
// Tests full SoC with s25fl_xip controller and s25fl_simple_rw flash model
//
// Features:
// - Full SoC instantiation (CPU + RAM + Flash + UART)
// - s25fl_simple_rw flash model with file loading
// - UART monitor and output capture
// - Automated test checking
// - Dual-simulator support (Verilator + Vivado xsim)
//
// See: QSPI_XIP_SOC_TEST_PLAN.md for test specification
//============================================================================

`timescale 1ns/1ps
`default_nettype none

module servant_zvibe_xip_tb();

    //========================================================================
    // Parameters
    //========================================================================
    parameter CLK_FREQ_HZ = 100_000_000;  // 100MHz system clock
    parameter CLK_PERIOD_NS = 10;         // 10ns period
    parameter UART_BAUD = 115200;
    parameter MEM_SIZE = 32*1024;         // 32KB RAM
    parameter FLASH_SIZE = 24;            // 16MB flash (24-bit address)

    // File paths (relative to build directory for Verilator)
    parameter BOOT_STUB_HEX = "ram_boot.hex";  // RAM preload (set to "" for direct XIP boot)
    parameter FLASH_FW_HEX = "test_firmware.hex";         // Test program in flash
    parameter FLASH_OFFSET = 24'h100000;                  // Flash load offset (1MB for boot stub @ 0x80100000)
    parameter RESET_PC = 32'h00000000;         // CPU reset PC (0x0=RAM boot, 0x80100100=XIP direct)

    // UART I/O files (set to "" to disable)
    parameter UART_OUTPUT_FILE = "uart_output.txt";       // TX capture file
    parameter UART_INPUT_FILE = "uart_input.txt";         // RX stimulus file

    // UART Model Selection
    // FAST_UART: Use fast Wishbone model (bypasses bit-level serialization)
    //   0 = Real UART with TX/RX serialization (accurate, slow)
    //   1 = Fast Wishbone model (instant writes, 100x+ faster)
    parameter FAST_UART = 1;  // Default to fast mode for development

    // Flash configuration
    // USE_CACHE: Insert 4KB BRAM cache between SoC WB and s25fl_xip
    //   0 = Direct XIP (BURST_WORDS=1, default)
    //   1 = 4KB BRAM cache + BURST_WORDS=4 XIP (matches Arty board wrapper)
    parameter USE_CACHE = 0;

    // Simulation control
    // MAX_CYCLES: Maximum simulation time (0 = unlimited)
    //   1_000_000 = 10ms (quick tests)
    //   100_000_000 = 1 second
    //   600_000_000_000 = 1 hour @ 100MHz
    parameter MAX_CYCLES = 100_000_000;  // 1 second default (override with +define+)

    // INACTIVITY_TIMEOUT: Auto-exit if no UART activity (0 = disabled)
    parameter INACTIVITY_TIMEOUT = 50_000_000;  // 500ms of no UART = exit

    // Magic exit sequence: firmware can write this to UART to gracefully exit
    // Writing "##EXIT##" to UART will end simulation immediately
    parameter ENABLE_EXIT_SEQUENCE = 1;

    //========================================================================
    // Clock and Reset
    //========================================================================
    logic clk = 0;
    logic rst = 1;

    // 100MHz clock generation
    always #(CLK_PERIOD_NS/2) clk = ~clk;

    // Cycle counter
    integer cycle_count = 0;
    always_ff @(posedge clk) begin
        if (!rst) cycle_count <= cycle_count + 1;
    end

    // Timeout watchdog
    always_ff @(posedge clk) begin
        if (MAX_CYCLES > 0 && cycle_count >= MAX_CYCLES) begin
            $display("\n[TB %t] ERROR: Simulation timeout after %0d cycles (%.3f sec)",
                     $time, MAX_CYCLES, $time / 1_000_000_000.0);
            $display("[TB] Test FAILED - maximum cycles reached");
            $display("[TB] Hint: Increase MAX_CYCLES or use 0 for unlimited");
            $finish;
        end
    end

    //========================================================================
    // UART Signals and Monitor
    //========================================================================
    logic uart_txd;
    logic uart_rxd = 1'b1;  // Idle high

    // UART RX stimulus for FAST_UART model (bypass serial)
    logic       uart_rx_valid = 1'b0;
    logic [7:0] uart_rx_char = 8'h00;

    // UART output capture buffer
    logic [7:0] uart_rx_buffer[0:1023];
    integer uart_rx_count = 0;

    // UART input stimulus buffer
    logic [7:0] uart_tx_buffer[0:1023];
    integer uart_tx_count = 0;
    integer uart_tx_index = 0;

    // File handles
    integer uart_output_fd;
    integer uart_input_fd;
    integer input_file_char;

    // Inactivity detection
    integer cycles_since_uart = 0;
    integer last_uart_cycle = 0;

    // Exit sequence detection
    logic [7:0] exit_seq[0:8];  // "##EXIT##\n"
    integer exit_match_pos = 0;

    // UART bit timing
    localparam UART_BIT_PERIOD = CLK_FREQ_HZ / UART_BAUD;

    // Initialize exit sequence
    initial begin
        exit_seq[0] = "#";
        exit_seq[1] = "#";
        exit_seq[2] = "E";
        exit_seq[3] = "X";
        exit_seq[4] = "I";
        exit_seq[5] = "T";
        exit_seq[6] = "#";
        exit_seq[7] = "#";
        exit_seq[8] = "\n";
    end

    // UART RX monitor task
    task uart_monitor;
        integer bit_time;
        integer i;
        logic [7:0] rx_byte;
        begin
            // Wait for start bit (1 → 0)
            @(negedge uart_txd);
            $display("[UART %t] Start bit detected", $time);

            // Sample in middle of each bit
            bit_time = UART_BIT_PERIOD;

            // Wait half bit period to center in start bit
            repeat(bit_time/2) @(posedge clk);

            // Skip start bit, sample data bits
            repeat(bit_time) @(posedge clk);

            // Sample 8 data bits (LSB first)
            for (i = 0; i < 8; i = i + 1) begin
                rx_byte[i] = uart_txd;
                repeat(bit_time) @(posedge clk);
            end

            // Check stop bit
            if (uart_txd !== 1'b1) begin
                $display("[UART %t] WARNING: Invalid stop bit", $time);
            end

            // Store received byte
            uart_rx_buffer[uart_rx_count] = rx_byte;
            uart_rx_count = uart_rx_count + 1;

            $display("[UART %t] Received: 0x%02h '%c'", $time, rx_byte,
                     (rx_byte >= 32 && rx_byte < 127) ? rx_byte : ".");

            // Update inactivity timer
            last_uart_cycle = cycle_count;

            // Check for exit sequence
            if (ENABLE_EXIT_SEQUENCE) begin
                if (rx_byte == exit_seq[exit_match_pos]) begin
                    exit_match_pos = exit_match_pos + 1;
                    if (exit_match_pos >= 9) begin
                        $display("\n[TB %t] ✓ Exit sequence detected: ##EXIT##", $time);
                        $display("[TB] Test completed gracefully");
                        $display("[TB] Total simulation time: %.3f seconds", $time / 1_000_000_000.0);
                        if (uart_output_fd != 0) $fclose(uart_output_fd);
                        $finish;
                    end
                end else begin
                    exit_match_pos = 0;  // Reset on mismatch
                end
            end

            // Write to output file if enabled
            if (uart_output_fd != 0) begin
                $fwrite(uart_output_fd, "%c", rx_byte);
                $fflush(uart_output_fd);  // Flush immediately for real-time viewing
            end
        end
    endtask

    // UART monitor process
    initial begin
        forever uart_monitor();
    end

    // Inactivity watchdog - exits if no UART activity for too long
    always_ff @(posedge clk) begin
        if (!rst && INACTIVITY_TIMEOUT > 0) begin
            cycles_since_uart = cycle_count - last_uart_cycle;
            if (cycles_since_uart >= INACTIVITY_TIMEOUT) begin
                $display("\n[TB %t] WARNING: No UART activity for %0d cycles (%.3f sec)",
                         $time, INACTIVITY_TIMEOUT, INACTIVITY_TIMEOUT / 100_000_000.0);
                $display("[TB] Assuming test complete or stuck - exiting");
                $display("[TB] Last UART at cycle %0d", last_uart_cycle);
                if (uart_output_fd != 0) $fclose(uart_output_fd);
                $finish;
            end
        end
    end

    // UART TX (input stimulus) task - sends byte to DUT's RX
    task uart_send_byte;
        input [7:0] tx_byte;
        integer bit_time;
        integer i;
        begin
            bit_time = UART_BIT_PERIOD;

            $display("[UART_TX %t] Sending: 0x%02h '%c'", $time, tx_byte,
                     (tx_byte >= 32 && tx_byte < 127) ? tx_byte : ".");

            // Start bit
            uart_rxd = 1'b0;
            repeat(bit_time) @(posedge clk);

            // Data bits (LSB first)
            for (i = 0; i < 8; i = i + 1) begin
                uart_rxd = tx_byte[i];
                repeat(bit_time) @(posedge clk);
            end

            // Stop bit
            uart_rxd = 1'b1;
            repeat(bit_time) @(posedge clk);
        end
    endtask

    // UART TX stimulus process - sends characters from file
    initial begin
        // Wait for reset and some bootup time
        wait(rst == 0);
        repeat(10000000) @(posedge clk);  // Wait 100ms after reset for firmware to boot

        // Send characters from input buffer if loaded
        while (uart_tx_index < uart_tx_count) begin
            if (FAST_UART == 1) begin
                // Fast path: drive uart_wb_model directly
                // Wait for RX buffer to be consumed (rx_buffer_valid == 0)
                while (dut.gen_fast_uart.uart.rx_buffer_valid) begin
                    repeat(1000) @(posedge clk);  // Wait 10us
                end

                uart_rx_char = uart_tx_buffer[uart_tx_index];
                uart_rx_valid = 1'b1;
                @(posedge clk);
                uart_rx_valid = 1'b0;
                $display("[UART_RX_FAST %t] Injected: 0x%02h '%c'", $time, uart_rx_char,
                         (uart_rx_char >= 32 && uart_rx_char < 127) ? uart_rx_char : ".");
            end else begin
                // Slow path: bit-bang serial protocol
                uart_send_byte(uart_tx_buffer[uart_tx_index]);
            end
            uart_tx_index = uart_tx_index + 1;

            // Small delay between characters to allow processing
            repeat(1000) @(posedge clk);  // 10us delay
        end
    end

    //========================================================================
    // QSPI Flash Signals (Tri-state)
    //========================================================================
    logic qspi_clk;
    logic qspi_cs_n;
    wire  [3:0] qspi_d;  // Bidirectional tri-state

    // GPIO LEDs (debug)
    logic [2:0] gpio_led;

    //========================================================================
    // Instantiate Servant ZVibe SoC
    //========================================================================
    servant_zvibe #(
        .memfile(BOOT_STUB_HEX),       // RAM preload ("" for direct XIP boot)
        .reset_pc(RESET_PC),           // 0x0=RAM boot stub, 0x80100100=XIP direct
        .memsize(MEM_SIZE),            // 32KB RAM
        .reset_strategy("MINI"),
        .with_csr(1),                  // Enable CSRs (needed for JAL?)
        .UART_PRESCALE(108),           // 100MHz / (115200 * 8) ≈ 108
        .UART_FIFO_DEPTH(64),
        .FAST_UART(FAST_UART),         // Use fast UART model for speed
        .FLASH_SIZE(FLASH_SIZE),       // 24-bit address = 16MB
        .USE_FAST_RAM_FLASH(0),        // Use REAL flash controller
        .USE_CACHE(USE_CACHE)          // Optional BRAM cache (set via -GUSE_CACHE=1)
        // SYNTH defaults to 0 for simulation
    ) dut (
        .wb_clk(clk),
        .wb_rst(rst),
        .uart_rxd(uart_rxd),
        .uart_txd(uart_txd),
        .uart_rx_valid(uart_rx_valid),
        .uart_rx_char(uart_rx_char),
        .qspi_clk(qspi_clk),
        .qspi_cs_n(qspi_cs_n),
        .qspi_d(qspi_d),
        .gpio_led(gpio_led)
    );

    //========================================================================
    // Instantiate S25FL128S Flash Model
    //========================================================================
    // Select flash model: USE_VENDOR_MODEL=1 for real S25FL128S (xsim only)
    //                     USE_VENDOR_MODEL=0 for s25fl_simple_rw (Verilator compatible)
`ifdef USE_VENDOR_MODEL
    // Real Spansion S25FL128S vendor model (has delay tasks - xsim only!)
    s25fl128s #(
        .mem_file_name(FLASH_FW_HEX),  // Memory file to preload
        .otp_file_name("none"),        // No OTP file
        .UserPreload(1)                // Enable file preload
    ) flash_model (
        .SI(qspi_d[0]),                // IO0 - MOSI (bidirectional in vendor model)
        .SO(qspi_d[1]),                // IO1 - MISO (bidirectional in vendor model)
        .SCK(qspi_clk),                // QSPI clock
        .CSNeg(qspi_cs_n),             // Chip select (active low)
        .RSTNeg(1'b1),                 // Reset (active low, tied high)
        .WPNeg(qspi_d[2]),             // IO2 - WP#
        .HOLDNeg(qspi_d[3])            // IO3 - HOLD#
    );
`else
    // Simplified flash model (Verilator compatible, no delays)
    s25fl_simple_rw #(
        .DEBUG(0),                     // Set to 1 to trace flash transactions
        .MEM_FILE(FLASH_FW_HEX),       // Hex file to load
        .MEM_FILE_OFFSET(FLASH_OFFSET), // Load at 0x100000 (1MB offset for boot stub @ 0x80100000)
        .MEM_FILE_IS_HEX(1)            // File is hex format
    ) flash_model (
        .SCK(qspi_clk),
        .CSNeg(qspi_cs_n),
        .SI(qspi_d[0]),                // IO0 - MOSI
        .SO(qspi_d[1]),                // IO1 - MISO
        .WPNeg(qspi_d[2]),             // IO2 - WP#
        .HOLDNeg(qspi_d[3])            // IO3 - HOLD#
    );
`endif

    //========================================================================
    // Test Control and Monitoring
    //========================================================================

    // Startup busy monitor
    logic flash_startup_busy;
    assign flash_startup_busy = dut.qspi_startup_busy;

    initial begin
        $display("\n========================================");
        $display("Servant ZVibe SoC XIP Testbench");
        $display("========================================\n");
        $display("[TB] Configuration:");
        $display("  CLK_FREQ:    %0d Hz", CLK_FREQ_HZ);
        $display("  RAM_SIZE:    %0d KB", MEM_SIZE/1024);
        $display("  FLASH_SIZE:  16 MB (24-bit address)");
        $display("  UART_BAUD:   %0d", UART_BAUD);
        $display("  BOOT_STUB:   %s", BOOT_STUB_HEX);
        $display("  FLASH_FW:    %s @ 0x%06h", FLASH_FW_HEX, FLASH_OFFSET);
        $display("");
        $display("[TB] Timeout Configuration:");
        if (MAX_CYCLES == 0) begin
            $display("  MAX_CYCLES:         Unlimited");
        end else begin
            $display("  MAX_CYCLES:         %0d (%.3f sec)", MAX_CYCLES, MAX_CYCLES / 100_000_000.0);
        end
        if (INACTIVITY_TIMEOUT == 0) begin
            $display("  INACTIVITY_TIMEOUT: Disabled");
        end else begin
            $display("  INACTIVITY_TIMEOUT: %0d (%.3f sec)", INACTIVITY_TIMEOUT, INACTIVITY_TIMEOUT / 100_000_000.0);
        end
        $display("  EXIT_SEQUENCE:      %s", ENABLE_EXIT_SEQUENCE ? "Enabled (##EXIT##)" : "Disabled");
        $display("");
    end

    // Monitor flash startup
    initial begin
        @(negedge flash_startup_busy);
        $display("[TB %t] Flash initialization complete (startup_busy cleared)", $time);
        $display("[TB %t]   Cycles for init: %0d", $time, cycle_count);
    end

    // Monitor first flash access
    logic        first_flash_access_seen = 0;
    integer      flash_access_count = 0;
    logic [31:0] last_flash_addr = 0;
    logic [31:0] prev_flash_req_addr = 0;
    logic [31:0] prev_flash_ack_data = 0;

    always_ff @(posedge clk) begin
        if (!rst && !first_flash_access_seen &&
            dut.wb_flash_stb && !dut.wb_flash_stall) begin
            first_flash_access_seen = 1;
            $display("[TB %t] First flash access detected", $time);
            $display("  Address: 0x%08h", dut.wb_flash_adr);
        end

        // Monitor flash request/response correlation
        if (!rst && dut.wb_flash_stb && !dut.wb_flash_stall) begin
            prev_flash_req_addr = dut.wb_flash_adr;
        end

        if (!rst && dut.wb_flash_ack) begin
            prev_flash_ack_data = dut.wb_flash_rdt;
            if (flash_access_count <= 10) begin
                $display("[TB %t] Flash ACK: wb_addr=0x%08h data=0x%08h",
                         $time, prev_flash_req_addr, dut.wb_flash_rdt);
            end
        end

        // Debug: Monitor QSPI state and address capture for first few accesses
        // NOTE: Disabled when using USE_FAST_RAM_FLASH=1 (generates don't have same hierarchy)
        /* FLASH-SPECIFIC DEBUG - DISABLED FOR RAM MODE
        if (!rst && flash_access_count <= 3) begin
            // Show transition from IDLE
            if (dut.gen_real_flash.qspi_flash.state == 4'd5 && dut.gen_real_flash.qspi_flash.next_state != 4'd5) begin
                $display("[TB %t] QSPI: IDLE→%0d cyc=%b stb=%b addr=0x%08h addr_reg=0x%06h quad_shift=0x%06h",
                         $time, dut.gen_real_flash.qspi_flash.next_state, dut.wb_flash_cyc, dut.wb_flash_stb,
                         dut.wb_flash_adr, dut.gen_real_flash.qspi_flash.addr_reg, dut.gen_real_flash.qspi_flash.quad_shift_reg);
            end
            // Show transition to IDLE
            if (dut.gen_real_flash.qspi_flash.state == 4'd11 && dut.gen_real_flash.qspi_flash.next_state == 4'd5) begin
                $display("[TB %t] QSPI: READ_DONE→IDLE ack=%b data=0x%08h",
                         $time, dut.wb_flash_ack, dut.wb_flash_rdt);
            end
            // Show when in IDLE state
            if (dut.gen_real_flash.qspi_flash.state == 4'd5 && dut.wb_flash_cyc && dut.wb_flash_stb &&
                dut.wb_flash_adr != prev_flash_req_addr) begin
                $display("[TB %t] QSPI: IN_IDLE with NEW request addr=0x%08h (addr_reg will be 0x%06h next cycle)",
                         $time, dut.wb_flash_adr, dut.wb_flash_adr[23:0]);
            end
        end
        */

        // Count flash accesses and show first 20 and every 1000th
        if (!rst && dut.wb_flash_stb && !dut.wb_flash_stall) begin
            flash_access_count = flash_access_count + 1;
            last_flash_addr = dut.wb_flash_adr;
            if (flash_access_count <= 20 || flash_access_count % 1000 == 0) begin
                $display("[TB %t] Flash STB %0d: addr=0x%08h cyc=%b stall=%b",
                         $time, flash_access_count, dut.wb_flash_adr, dut.wb_flash_cyc, dut.wb_flash_stall);
            end
        end

        // Simplified monitoring - just show state transitions
        // (Removed verbose per-cycle monitoring to speed up simulation)
    end

    // Monitor CPU instruction bus (focus on uart_putc loop)
    integer      ibus_fetch_count = 0;
    logic [31:0] prev_ibus_addr = 0;
    integer      uart_loop_count = 0;
    integer      ext_access_count = 0;  // Declared early for IEEE Verilog compliance
    logic        uart_stall_reported = 0;

    always_ff @(posedge clk) begin
        // Monitor all fetches from uart_putc loop (0x8040002c-0x80400048)
        if (!rst && dut.cpu.wb_ibus_stb &&
            dut.cpu.wb_ibus_adr >= 32'h8040002c && dut.cpu.wb_ibus_adr <= 32'h80400048) begin
            if (uart_loop_count < 10) begin
                $display("[TB %t] UART_PUTC FETCH #%0d: addr=0x%08h stb=%b ack=%b rdt=0x%08h flash_cyc=%b flash_stb=%b flash_ack=%b flash_stall=%b",
                         $time, uart_loop_count, dut.cpu.wb_ibus_adr,
                         dut.cpu.wb_ibus_stb, dut.cpu.wb_ibus_ack, dut.cpu.wb_ibus_rdt,
                         dut.wb_flash_cyc, dut.wb_flash_stb, dut.wb_flash_ack, dut.wb_flash_stall);
            end
            // Report stall condition once
            if (!uart_stall_reported && uart_loop_count == 100 && !dut.cpu.wb_ibus_ack) begin
                $display("[TB %t] ⚠️  FLASH STALL DETECTED: CPU waiting for flash @ 0x%08h", $time, dut.cpu.wb_ibus_adr);
                $display("  Flash controller: cyc=%b stb=%b ack=%b stall=%b",
                         dut.wb_flash_cyc, dut.wb_flash_stb,
                         dut.wb_flash_ack, dut.wb_flash_stall);
                uart_stall_reported = 1;
            end
            uart_loop_count = uart_loop_count + 1;
        end

        // Monitor first 30 ibus fetches overall (increased to capture trap)
        if (!rst && dut.cpu.wb_ibus_stb && ibus_fetch_count < 30) begin
            if (dut.cpu.wb_ibus_adr != prev_ibus_addr) begin
                $display("[TB %t] IBUS FETCH #%0d: addr=0x%08h stb=%b ack=%b rdt=0x%08h",
                         $time, ibus_fetch_count, dut.cpu.wb_ibus_adr,
                         dut.cpu.wb_ibus_stb, dut.cpu.wb_ibus_ack, dut.cpu.wb_ibus_rdt);
                prev_ibus_addr = dut.cpu.wb_ibus_adr;
            end
        end
        if (!rst && dut.cpu.wb_ibus_stb) begin
            ibus_fetch_count = ibus_fetch_count + 1;
        end
    end

    // Monitor for trap handler entry (0x80400028 = _irq_handler)
    logic trap_handler_entered = 0;
    always_ff @(posedge clk) begin
        if (!rst && !trap_handler_entered && dut.cpu.wb_ibus_stb && dut.cpu.wb_ibus_adr == 32'h80400028) begin
            trap_handler_entered = 1;
            $display("\n[TB %t] ⚠️  TRAP/EXCEPTION DETECTED!", $time);
            $display("[TB] CPU jumped to _irq_handler @ 0x80400028");
            $display("[TB] Previous instruction fetch was @ 0x%08h", prev_ibus_addr);
            $display("[TB] Instruction bus fetch count: %0d", ibus_fetch_count);
            $display("[TB] Flash access count: %0d", flash_access_count);
            $display("[TB] External bus access count: %0d\n", ext_access_count);
        end
    end

    // Monitor EXT (peripheral) bus accesses - before address decode
    // Note: ext_access_count declared earlier for IEEE Verilog compliance
    always_ff @(posedge clk) begin
        if (!rst && dut.wb_ext_stb && (ext_access_count <= 30 || (dut.wb_ext_we && dut.wb_ext_adr == 32'h40000000))) begin
            ext_access_count = ext_access_count + 1;
            if (dut.wb_ext_we) begin
                $display("[TB %t] EXT WRITE #%0d: addr=0x%08h data=0x%08h",
                         $time, ext_access_count, dut.wb_ext_adr, dut.wb_ext_dat);
            end else begin
                $display("[TB %t] EXT READ #%0d: addr=0x%08h",
                         $time, ext_access_count, dut.wb_ext_adr);
            end
        end
    end

    // Monitor UART wishbone accesses - after address decode
    integer uart_access_count = 0;
    always_ff @(posedge clk) begin
        if (!rst && dut.wb_uart_stb) begin
            uart_access_count = uart_access_count + 1;
            if (dut.wb_uart_we) begin
                $display("[TB %t] UART WRITE #%0d: addr=0x%08h data=0x%08h",
                         $time, uart_access_count, dut.wb_uart_adr, dut.wb_uart_dat);
            end else begin
                $display("[TB %t] UART READ #%0d: addr=0x%08h",
                         $time, uart_access_count, dut.wb_uart_adr);
            end
        end
        if (!rst && dut.wb_uart_ack) begin
            if (!dut.wb_uart_we) begin
                $display("[TB %t] UART READ ACK: uart_rdt=0x%08h ext_rdt=0x%08h", $time, dut.wb_uart_rdt, dut.wb_ext_rdt);
            end
        end
    end

    // Monitor RAM accesses (first 20 to see bootup)
    integer ram_access_count = 0;
    always_ff @(posedge clk) begin
        if (!rst && dut.wb_ram_stb && ram_access_count < 20) begin
            ram_access_count = ram_access_count + 1;
            if (dut.wb_ram_we) begin
                $display("[TB %t] RAM WRITE #%0d: addr=0x%08h data=0x%08h",
                         $time, ram_access_count, dut.wb_ram_adr, dut.wb_ram_dat);
            end else begin
                $display("[TB %t] RAM READ #%0d: addr=0x%08h",
                         $time, ram_access_count, dut.wb_ram_adr);
            end
        end
    end

    //========================================================================
    // Test Stimulus
    //========================================================================

    initial begin
        // Dump waveforms
        $dumpfile("servant_zvibe_xip_tb.vcd");
        $dumpvars(0, servant_zvibe_xip_tb);

        // Open UART output file for writing
        // Note: When FAST_UART=1, the uart_wb_model handles file output
        if (UART_OUTPUT_FILE != "" && FAST_UART == 0) begin
            uart_output_fd = $fopen(UART_OUTPUT_FILE, "w");
            if (uart_output_fd == 0) begin
                $display("[TB] ERROR: Could not open UART output file: %s", UART_OUTPUT_FILE);
            end else begin
                $display("[TB] UART output will be written to: %s", UART_OUTPUT_FILE);
            end
        end else begin
            uart_output_fd = 0;
            if (FAST_UART == 1) begin
                $display("[TB] UART output file handled by uart_wb_model");
            end else begin
                $display("[TB] UART output file disabled");
            end
        end

        // Load UART input stimulus file
        if (UART_INPUT_FILE != "") begin
            uart_input_fd = $fopen(UART_INPUT_FILE, "r");
            if (uart_input_fd == 0) begin
                $display("[TB] WARNING: Could not open UART input file: %s", UART_INPUT_FILE);
                $display("[TB] Continuing without input stimulus");
            end else begin
                $display("[TB] Loading UART input stimulus from: %s", UART_INPUT_FILE);
                uart_tx_count = 0;
                while (!$feof(uart_input_fd) && uart_tx_count < 1024) begin
                    input_file_char = $fgetc(uart_input_fd);
                    if (input_file_char != -1) begin
                        uart_tx_buffer[uart_tx_count] = input_file_char[7:0];
                        uart_tx_count = uart_tx_count + 1;
                    end
                end
                $fclose(uart_input_fd);
                $display("[TB] Loaded %0d characters from input file", uart_tx_count);
            end
        end else begin
            $display("[TB] UART input file disabled");
        end

        // Flash firmware is loaded automatically by flash_model via MEM_FILE parameter
        $display("[TB %t] Flash firmware loaded from %s at offset 0x%06h", $time, FLASH_FW_HEX, FLASH_OFFSET);

        // Debug: Dump first 16 bytes from flash at address 0x100000
        $display("[TB] Flash memory dump at 0x100000:");
        $display("  [0x100000] = 0x%02h %02h %02h %02h",
                 flash_model.Mem[24'h100000], flash_model.Mem[24'h100001],
                 flash_model.Mem[24'h100002], flash_model.Mem[24'h100003]);
        $display("  [0x100004] = 0x%02h %02h %02h %02h",
                 flash_model.Mem[24'h100004], flash_model.Mem[24'h100005],
                 flash_model.Mem[24'h100006], flash_model.Mem[24'h100007]);
        $display("  Expected first word: 0x37 0x81 0x00 0x00 (lui sp, 0x8)");

        // Reset sequence
        rst = 1;
        repeat(10) @(posedge clk);
        $display("\n[TB %t] Releasing reset", $time);
        rst = 0;

        // Wait for some activity
        $display("[TB %t] Waiting for execution...\n", $time);

        // Test will run until:
        // 1. Timeout (MAX_CYCLES)
        // 2. Manual $finish from test program
        // 3. Expected UART output received
    end

    //========================================================================
    // Test Verification Tasks
    //========================================================================

    // Task: Wait for specific UART string
    task wait_for_uart_string;
        input [256*8-1:0] expected_str;  // Max 256 chars
        input integer max_chars;
        integer i, j, match_len;
        logic [7:0] exp_byte;
        logic match_found;
        begin
            match_len = max_chars;
            match_found = 0;

            $display("[TB %t] Waiting for UART string (max %0d chars): \"%s\"",
                     $time, match_len, expected_str);

            // Wait for enough characters received
            wait(uart_rx_count >= match_len);

            // Check if received string matches
            match_found = 1;
            for (i = 0; i < match_len; i = i + 1) begin
                exp_byte = expected_str[i*8 +: 8];
                if (uart_rx_buffer[i] !== exp_byte) begin
                    match_found = 0;
                end
            end

            if (match_found) begin
                $display("[TB %t] ✓ UART string match!", $time);
            end else begin
                $display("[TB %t] ✗ UART string mismatch!", $time);
                $display("  Expected: %s", expected_str);
                $write("  Received: ");
                for (i = 0; i < match_len; i = i + 1) begin
                    $write("%c", uart_rx_buffer[i]);
                end
                $display("");
            end
        end
    endtask

    // Task: Check UART output contains string
    task check_uart_contains;
        input [256*8-1:0] search_str;
        input integer search_len;
        integer i, j;
        logic [7:0] exp_byte;
        logic match_found;
        begin
            match_found = 0;

            // Search through received buffer
            for (i = 0; i <= uart_rx_count - search_len; i = i + 1) begin
                match_found = 1;
                for (j = 0; j < search_len; j = j + 1) begin
                    exp_byte = search_str[j*8 +: 8];
                    if (uart_rx_buffer[i+j] !== exp_byte) begin
                        match_found = 0;
                    end
                end
                if (match_found) begin
                    $display("[TB %t] ✓ Found string at position %0d", $time, i);
                    i = uart_rx_count;  // Break loop
                end
            end

            if (!match_found) begin
                $display("[TB %t] ✗ String not found in UART output", $time);
            end
        end
    endtask

    //========================================================================
    // Test Cases
    //========================================================================

    // TEST 1.1: Flash Initialization
    initial begin
        #1;  // Wait for initial block

        $display("\n========================================");
        $display("TEST 1.1: Flash Initialization");
        $display("========================================");

        // Wait for reset to be released
        @(negedge rst);

        // Monitor for WRR sequence
        $display("[TEST 1.1] Monitoring QSPI for WRR sequence...");

        // Wait for flash init to complete
        @(negedge flash_startup_busy);

        // Check flash QUAD mode enabled
        if (flash_model.Config_reg1[1] === 1'b1) begin
            $display("[TEST 1.1] ✓ PASS - Flash QUAD mode enabled");
        end else begin
            $display("[TEST 1.1] ✗ FAIL - Flash QUAD mode NOT enabled");
        end

        $display("[TEST 1.1] Initialization cycles: %0d", cycle_count);
        $display("");
    end

    // TEST 2.1: Boot Stub Execution
    initial begin
        /* verilator lint_off WAITCONST */
        wait(flash_startup_busy == 0);  // Always 0 when USE_FAST_RAM_FLASH=1
        /* verilator lint_on WAITCONST */

        $display("\n========================================");
        $display("TEST 2.1: Boot Stub Execution");
        $display("========================================");

        // Wait for first flash access (indicates jump to flash happened)
        wait(first_flash_access_seen);

        $display("[TEST 2.1] ✓ PASS - Boot stub executed, jumped to flash");
        $display("[TEST 2.1] Cycles to first flash access: %0d", cycle_count);
        $display("");
    end

    //========================================================================
    // Test Summary
    //========================================================================

    // Final summary (only reached if no other exit mechanism triggered)
    initial begin
        // This should not normally be reached - proper tests should:
        //   1. Use ##EXIT## sequence for graceful exit, OR
        //   2. Hit MAX_CYCLES timeout, OR
        //   3. Trigger inactivity timeout
        //
        // This is a fallback safety net
        if (MAX_CYCLES == 0) begin
            #3600_000_000_000;  // 1 hour max for unlimited runs
        end else begin
            #(MAX_CYCLES * 10);  // Wait for MAX_CYCLES (convert cycles to ns)
        end

        $display("\n========================================");
        $display("Test Summary (Fallback Exit)");
        $display("========================================");
        $display("NOTE: Test did not exit via ##EXIT## or timeout");
        $display("Total cycles: %0d (%.3f sec)", cycle_count, $time / 1_000_000_000.0);
        $display("Flash accesses: %0d", flash_access_count);
        $display("Last flash address: 0x%08h", last_flash_addr);
        $display("UART characters received: %0d", uart_rx_count);

        if (uart_rx_count > 0) begin
            $write("UART output: ");
            for (integer k = 0; k < uart_rx_count; k = k + 1) begin
                $write("%c", uart_rx_buffer[k]);
            end
            $display("");
        end

        // Close UART output file
        if (uart_output_fd != 0) begin
            $fclose(uart_output_fd);
            $display("UART output written to: %s", UART_OUTPUT_FILE);
        end

        $display("\nSimulation complete");
        $display("========================================\n");
        $finish;
    end


    //========================================================================
    // Debug: UART Address/Data Flow Monitoring
    //========================================================================

    integer uart_flow_count = 0;
    integer flash_cmp_count = 0;
    logic prev_uart_ack = 0;

    always_ff @(posedge clk) begin
        prev_uart_ack <= dut.wb_uart_ack;

        // Detailed UART transaction monitoring - show cycle-by-cycle for first few transactions
        if (!rst && (dut.wb_uart_stb || dut.wb_uart_ack) && uart_flow_count < 50) begin
            uart_flow_count = uart_flow_count + 1;
            $display("[TB %t] UART: cyc=%b stb=%b we=%b ack=%b adr=0x%08h rdt=0x%08h",
                     $time, dut.wb_uart_cyc, dut.wb_uart_stb, dut.wb_uart_we,
                     dut.wb_uart_ack, dut.wb_uart_adr, dut.wb_uart_rdt);
        end

        // Compare with flash transactions (working reference)
        if (!rst && (dut.wb_flash_stb || dut.wb_flash_ack) && flash_cmp_count < 50) begin
            flash_cmp_count = flash_cmp_count + 1;
            $display("[TB %t] FLASH: cyc=%b stb=%b we=%b ack=%b stall=%b adr=0x%08h rdt=0x%08h",
                     $time, dut.wb_flash_cyc, dut.wb_flash_stb, dut.wb_flash_we,
                     dut.wb_flash_ack, dut.wb_flash_stall, dut.wb_flash_adr, dut.wb_flash_rdt);
        end
    end

endmodule

`default_nettype wire
