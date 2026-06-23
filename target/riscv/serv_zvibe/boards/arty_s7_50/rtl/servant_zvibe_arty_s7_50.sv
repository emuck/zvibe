// Copyright (c) 2025 Martin R. Raumann
// SPDX-License-Identifier: BSD-3-Clause

/*
 * Servant ZVibe Top Level for Arty S7-50
 *
 * FPGA: XC7S50-CSGA324-1
 * Clock: 100MHz input → 166.66MHz system (via MMCM)
 * UART: USB-UART bridge (CH340) @ 115200 baud
 */


module servant_zvibe_arty_s7_50 (
    // 100MHz clock input
    input  logic clk_100mhz,

    // Reset button (BTN0 on G15)
    input  logic btn0,

    // UART (connected to USB-UART bridge)
    input  logic uart_rxd,
    output logic uart_txd,

    // QSPI Flash (qspi_clk handled by STARTUPE2, not a port)
    output logic       qspi_cs_n,
    inout  wire  [3:0] qspi_d,      // tri-state inout: keep as wire

    // LEDs for debugging
    output logic       heartbeat_led,  // LD2
    output logic [2:0] gpio_led        // LD3, LD4, LD5
);

    //========================================================================
    // MMCM Clock Generation
    //========================================================================
    // Generate system clock from 100MHz input
    // Current: 166.66MHz output (6.0ns period)
    logic sys_clk;      // Generated system clock
    logic mmcm_rst;     // Reset from MMCM (asserted until locked)

    servant_zvibe_arty_s7_50_clock_gen
        // Clock frequency: 100MHz × (10.0 / 6.0) = 166.66MHz
        // To change: adjust CLKOUT0_DIVIDE_F (must be multiple of 0.125)
        //   100MHz: DIV=10.0, 125MHz: DIV=8.0, 166.66MHz: DIV=6.0
        #(.CLKFBOUT_MULT_F(10.000),   // VCO multiplier (VCO = 1000MHz)
          .CLKOUT0_DIVIDE_F(6.000))   // Output divider (166.66MHz output)
    clock_gen (
        .i_clk(clk_100mhz),
        .i_rst(1'b0),                  // No external reset needed
        .o_clk(sys_clk),
        .o_rst(mmcm_rst)
    );

    //========================================================================
    // Power-On Reset and Reset Debounce
    //========================================================================
    // Power-on reset: Hold reset for 256 clocks after FPGA configuration
    // Note: POR runs on input clock to work before MMCM locks
    logic [7:0] por_counter  = 8'h00;
    logic       por_reset    = 1'b1;

    always_ff @(posedge clk_100mhz) begin
        if (por_counter != 8'hFF) begin
            por_counter <= por_counter + 1;
            por_reset   <= 1'b1;
        end else begin
            por_reset <= 1'b0;
        end
    end

    // Debounce BTN0 with 3 flip-flops in series
    // Note: Runs on input clock to work before MMCM locks
    logic [2:0] btn_debounce = 3'b0;

    always_ff @(posedge clk_100mhz) begin
        btn_debounce <= {btn_debounce[1:0], btn0};
    end

    // Synchronize POR reset to system clock domain
    logic por_reset_sync1, por_reset_sync2;

    always_ff @(posedge sys_clk) begin
        por_reset_sync1 <= por_reset;
        por_reset_sync2 <= por_reset_sync1;
    end

    // Synchronize button reset to system clock domain
    logic btn_reset_sync1, btn_reset_sync2;
    logic btn_reset;
    assign btn_reset = btn_debounce[2];

    always_ff @(posedge sys_clk) begin
        btn_reset_sync1 <= btn_reset;
        btn_reset_sync2 <= btn_reset_sync1;
    end

    // Combined reset: MMCM reset OR POR OR button press
    // All resets are synchronized to system clock domain
    logic reset;
    logic reset_n;
    assign reset   = mmcm_rst | por_reset_sync2 | btn_reset_sync2;  // Active-high reset
    assign reset_n = !reset;                                         // Active-low reset

    //========================================================================
    // QSPI Subsystem Signals
    //========================================================================
    logic qspi_clk;  // Muxed QSPI clock → STARTUPE2 → CCLK pin

    // External flash Wishbone bus (SoC → s25fl_xip)
    logic        wb_flash_cyc;
    logic        wb_flash_stb;
    logic [31:0] wb_flash_adr;
    logic [31:0] wb_flash_dat;
    logic [3:0]  wb_flash_sel;
    logic        wb_flash_we;
    logic [31:0] wb_flash_rdt;
    logic        wb_flash_ack;
    logic        wb_flash_stall;

    // Flash write command interface (SoC → s25fl_write)
    logic        flash_wr_cmd_wren;
    logic        flash_wr_cmd_se;
    logic        flash_wr_cmd_pp;
    logic        flash_wr_cmd_rdsr;
    logic [23:0] flash_wr_addr;
    logic [7:0]  flash_wr_data;
    logic        flash_write_busy;
    logic        flash_write_error;
    logic [7:0]  flash_write_status;
    logic        flash_write_active;

    // QSPI controller signals (XIP and Write → mux → pins)
    logic        xip_sck,  xip_cs_n;
    logic [3:0]  xip_dat_out, xip_dat_oe, xip_dat_in;
    logic        write_sck, write_cs_n;
    logic [3:0]  write_dat_out, write_dat_oe, write_dat_in;
    logic        qspi_sck_mux,  qspi_cs_n_mux;
    logic [3:0]  qspi_dat_out_mux, qspi_dat_oe_mux, qspi_dat_in_mux;

    // Cache to XIP burst interface (128-bit, BURST_WORDS=4)
    logic        cache_xip_cyc;
    logic        cache_xip_stb;
    logic [31:0] cache_xip_addr;
    logic [127:0] cache_xip_data;
    logic        cache_xip_ack;
    logic        cache_xip_stall;

    // XIP idle: XIP's downstream WB input is not active and CS# is deasserted.
    // Use cache_xip_cyc (not wb_flash_cyc) so the write controller can grab
    // the bus while the cache is filling BRAM after a burst completes.
    logic xip_idle;
    assign xip_idle = !cache_xip_cyc && xip_cs_n;

    //========================================================================
    // GPIO LED signals from SoC
    //========================================================================
    logic [2:0] soc_gpio_led;  // GPIO LEDs from SoC (LD3, LD4, LD5)

    //========================================================================
    // Servant ZVibe SoC Instance
    //========================================================================
    // QSPI managed by board wrapper (USE_EXTERNAL_FLASH_WB=1).
    // UART_PRESCALE: sys_clk_freq / (115200 * 8)
    //   166.66MHz: 166666667 / 921600 ≈ 181
    servant_zvibe #(
        .memfile(""),                      // No preload — XIP direct boot
        .reset_pc(32'h80100100),           // Boot directly into XIP firmware
        .memsize(32*1024),                 // 32KB RAM
        .reset_strategy("MINI"),
        .with_csr(0),                      // Minimal RV32I
        .UART_PRESCALE(181),               // 166.66MHz / (115200 * 8) ≈ 181
        .UART_FIFO_DEPTH(64),
        .FLASH_SIZE(24),                   // 24 bits = 16MB flash
        .USE_FAST_RAM_FLASH(0),
        .USE_EXTERNAL_FLASH_WB(1)          // QSPI controllers in this wrapper
    ) soc (
        .wb_clk(sys_clk),
        .wb_rst(reset),
        .uart_rxd(uart_rxd),
        .uart_txd(uart_txd),
        .uart_rx_valid(1'b0),
        .uart_rx_char(8'h00),
        .qspi_clk(),                       // Unused - QSPI managed in wrapper
        .qspi_cs_n(),
        .qspi_d(),

        // External flash Wishbone bus
        .o_wb_flash_cyc(wb_flash_cyc),
        .o_wb_flash_stb(wb_flash_stb),
        .o_wb_flash_adr(wb_flash_adr),
        .o_wb_flash_dat(wb_flash_dat),
        .o_wb_flash_sel(wb_flash_sel),
        .o_wb_flash_we(wb_flash_we),
        .i_wb_flash_rdt(wb_flash_rdt),
        .i_wb_flash_ack(wb_flash_ack),
        .i_wb_flash_stall(wb_flash_stall),

        // Flash write command interface
        .o_flash_wr_cmd_wren(flash_wr_cmd_wren),
        .o_flash_wr_cmd_se(flash_wr_cmd_se),
        .o_flash_wr_cmd_pp(flash_wr_cmd_pp),
        .o_flash_wr_cmd_rdsr(flash_wr_cmd_rdsr),
        .o_flash_wr_addr(flash_wr_addr),
        .o_flash_wr_data(flash_wr_data),
        .i_flash_wr_busy(flash_write_busy),
        .i_flash_wr_error(flash_write_error),

        // UFM Wishbone bus — not present on Arty (MAX10-only peripheral)
        // Tie inputs to safe idle values; outputs left open (not used on Arty)
        .i_wb_ufm_rdt(32'h0),
        .i_wb_ufm_ack(1'b0),
        .o_wb_ufm_cyc(),
        .o_wb_ufm_stb(),
        .o_wb_ufm_adr(),
        .o_wb_ufm_dat(),
        .o_wb_ufm_sel(),
        .o_wb_ufm_we(),

        .gpio_led(soc_gpio_led)
    );

    //========================================================================
    // QSPI Subsystem - Cache + XIP Controller + Write Controller + Mux
    //========================================================================

    // QSPI XIP Cache (4KB direct-mapped BRAM, 256 lines × 16 bytes)
    qspi_cache_bram #(
        .NUM_LINES(256)
    ) flash_cache (
        .i_clk     (sys_clk),
        .i_reset   (reset),

        // Upstream: SoC 32-bit WB read bus
        .i_wb_cyc  (wb_flash_cyc),
        .i_wb_stb  (wb_flash_stb),
        .i_wb_addr (wb_flash_adr),
        .o_wb_data (wb_flash_rdt),
        .o_wb_ack  (wb_flash_ack),
        .o_wb_stall(wb_flash_stall),

        // Downstream: 128-bit burst to s25fl_xip
        .o_xip_cyc  (cache_xip_cyc),
        .o_xip_stb  (cache_xip_stb),
        .o_xip_addr (cache_xip_addr),
        .i_xip_data (cache_xip_data),
        .i_xip_ack  (cache_xip_ack),
        .i_xip_stall(cache_xip_stall)
    );

    // XIP Flash Controller (BURST_WORDS=4 for cache line fills)
    s25fl_xip #(
        .CLK_DIV(2),                           // 166.66MHz / 12 = 13.9MHz QSPI SCK (matches sim)
        .FLASH_ADDR_WIDTH(24),                 // 16MB flash
        .WRR_WAIT_CYCLES(16'd65535),
        .BURST_WORDS(4)                        // 4-word (16-byte) burst for cache fills
    ) qspi_flash (
        .i_clk(sys_clk),
        .i_reset(reset),

        .i_wb_cyc  (cache_xip_cyc),
        .i_wb_stb  (cache_xip_stb),
        .i_wb_addr (cache_xip_addr),
        .o_wb_data (cache_xip_data),
        .o_wb_ack  (cache_xip_ack),
        .o_wb_stall(cache_xip_stall),

        .o_qspi_sck(xip_sck),
        .o_qspi_cs_n(xip_cs_n),
        .o_qspi_dat(xip_dat_out),
        .o_qspi_oe(xip_dat_oe),
        .i_qspi_dat(xip_dat_in),

        .o_startup_busy(),
        .o_state()
    );

    // Flash Write Controller
    s25fl_write #(
        .CLK_DIV(2)
    ) flash_write (
        .i_clk(sys_clk),
        .i_reset(reset),

        .i_cmd_wren(flash_wr_cmd_wren),
        .i_cmd_se(flash_wr_cmd_se),
        .i_cmd_pp(flash_wr_cmd_pp),
        .i_cmd_rdsr(flash_wr_cmd_rdsr),
        .i_address(flash_wr_addr),
        .i_data(flash_wr_data),
        .i_data_valid(1'b1),

        .o_busy(flash_write_busy),
        .o_error(flash_write_error),
        .o_status(flash_write_status),
        .o_data_out(),

        .o_qspi_sck(write_sck),
        .o_qspi_cs_n(write_cs_n),
        .o_qspi_dat(write_dat_out),
        .o_qspi_oe(write_dat_oe),
        .i_qspi_dat(write_dat_in),

        .o_active(flash_write_active),
        .i_xip_idle(xip_idle)
    );

    // QSPI Mux - arbitrates between XIP and Write controllers
    qspi_mux qspi_mux_inst (
        .i_write_active(flash_write_active),

        .i_xip_sck(xip_sck),
        .i_xip_cs_n(xip_cs_n),
        .i_xip_dat(xip_dat_out),
        .i_xip_oe(xip_dat_oe),

        .i_write_sck(write_sck),
        .i_write_cs_n(write_cs_n),
        .i_write_dat(write_dat_out),
        .i_write_oe(write_dat_oe),

        .o_qspi_sck(qspi_sck_mux),
        .o_qspi_cs_n(qspi_cs_n_mux),
        .o_qspi_dat(qspi_dat_out_mux),
        .o_qspi_oe(qspi_dat_oe_mux),
        .i_qspi_dat(qspi_dat_in_mux),

        .o_xip_dat_in(xip_dat_in),
        .o_write_dat_in(write_dat_in)
    );

    // Connect muxed QSPI signals to pins
    assign qspi_clk  = qspi_sck_mux;
    assign qspi_cs_n = qspi_cs_n_mux;

    // Bidirectional data buffers
    assign qspi_d[0] = qspi_dat_oe_mux[0] ? qspi_dat_out_mux[0] : 1'bz;
    assign qspi_d[1] = qspi_dat_oe_mux[1] ? qspi_dat_out_mux[1] : 1'bz;
    assign qspi_d[2] = qspi_dat_oe_mux[2] ? qspi_dat_out_mux[2] : 1'bz;
    assign qspi_d[3] = qspi_dat_oe_mux[3] ? qspi_dat_out_mux[3] : 1'bz;
    assign qspi_dat_in_mux = qspi_d;

    //========================================================================
    // Heartbeat LED - blinks when CPU is running
    //========================================================================
    logic [25:0] heartbeat_counter = '0;

    always_ff @(posedge sys_clk) begin
        if (reset)
            heartbeat_counter <= '0;
        else
            heartbeat_counter <= heartbeat_counter + 1;
    end

    assign heartbeat_led = heartbeat_counter[25];  // Blinks at ~2.5Hz (at 166.66MHz)

    //========================================================================
    // QSPI Clock Activity Monitor - shows STARTUPE2 is working
    //========================================================================
    // Count qspi_clk edges to verify STARTUPE2 is driving the physical pin
    // LD5 will toggle if qspi_clk is active
    logic [15:0] qspi_activity_counter = '0;
    logic        qspi_clk_sync1        = 1'b0;
    logic        qspi_clk_sync2        = 1'b0;
    logic        qspi_clk_prev         = 1'b0;

    always_ff @(posedge sys_clk) begin
        // Synchronize qspi_clk to system clock domain
        qspi_clk_sync1 <= qspi_clk;
        qspi_clk_sync2 <= qspi_clk_sync1;
        qspi_clk_prev  <= qspi_clk_sync2;

        // Count edges on qspi_clk
        if (qspi_clk_sync2 != qspi_clk_prev)
            qspi_activity_counter <= qspi_activity_counter + 1;
    end

    // LD5 shows qspi_clk activity (toggles at visible rate if clock is active)
    // LD3, LD4 come from SoC GPIO
    assign gpio_led[1:0] = soc_gpio_led[1:0];          // LD3 (boot), LD4 (XIP)
    assign gpio_led[2]   = qspi_activity_counter[15];   // LD5: QSPI clock activity

    //========================================================================
    // STARTUPE2 - Connect QSPI clock to configuration flash CCLK pin
    //========================================================================
    // The Arty S7-50 QSPI flash is the configuration flash, so we need
    // to use STARTUPE2 to drive its clock (CCLK) instead of a regular I/O
    STARTUPE2 #(
        .PROG_USR("FALSE"),
        .SIM_CCLK_FREQ(0.0)
    ) startupe2_inst (
        .CFGCLK(),              // 1-bit output: Config clock output
        .CFGMCLK(),             // 1-bit output: Config internal oscillator clock
        .EOS(),                 // 1-bit output: Active high when FPGA done
        .PREQ(),                // 1-bit output: PROGRAM request to fabric
        .CLK(1'b0),             // 1-bit input: User clock input
        .GSR(1'b0),             // 1-bit input: Global Set/Reset (GSR)
        .GTS(1'b0),             // 1-bit input: Global 3-state (GTS)
        .KEYCLEARB(1'b1),       // 1-bit input: Clear AES key
        .PACK(1'b0),            // 1-bit input: PROGRAM acknowledge
        .USRCCLKO(qspi_clk),    // 1-bit input: User CCLK - connect our QSPI clock here
        .USRCCLKTS(1'b0),       // 1-bit input: User CCLK tristate (0 = drive, 1 = tristate)
        .USRDONEO(1'b1),        // 1-bit input: User DONE output (keep high)
        .USRDONETS(1'b1)        // 1-bit input: User DONE tristate (1 = let config logic control)
    );

endmodule

