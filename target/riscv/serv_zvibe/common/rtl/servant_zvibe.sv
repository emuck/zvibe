/*
 * Servant ZVibe - SERV-based SoC with Hardware UART
 *
 * Based on the Servant SoC by Olof Kindgren (ISC License)
 * See: https://github.com/olofk/serv
 *
 * Modifications Copyright (c) 2025 Martin R. Raumann
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Modified to use uart_wb instead of bit-banged GPIO UART
 *
 * Features:
 * - SERV RV32I bit-serial CPU
 * - Configurable RAM size
 * - Hardware UART with FIFOs (115200 baud @ 100MHz)
 * - Timer peripheral
 */

module servant_zvibe #(
    parameter memfile = "",
    parameter memsize = 64*1024,       // 64KB (increased for larger games)
    parameter reset_strategy = "MINI",
    parameter reset_pc = 32'h00000000, // Boot address (0x00000000=RAM, 0x80001000=XIP)
    parameter with_csr = 0,            // Minimal RV32I, no CSRs

    // UART parameters
    parameter UART_PRESCALE = 108,     // 100MHz / (115200 * 8) ≈ 108
    parameter UART_FIFO_DEPTH = 64,
    parameter FAST_UART = 0,           // 0=real UART, 1=fast Wishbone model (sim only)

    // QSPI Flash parameters
    parameter FLASH_SIZE = 24,          // 24 bits = 16MB flash
    parameter USE_FAST_RAM_FLASH = 0,   // 0=real flash, 1=fast RAM mimic (test only)
    parameter USE_EXTERNAL_FLASH_WB = 0,// 0=internal controller, 1=external wishbone (MAX10)
    parameter USE_CACHE = 0             // 0=direct XIP, 1=4KB BRAM cache + BURST_WORDS=4 XIP
) (
    input logic  wb_clk,
    input logic  wb_rst,

    // UART pins
    input logic  uart_rxd,
    output logic uart_txd,

    // UART RX stimulus for FAST_UART model (testbench only)
    input logic        uart_rx_valid,
    input logic [7:0]  uart_rx_char,

    // QSPI Flash pins (not used when USE_EXTERNAL_FLASH_WB=1)
    output logic        qspi_clk,
    output logic        qspi_cs_n,
    inout wire [3:0]   qspi_d,

    // External Flash Wishbone interface (used when USE_EXTERNAL_FLASH_WB=1)
    output logic        o_wb_flash_cyc,
    output logic        o_wb_flash_stb,
    output logic [31:0] o_wb_flash_adr,
    output logic [31:0] o_wb_flash_dat,
    output logic [3:0]  o_wb_flash_sel,
    output logic        o_wb_flash_we,
    input logic [31:0]  i_wb_flash_rdt,
    input logic         i_wb_flash_ack,
    input logic         i_wb_flash_stall,

    // Flash write command interface (used when USE_EXTERNAL_FLASH_WB=1)
    output logic        o_flash_wr_cmd_wren,
    output logic        o_flash_wr_cmd_se,
    output logic        o_flash_wr_cmd_pp,
    output logic        o_flash_wr_cmd_rdsr,
    output logic [23:0] o_flash_wr_addr,
    output logic [7:0]  o_flash_wr_data,
    input logic         i_flash_wr_busy,
    input logic         i_flash_wr_error,

    // UFM Write Wishbone interface (for MAX10 UFM erase/program operations)
    output logic        o_wb_ufm_cyc,
    output logic        o_wb_ufm_stb,
    output logic [31:0] o_wb_ufm_adr,
    output logic [31:0] o_wb_ufm_dat,
    output logic [3:0]  o_wb_ufm_sel,
    output logic        o_wb_ufm_we,
    input logic [31:0]  i_wb_ufm_rdt,
    input logic         i_wb_ufm_ack,

    // GPIO debug LEDs
    output logic [2:0]  gpio_led
);

    localparam aw = $clog2(memsize);
    localparam csr_regs = with_csr * 4;
    localparam width = 1;  // Bit-serial
    localparam rf_width = width * 2;
    localparam rf_l2d = $clog2((32 + csr_regs) * 32 / rf_width);

    logic timer_irq;

    //========================================================================
    // CPU Memory Interface (Instruction + Data)
    //========================================================================
    logic [31:0] wb_cpu_mem_adr;
    logic [31:0] wb_cpu_mem_dat;
    logic [3:0]  wb_cpu_mem_sel;
    logic        wb_cpu_mem_we;
    logic        wb_cpu_mem_stb;
    logic [31:0] wb_cpu_mem_rdt;
    logic        wb_cpu_mem_ack;

    // RAM-specific signals (after mem mux)
    logic [31:0] wb_ram_adr;
    logic [31:0] wb_ram_dat;
    logic [3:0]  wb_ram_sel;
    logic        wb_ram_we;
    logic        wb_ram_stb;
    logic [31:0] wb_ram_rdt;
    logic        wb_ram_ack;

    //========================================================================
    // CPU Extension Interface (Peripherals)
    //========================================================================
    logic [31:0] wb_ext_adr;
    logic [31:0] wb_ext_dat;
    logic [3:0]  wb_ext_sel;
    logic        wb_ext_we;
    logic        wb_ext_stb;
    logic [31:0] wb_ext_rdt;
    logic        wb_ext_ack;

    //========================================================================
    // Peripheral Wishbone Buses
    //========================================================================

    // UART (replaces GPIO in standard Servant)
    logic [31:0] wb_uart_adr;
    logic [31:0] wb_uart_dat;
    logic        wb_uart_we;
    logic        wb_uart_cyc;
    logic        wb_uart_stb;
    logic [31:0] wb_uart_rdt;
    logic        wb_uart_ack;

    // Timer
    logic [31:0] wb_timer_dat;
    logic        wb_timer_we;
    logic        wb_timer_stb;
    logic [31:0] wb_timer_rdt;

    // GPIO LEDs
    logic [31:0] wb_gpio_adr;
    logic [31:0] wb_gpio_dat;
    logic        wb_gpio_we;
    logic        wb_gpio_cyc;
    logic        wb_gpio_stb;
    logic [31:0] wb_gpio_rdt;
    logic        wb_gpio_ack;

    // Flash Status Interface (on EXT interface via peripheral mux)
    logic        wb_flash_status_cyc;
    logic        wb_flash_status_stb;
    logic [31:0] wb_flash_status_adr;
    logic        wb_flash_status_ack;
    logic [31:0] wb_flash_status_rdt;

    // QSPI Flash (on MEM interface, not EXT)
    logic [31:0] wb_flash_adr;
    logic [31:0] wb_flash_dat;
    logic [3:0]  wb_flash_sel;
    logic        wb_flash_we;
    logic        wb_flash_cyc;
    logic        wb_flash_stb;
    logic [31:0] wb_flash_rdt;
    logic        wb_flash_ack;
    logic        wb_flash_stall;

    // UFM Write (on MEM interface for 0x82xxxxxx)
    logic [31:0] wb_ufm_adr;
    logic [31:0] wb_ufm_dat;
    logic [3:0]  wb_ufm_sel;
    logic        wb_ufm_we;
    logic        wb_ufm_cyc;
    logic        wb_ufm_stb;
    logic [31:0] wb_ufm_rdt;
    logic        wb_ufm_ack;

    // Flash write register access detection (0x81xxxxxx)
    logic flash_write_reg_access;
    assign flash_write_reg_access = (wb_cpu_mem_adr[31:24] == 8'h81);

    // Intermediate signals to mem_mux (gated for 0x81 flash write registers)
    logic        wb_mem_mux_stb;
    logic [31:0] wb_mem_mux_rdt;
    logic        wb_mem_mux_ack;

    // Flash write controller interface signals
    logic        flash_write_busy;
    logic        flash_write_error;
    logic [7:0]  flash_write_status;
    logic        flash_write_active;

    // Flash write register response signals (declared here, driven in always block below)
    logic        flash_write_ack;
    logic [31:0] flash_write_rdt;

    // QSPI mux signals (XIP and Write controllers)
    logic        xip_sck;
    logic        xip_cs_n;
    logic [3:0]  xip_dat_out;
    logic [3:0]  xip_dat_oe;
    logic [3:0]  xip_dat_in;

    logic        write_sck;
    logic        write_cs_n;
    logic [3:0]  write_dat_out;
    logic [3:0]  write_dat_oe;
    logic [3:0]  write_dat_in;

    // Muxed QSPI signals (to pins)
    logic        qspi_sck_mux;
    logic        qspi_cs_n_mux;
    logic [3:0]  qspi_dat_out_mux;
    logic [3:0]  qspi_dat_oe_mux;
    logic [3:0]  qspi_dat_in_mux;

    //========================================================================
    // Register File Interface
    //========================================================================
    logic [rf_l2d-1:0]   rf_waddr;
    logic [rf_width-1:0] rf_wdata;
    logic                rf_wen;
    logic [rf_l2d-1:0]   rf_raddr;
    logic                rf_ren;
    logic [rf_width-1:0] rf_rdata;

    //========================================================================
    // Memory Mux (RAM + Flash on MEM interface)
    // Note: Strobe is gated to exclude 0x81xxxxxx (flash write registers)
    //========================================================================
    // Gate strobe to mem_mux - don't send 0x81 addresses there
    assign wb_mem_mux_stb = wb_cpu_mem_stb && !flash_write_reg_access;

    servant_mem_mux mem_mux (
        .i_wb_cpu_adr(wb_cpu_mem_adr),
        .i_wb_cpu_dat(wb_cpu_mem_dat),
        .i_wb_cpu_sel(wb_cpu_mem_sel),
        .i_wb_cpu_we(wb_cpu_mem_we),
        .i_wb_cpu_stb(wb_mem_mux_stb),  // Gated strobe - excludes 0x81
        .o_wb_cpu_rdt(wb_mem_mux_rdt),  // Response muxed below
        .o_wb_cpu_ack(wb_mem_mux_ack),  // ACK muxed below

        .o_wb_ram_adr(wb_ram_adr),
        .o_wb_ram_dat(wb_ram_dat),
        .o_wb_ram_sel(wb_ram_sel),
        .o_wb_ram_we(wb_ram_we),
        .o_wb_ram_stb(wb_ram_stb),
        .i_wb_ram_rdt(wb_ram_rdt),
        .i_wb_ram_ack(wb_ram_ack),

        .o_wb_flash_adr(wb_flash_adr),
        .o_wb_flash_dat(wb_flash_dat),
        .o_wb_flash_sel(wb_flash_sel),
        .o_wb_flash_we(wb_flash_we),
        .o_wb_flash_cyc(wb_flash_cyc),
        .o_wb_flash_stb(wb_flash_stb),
        .i_wb_flash_rdt(wb_flash_rdt),
        .i_wb_flash_ack(wb_flash_ack),
        .i_wb_flash_stall(wb_flash_stall),

        .o_wb_ufm_adr(wb_ufm_adr),
        .o_wb_ufm_dat(wb_ufm_dat),
        .o_wb_ufm_sel(wb_ufm_sel),
        .o_wb_ufm_we(wb_ufm_we),
        .o_wb_ufm_cyc(wb_ufm_cyc),
        .o_wb_ufm_stb(wb_ufm_stb),
        .i_wb_ufm_rdt(wb_ufm_rdt),
        .i_wb_ufm_ack(wb_ufm_ack),

        // Debug outputs (unused in synthesis; available for probing in simulation)
        .o_debug_sel_ufm(),
        .o_debug_sel_flash(),
        .o_debug_cpu_stb()
    );

    // Mux response back to CPU - select between mem_mux and flash write registers
    assign wb_cpu_mem_rdt = flash_write_reg_access ? flash_write_rdt : wb_mem_mux_rdt;
    assign wb_cpu_mem_ack = flash_write_reg_access ? flash_write_ack : wb_mem_mux_ack;

    //========================================================================
    // Peripheral Mux (UART + Timer + GPIO on EXT interface)
    //========================================================================
    servant_zvibe_mux mux (
        .i_wb_cpu_adr(wb_ext_adr),
        .i_wb_cpu_dat(wb_ext_dat),
        .i_wb_cpu_we(wb_ext_we),
        .i_wb_cpu_cyc(wb_ext_stb),
        .o_wb_cpu_rdt(wb_ext_rdt),
        .o_wb_cpu_ack(wb_ext_ack),

        .o_wb_uart_adr(wb_uart_adr),
        .o_wb_uart_dat(wb_uart_dat),
        .o_wb_uart_we(wb_uart_we),
        .o_wb_uart_cyc(wb_uart_cyc),
        .i_wb_uart_rdt(wb_uart_rdt),
        .i_wb_uart_ack(wb_uart_ack),

        .o_wb_timer_dat(wb_timer_dat),
        .o_wb_timer_we(wb_timer_we),
        .o_wb_timer_cyc(wb_timer_stb),
        .i_wb_timer_rdt(wb_timer_rdt),

        .o_wb_gpio_adr(wb_gpio_adr),
        .o_wb_gpio_dat(wb_gpio_dat),
        .o_wb_gpio_we(wb_gpio_we),
        .o_wb_gpio_cyc(wb_gpio_cyc),
        .i_wb_gpio_rdt(wb_gpio_rdt),
        .i_wb_gpio_ack(wb_gpio_ack),

        .o_wb_flash_status_cyc(wb_flash_status_cyc),
        .i_wb_flash_status_rdt(wb_flash_status_rdt),
        .i_wb_flash_status_ack(wb_flash_status_ack)
    );

    // QSPI status signals (used in flash status register, declared early for IEEE Verilog compliance)
    logic       qspi_startup_busy;     // Flash initialization in progress
    logic [4:0] qspi_state;            // Current FSM state (5 bits)

    // UART cyc and stb are the same (stb derived from cyc in mux)
    assign wb_uart_stb = wb_uart_cyc;

    // GPIO cyc and stb are the same
    assign wb_gpio_stb = wb_gpio_cyc;

    // Flash status cyc and stb are the same
    assign wb_flash_status_stb = wb_flash_status_cyc;

    //========================================================================
    // RAM (via Memory Mux)
    //========================================================================
    servant_ram #(
        .memfile(memfile),
        .depth(memsize),
        .RESET_STRATEGY(reset_strategy)
    ) ram (
        .i_wb_clk(wb_clk),
        .i_wb_rst(wb_rst),
        .i_wb_adr(wb_ram_adr[31:2]),  // Pass bits [31:2], port uses lower [aw-1:2]
        .i_wb_cyc(wb_ram_stb),
        .i_wb_we(wb_ram_we),
        .i_wb_sel(wb_ram_sel),
        .i_wb_dat(wb_ram_dat),
        .o_wb_rdt(wb_ram_rdt),
        .o_wb_ack(wb_ram_ack)
    );

    //========================================================================
    // Timer
    //========================================================================
    servant_timer #(
        .RESET_STRATEGY(reset_strategy),
        .WIDTH(32)
    ) timer (
        .i_clk(wb_clk),
        .i_rst(wb_rst),
        .o_irq(timer_irq),
        .i_wb_cyc(wb_timer_stb),
        .i_wb_we(wb_timer_we),
        .i_wb_dat(wb_timer_dat),
        .o_wb_dat(wb_timer_rdt)
    );

    //========================================================================
    // Hardware UART (replaces servant_gpio)
    //========================================================================
    generate
        if (FAST_UART == 0) begin : gen_real_uart
            // Real UART with TX/RX serialization (accurate, slow in simulation)
            initial $display("[SERVANT_ZVIBE] Using REAL UART (bit-level serialization)");
            uart_wb #(
                .PRESCALE(UART_PRESCALE),
                .FIFO_DEPTH(UART_FIFO_DEPTH)
            ) uart (
                .i_wb_clk(wb_clk),
                .i_wb_rst(wb_rst),
                .i_wb_adr(wb_uart_adr),
                .i_wb_dat(wb_uart_dat),
                .o_wb_dat(wb_uart_rdt),
                .i_wb_we(wb_uart_we),
                .i_wb_cyc(wb_uart_cyc),
                .i_wb_stb(wb_uart_stb),
                .o_wb_ack(wb_uart_ack),
                .uart_txd(uart_txd),
                .uart_rxd(uart_rxd)
            );
        end else begin : gen_fast_uart
            // Fast Wishbone model (bypasses serialization, simulation only)
            initial $display("[SERVANT_ZVIBE] Using FAST UART MODEL (instant Wishbone writes)");
            uart_wb_model #(
                .OUTPUT_FILE("uart_output.txt")
            ) uart (
                .i_clk(wb_clk),
                .i_reset(wb_rst),
                .i_wb_cyc(wb_uart_cyc),
                .i_wb_stb(wb_uart_stb),
                .i_wb_we(wb_uart_we),
                .i_wb_addr({24'h0, wb_uart_adr}),
                .i_wb_data(wb_uart_dat),
                .o_wb_data(wb_uart_rdt),
                .o_wb_ack(wb_uart_ack),
                .rx_valid(uart_rx_valid),
                .rx_char(uart_rx_char)
            );

            // Tie off UART pins for fast model
            assign uart_txd = 1'b1;  // Idle high
        end
    endgenerate

    //========================================================================
    // GPIO Debug LEDs
    //========================================================================
    logic [2:0] gpio_led_sw;  // Software-controlled LEDs

    gpio_leds gpio (
        .i_wb_clk(wb_clk),
        .i_wb_rst(wb_rst),
        .i_wb_adr(wb_gpio_adr),
        .i_wb_dat(wb_gpio_dat),
        .i_wb_we(wb_gpio_we),
        .i_wb_cyc(wb_gpio_cyc),
        .i_wb_stb(wb_gpio_stb),
        .o_wb_ack(wb_gpio_ack),
        .o_wb_dat(wb_gpio_rdt),
        .o_gpio_led(gpio_led_sw)
    );

    // All LEDs are software-controlled via GPIO register
    assign gpio_led = gpio_led_sw;

    //========================================================================
    // Flash Status Register (Read-only)
    // Address: 0x40000030
    //
    // Bit Layout:
    //   Bit 0:   startup_busy     - Flash initialization in progress (1=busy, 0=ready)
    //   Bit 1:   (reserved, always 0)
    //   Bits 5:2: state[3:0]      - Current FSM state (see s25fl_xip.v for encoding)
    //   Bit 6:   wb_stall         - Controller is stalling/busy with operation
    //   Bit 7:   (reserved)
    //   Bits 15:8: fpga_version   - FPGA build version number
    //   Bits 31:16: (reserved/zero)
    //========================================================================
    // Note: Wire declarations moved earlier (before assign statements)

    // FPGA version - increment on each build
    localparam [7:0] FPGA_VERSION = 8'd15;  // v15: Flash write controller integration with QSPI mux

    // Simple read-only register
    logic flash_status_ack;
    always_ff @(posedge wb_clk) begin
        if (wb_rst)
            flash_status_ack <= 1'b0;
        else
            flash_status_ack <= wb_flash_status_cyc && wb_flash_status_stb && !flash_status_ack;
    end

    assign wb_flash_status_ack = flash_status_ack;
    assign wb_flash_status_rdt = {16'b0,              // Bits 31:16 reserved
                                   FPGA_VERSION,      // Bits 15:8: version number
                                   wb_flash_stall,    // Bit 7: stall signal
                                   qspi_state[4:0],   // Bits 6:2: FSM state (5 bits)
                                   1'b0,              // Bit 1: reserved
                                   qspi_startup_busy};    // Bit 0: startup busy

    //========================================================================
    // Flash Write Controller Registers (0x81000000-0x8100000F)
    //========================================================================

    // Detect flash write register access (address 0x81xxxxxx)
    logic flash_write_access;
    assign flash_write_access = wb_cpu_mem_stb && (wb_cpu_mem_adr[31:24] == 8'h81);
    logic [3:0] flash_write_reg;
    assign flash_write_reg = wb_cpu_mem_adr[3:0];       // Register offset

    // Flash write interface registers
    logic [23:0] flash_write_addr_reg;
    logic [7:0]  flash_write_data_reg;
    logic        cmd_wren_strobe;
    logic        cmd_se_strobe;
    logic        cmd_pp_strobe;
    logic        cmd_rdsr_strobe;

    // Wishbone response for flash write registers (declared earlier with other signals)

    always_ff @(posedge wb_clk) begin
        if (wb_rst) begin
            flash_write_addr_reg <= 24'h000000;
            flash_write_data_reg <= 8'h00;
            cmd_wren_strobe <= 1'b0;
            cmd_se_strobe <= 1'b0;
            cmd_pp_strobe <= 1'b0;
            cmd_rdsr_strobe <= 1'b0;
            flash_write_ack <= 1'b0;
            flash_write_rdt <= 32'h00000000;
        end else begin
            // Clear strobes after one cycle
            cmd_wren_strobe <= 1'b0;
            cmd_se_strobe <= 1'b0;
            cmd_pp_strobe <= 1'b0;
            cmd_rdsr_strobe <= 1'b0;

            // Handle flash write register access
            if (flash_write_access && !flash_write_ack) begin
                flash_write_ack <= 1'b1;

                if (wb_cpu_mem_we) begin
                    // Write access
                    unique case (flash_write_reg)
                        4'h0: begin  // CTRL register - command strobes
                            if (wb_cpu_mem_dat[0]) cmd_wren_strobe <= 1'b1;
                            if (wb_cpu_mem_dat[1]) cmd_se_strobe <= 1'b1;
                            if (wb_cpu_mem_dat[2]) cmd_pp_strobe <= 1'b1;
                            if (wb_cpu_mem_dat[3]) cmd_rdsr_strobe <= 1'b1;
                        end
                        4'h4: flash_write_addr_reg <= wb_cpu_mem_dat[23:0];  // ADDR register
                        4'h8: flash_write_data_reg <= wb_cpu_mem_dat[7:0];   // DATA register
                        // 4'hC: STATUS is read-only
                    endcase
                end else begin
                    // Read access
                    unique case (flash_write_reg)
                        4'h0: flash_write_rdt <= 32'h00000000;  // CTRL (write-only)
                        4'h4: flash_write_rdt <= {8'h00, flash_write_addr_reg};
                        4'h8: flash_write_rdt <= {24'h000000, flash_write_data_reg};
                        4'hC: flash_write_rdt <= {24'h000000,
                                                   4'b0000,           // bits 7:4
                                                   flash_write_error, // bit 3 (STATUS_ERROR)
                                                   1'b0,              // bit 2
                                                   flash_write_busy,  // bit 1 (STATUS_BUSY)
                                                   1'b0};             // bit 0
                        default: flash_write_rdt <= 32'hDEADBEEF;
                    endcase
                end
            end else begin
                flash_write_ack <= 1'b0;
            end
        end
    end

    //========================================================================
    // QSPI Flash Controller (s25fl_xip - Custom XIP Controller)
    //========================================================================

    // Note: QSPI status signals (qspi_startup_busy, qspi_state)
    // are declared earlier in the file for IEEE Verilog compliance

    // Conditional instantiation: RAM mimic / Internal QSPI / External Wishbone
    generate
        if (USE_EXTERNAL_FLASH_WB == 1) begin : gen_external_flash
            // External flash controller (e.g., MAX10 UFM)
            // Expose internal wishbone signals as module ports
            assign o_wb_flash_cyc = wb_flash_cyc;
            assign o_wb_flash_stb = wb_flash_stb;
            assign o_wb_flash_adr = wb_flash_adr;
            assign o_wb_flash_dat = wb_flash_dat;
            assign o_wb_flash_sel = wb_flash_sel;
            assign o_wb_flash_we  = wb_flash_we;
            assign wb_flash_rdt   = i_wb_flash_rdt;
            assign wb_flash_ack   = i_wb_flash_ack;
            assign wb_flash_stall = i_wb_flash_stall;

            // Tie off QSPI pins (not used)
            assign qspi_clk = 1'b0;
            assign qspi_cs_n = 1'b1;
            assign qspi_d[0] = 1'bz;
            assign qspi_d[1] = 1'bz;
            assign qspi_d[2] = 1'bz;
            assign qspi_d[3] = 1'bz;

            // Tie off flash status signals
            assign qspi_startup_busy = 1'b0;
            assign qspi_state = 5'h0;

            // Route write command strobes and registers to board wrapper
            assign o_flash_wr_cmd_wren  = cmd_wren_strobe;
            assign o_flash_wr_cmd_se    = cmd_se_strobe;
            assign o_flash_wr_cmd_pp    = cmd_pp_strobe;
            assign o_flash_wr_cmd_rdsr  = cmd_rdsr_strobe;
            assign o_flash_wr_addr      = flash_write_addr_reg;
            assign o_flash_wr_data      = flash_write_data_reg;

            // Route write status from board wrapper
            assign flash_write_busy  = i_flash_wr_busy;
            assign flash_write_error = i_flash_wr_error;

            // UFM Write interface (MAX10 only)
            assign o_wb_ufm_cyc = wb_ufm_cyc;
            assign o_wb_ufm_stb = wb_ufm_stb;
            assign o_wb_ufm_adr = wb_ufm_adr;
            assign o_wb_ufm_dat = wb_ufm_dat;
            assign o_wb_ufm_sel = wb_ufm_sel;
            assign o_wb_ufm_we  = wb_ufm_we;
            assign wb_ufm_rdt   = i_wb_ufm_rdt;
            assign wb_ufm_ack   = i_wb_ufm_ack;

        end else if (USE_FAST_RAM_FLASH == 1) begin : gen_ram_flash
            // Fast RAM mimic for testing (1-cycle access)
            wb_ram_flash_mimic #(
                .DEPTH(16384)  // 16K words = 64KB
            ) ram_flash_mimic (
                .i_clk(wb_clk),
                .i_rst(wb_rst),

                // Wishbone interface
                .i_wb_cyc(wb_flash_cyc),
                .i_wb_addr(wb_flash_adr),
                .i_wb_data(32'h0),          // Read-only for XIP
                .i_wb_sel(4'b1111),
                .i_wb_we(1'b0),             // Read-only
                .i_wb_stb(wb_flash_stb),
                .o_wb_rdt(wb_flash_rdt),
                .o_wb_ack(wb_flash_ack)
            );

            // Tie off flash-specific signals
            assign wb_flash_stall = 1'b0;        // RAM never stalls
            assign qspi_startup_busy = 1'b0;     // Always ready
            assign qspi_state = 5'h0;            // N/A for RAM

            // For RAM flash mode, we don't use QSPI mux - tie off pins directly
            assign qspi_clk = 1'b0;
            assign qspi_cs_n = 1'b1;             // Deassert CS

            // Tie off QSPI data pins (all Hi-Z in RAM mode)
            assign qspi_d[0] = 1'bz;
            assign qspi_d[1] = 1'bz;
            assign qspi_d[2] = 1'bz;
            assign qspi_d[3] = 1'bz;

            // Tie off external flash wishbone ports (not used)
            assign o_wb_flash_cyc = 1'b0;
            assign o_wb_flash_stb = 1'b0;
            assign o_wb_flash_adr = 32'h0;
            assign o_wb_flash_dat = 32'h0;
            assign o_wb_flash_sel = 4'h0;
            assign o_wb_flash_we  = 1'b0;

            // Tie off write command ports and status (no write controller in RAM mode)
            assign o_flash_wr_cmd_wren  = 1'b0;
            assign o_flash_wr_cmd_se    = 1'b0;
            assign o_flash_wr_cmd_pp    = 1'b0;
            assign o_flash_wr_cmd_rdsr  = 1'b0;
            assign o_flash_wr_addr      = 24'h0;
            assign o_flash_wr_data      = 8'h0;
            assign flash_write_busy     = 1'b0;
            assign flash_write_error    = 1'b0;

            // Tie off UFM write interface (not used in RAM mode)
            assign o_wb_ufm_cyc = 1'b0;
            assign o_wb_ufm_stb = 1'b0;
            assign o_wb_ufm_adr = 32'h0;
            assign o_wb_ufm_dat = 32'h0;
            assign o_wb_ufm_sel = 4'h0;
            assign o_wb_ufm_we  = 1'b0;
            assign wb_ufm_rdt   = 32'h0;
            assign wb_ufm_ack   = 1'b0;

        end else if (USE_CACHE == 1) begin : gen_cached_flash
            // 4KB BRAM cache + BURST_WORDS=4 XIP + Write + Mux
            // Matches the Arty S7-50 board wrapper configuration exactly.

            // 128-bit burst wires between cache and BURST_WORDS=4 XIP
            logic [127:0] cache_xip_burst;
            logic         cache_xip_cyc, cache_xip_stb;
            logic [31:0]  cache_xip_addr;
            logic         cache_xip_ack, cache_xip_stall;

            // XIP is idle when cache has no pending burst request and CS# deasserted.
            // Use cache's downstream cyc so write controller can grab the bus after
            // a burst completes but before the cache has finished filling BRAM.
            logic xip_idle;
            assign xip_idle = !cache_xip_cyc && xip_cs_n;

            // 4KB direct-mapped BRAM cache (256 lines × 16 bytes)
            qspi_cache_bram #(.NUM_LINES(256)) flash_cache (
                .i_clk     (wb_clk),
                .i_reset   (wb_rst),
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
                .i_xip_data (cache_xip_burst),
                .i_xip_ack  (cache_xip_ack),
                .i_xip_stall(cache_xip_stall)
            );

            // XIP with BURST_WORDS=4 (16-byte bursts to fill one cache line)
            s25fl_xip #(
                .CLK_DIV(2),
                .FLASH_ADDR_WIDTH(24),
                .WRR_WAIT_CYCLES(16'd65535),
                .BURST_WORDS(4)
            ) qspi_flash (
                .i_clk(wb_clk),
                .i_reset(wb_rst),
                .i_wb_cyc  (cache_xip_cyc),
                .i_wb_stb  (cache_xip_stb),
                .i_wb_addr (cache_xip_addr),
                .o_wb_data (cache_xip_burst),
                .o_wb_ack  (cache_xip_ack),
                .o_wb_stall(cache_xip_stall),
                .o_qspi_sck(xip_sck),
                .o_qspi_cs_n(xip_cs_n),
                .o_qspi_dat(xip_dat_out),
                .o_qspi_oe (xip_dat_oe),
                .i_qspi_dat(xip_dat_in),
                .o_startup_busy(qspi_startup_busy),
                .o_state(qspi_state)
            );

            // Flash Write Controller
            s25fl_write #(
                .CLK_DIV(2)
            ) flash_write (
                .i_clk(wb_clk),
                .i_reset(wb_rst),
                .i_cmd_wren(cmd_wren_strobe),
                .i_cmd_se(cmd_se_strobe),
                .i_cmd_pp(cmd_pp_strobe),
                .i_cmd_rdsr(cmd_rdsr_strobe),
                .i_address(flash_write_addr_reg),
                .i_data(flash_write_data_reg),
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

            assign qspi_clk = qspi_sck_mux;
            assign qspi_cs_n = qspi_cs_n_mux;
            assign qspi_d[0] = qspi_dat_oe_mux[0] ? qspi_dat_out_mux[0] : 1'bz;
            assign qspi_d[1] = qspi_dat_oe_mux[1] ? qspi_dat_out_mux[1] : 1'bz;
            assign qspi_d[2] = qspi_dat_oe_mux[2] ? qspi_dat_out_mux[2] : 1'bz;
            assign qspi_d[3] = qspi_dat_oe_mux[3] ? qspi_dat_out_mux[3] : 1'bz;
            assign qspi_dat_in_mux = qspi_d;

            assign o_wb_flash_cyc = 1'b0;
            assign o_wb_flash_stb = 1'b0;
            assign o_wb_flash_adr = 32'h0;
            assign o_wb_flash_dat = 32'h0;
            assign o_wb_flash_sel = 4'h0;
            assign o_wb_flash_we  = 1'b0;
            assign o_flash_wr_cmd_wren  = 1'b0;
            assign o_flash_wr_cmd_se    = 1'b0;
            assign o_flash_wr_cmd_pp    = 1'b0;
            assign o_flash_wr_cmd_rdsr  = 1'b0;
            assign o_flash_wr_addr      = 24'h0;
            assign o_flash_wr_data      = 8'h0;
            assign o_wb_ufm_cyc = 1'b0;
            assign o_wb_ufm_stb = 1'b0;
            assign o_wb_ufm_adr = 32'h0;
            assign o_wb_ufm_dat = 32'h0;
            assign o_wb_ufm_sel = 4'h0;
            assign o_wb_ufm_we  = 1'b0;
            assign wb_ufm_rdt   = 32'h0;
            assign wb_ufm_ack   = 1'b0;

        end else begin : gen_real_flash
            // Real QSPI Flash Controller with Write Support
            // Fixed timing: 6 dummy cycles (S25FL128S default), sample edge shift
            // Works reliably on both FPGA hardware and Verilator simulation

            // XIP idle signal (write controller needs to wait for XIP to be idle)
            // XIP is idle when there's no active Wishbone cycle AND CS# is deasserted
            // CRITICAL: Must check wb_flash_cyc (active request) not just stall
            // Stall can be low between reads, but cyc indicates active use
            logic xip_idle = !wb_flash_cyc && xip_cs_n;  // No active cycle and CS# deasserted

            // XIP Flash Controller
            s25fl_xip #(
                .CLK_DIV(2),                           // QSPI clock: 100MHz / (2*2+2) = 16.7MHz
                .FLASH_ADDR_WIDTH(24),                 // 24 bits = 16MB flash
                .WRR_WAIT_CYCLES(16'd65535)            // Wait for WRR completion
            ) qspi_flash (
                .i_clk(wb_clk),
                .i_reset(wb_rst),

                // Wishbone interface (read-only)
                .i_wb_cyc(wb_flash_cyc),
                .i_wb_stb(wb_flash_stb),
                .i_wb_addr(wb_flash_adr),   // Full 32-bit byte address
                .o_wb_data(wb_flash_rdt),
                .o_wb_ack(wb_flash_ack),
                .o_wb_stall(wb_flash_stall),

                // QSPI physical interface (to mux, not directly to pins)
                .o_qspi_sck(xip_sck),
                .o_qspi_cs_n(xip_cs_n),
                .o_qspi_dat(xip_dat_out),  // Output data
                .o_qspi_oe(xip_dat_oe),    // Output enables (per bit)
                .i_qspi_dat(xip_dat_in),   // Input data

                // Status outputs for debug register
                .o_startup_busy(qspi_startup_busy),
                .o_state(qspi_state)
            );

            // Flash Write Controller
            s25fl_write #(
                .CLK_DIV(2)
            ) flash_write (
                .i_clk(wb_clk),
                .i_reset(wb_rst),

                .i_cmd_wren(cmd_wren_strobe),
                .i_cmd_se(cmd_se_strobe),
                .i_cmd_pp(cmd_pp_strobe),
                .i_cmd_rdsr(cmd_rdsr_strobe),
                .i_address(flash_write_addr_reg),
                .i_data(flash_write_data_reg),
                .i_data_valid(1'b1),  // Simplified: single byte mode

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
                .i_write_active(flash_write_active),  // Write controller active signal

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

            // Connect muxed signals to actual QSPI pins
            assign qspi_clk = qspi_sck_mux;
            assign qspi_cs_n = qspi_cs_n_mux;

            // Bidirectional buffers
            assign qspi_d[0] = qspi_dat_oe_mux[0] ? qspi_dat_out_mux[0] : 1'bz;
            assign qspi_d[1] = qspi_dat_oe_mux[1] ? qspi_dat_out_mux[1] : 1'bz;
            assign qspi_d[2] = qspi_dat_oe_mux[2] ? qspi_dat_out_mux[2] : 1'bz;
            assign qspi_d[3] = qspi_dat_oe_mux[3] ? qspi_dat_out_mux[3] : 1'bz;
            assign qspi_dat_in_mux = qspi_d;

            // Tie off external flash wishbone ports (using internal controller)
            assign o_wb_flash_cyc = 1'b0;
            assign o_wb_flash_stb = 1'b0;
            assign o_wb_flash_adr = 32'h0;
            assign o_wb_flash_dat = 32'h0;
            assign o_wb_flash_sel = 4'h0;
            assign o_wb_flash_we  = 1'b0;

            // Tie off external write command ports (using internal write controller)
            assign o_flash_wr_cmd_wren  = 1'b0;
            assign o_flash_wr_cmd_se    = 1'b0;
            assign o_flash_wr_cmd_pp    = 1'b0;
            assign o_flash_wr_cmd_rdsr  = 1'b0;
            assign o_flash_wr_addr      = 24'h0;
            assign o_flash_wr_data      = 8'h0;

            // Tie off UFM write interface (not used with real QSPI flash)
            assign o_wb_ufm_cyc = 1'b0;
            assign o_wb_ufm_stb = 1'b0;
            assign o_wb_ufm_adr = 32'h0;
            assign o_wb_ufm_dat = 32'h0;
            assign o_wb_ufm_sel = 4'h0;
            assign o_wb_ufm_we  = 1'b0;
            assign wb_ufm_rdt   = 32'h0;
            assign wb_ufm_ack   = 1'b0;
        end
    endgenerate

    //========================================================================
    // Register File
    //========================================================================
    serv_rf_ram #(
        .width(rf_width),
        .csr_regs(csr_regs)
    ) rf (
        .i_clk(wb_clk),
        .i_waddr(rf_waddr),
        .i_wdata(rf_wdata),
        .i_wen(rf_wen),
        .i_raddr(rf_raddr),
        .i_ren(rf_ren),
        .o_rdata(rf_rdata)
    );

    //========================================================================
    // SERV CPU
    //========================================================================
    servile #(
        .width(width),
        .reset_pc(reset_pc),
        .reset_strategy(reset_strategy),
`ifdef SIMULATION
        .sim(1'b1),  // Enable simulation mode only for simulation
`else
        .sim(1'b0),  // Disable for synthesis (avoids unsynthesizable $value$plusargs)
`endif
        .debug(1'b0),
        .with_c(1'b0),
        .with_csr(with_csr[0]),
        .with_mdu(1'b0)
    ) cpu (
        .i_clk(wb_clk),
        .i_rst(wb_rst),
        .i_timer_irq(timer_irq),

        // Memory interface (goes to mem_mux for RAM+Flash)
        .o_wb_mem_adr(wb_cpu_mem_adr),
        .o_wb_mem_dat(wb_cpu_mem_dat),
        .o_wb_mem_sel(wb_cpu_mem_sel),
        .o_wb_mem_we(wb_cpu_mem_we),
        .o_wb_mem_stb(wb_cpu_mem_stb),
        .i_wb_mem_rdt(wb_cpu_mem_rdt),
        .i_wb_mem_ack(wb_cpu_mem_ack),

        // Extension/peripheral interface
        .o_wb_ext_adr(wb_ext_adr),
        .o_wb_ext_dat(wb_ext_dat),
        .o_wb_ext_sel(wb_ext_sel),
        .o_wb_ext_we(wb_ext_we),
        .o_wb_ext_stb(wb_ext_stb),
        .i_wb_ext_rdt(wb_ext_rdt),
        .i_wb_ext_ack(wb_ext_ack),

        // Register file interface
        .o_rf_waddr(rf_waddr),
        .o_rf_wdata(rf_wdata),
        .o_rf_wen(rf_wen),
        .o_rf_raddr(rf_raddr),
        .o_rf_ren(rf_ren),
        .i_rf_rdata(rf_rdata)
    );

endmodule
