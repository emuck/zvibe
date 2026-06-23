///////////////////////////////////////////////////////////////////////////////
// servant_zvibe_max10_08_eval_xip.sv
//
// Servant ZVibe Top Level for MAX10 08 Evaluation Board (Pure XIP)
//
// FPGA: Intel MAX10 10M08
// Clock: External 50 MHz → PLL → 100 MHz
// UART: USB-UART bridge @ 115200 baud
// Flash: On-chip UFM (User Flash Memory) - Execute-in-Place (XIP)
//
// Copyright (c) 2025 Martin R. Raumann
// SPDX-License-Identifier: BSD-3-Clause
//
// Boot Architecture:
// - CPU resets to 0x80001000 (UFM XIP address)
// - Executes directly from UFM (no bootloader, no RAM copy)
// - RAM used only for .data, .bss, stack
//
///////////////////////////////////////////////////////////////////////////////


module servant_zvibe_max10_08_eval_xip #(
    parameter FAST_UART = 0           // 0=real UART, 1=fast Wishbone model (sim only)
)(
    // External 50 MHz clock input (CLK0p on Bank 2)
    input  logic clk_50mhz,

    // DIP Switch 1 - PLL async reset (active high, normally OFF)
    input  logic dip_sw1,

    // UART (connected to USB-UART bridge)
    input  logic uart_rxd,
    output logic uart_txd,

    // LEDs for debugging (5 LEDs - LED4 for PLL locked)
    output logic [4:0] gpio_led

`ifdef SIMULATION
    // Simulation-only: UART RX bypass for uart_wb_model injection
    ,input  logic        uart_rx_valid
    ,input  logic [7:0]  uart_rx_char
`endif
);

    //========================================================================
    // Clock Generation (PLL: 50 MHz → 100 MHz)
    //========================================================================
    logic sys_clk;          // 100 MHz system clock
    logic pll_locked;       // PLL lock status (active high when stable)

    // PLL: 50 MHz → 100 MHz via ALTPLL primitive (MAX10 on-chip PLL).
    // Direct instantiation — no wizard wrapper needed.
    wire [4:0] pll_clk_out;
    assign sys_clk = pll_clk_out[0];

    altpll pll_inst (
        .areset        (dip_sw1),
        .inclk         ({1'b0, clk_50mhz}),
        .clk           (pll_clk_out),
        .locked        (pll_locked),
        // Unused ports tied off per Quartus megafunction convention
        .activeclock   (), .clkbad      (), .clkena ({6{1'b1}}), .clkloss      (),
        .clkswitch     (1'b0), .configupdate(1'b0), .enable0 (), .enable1      (),
        .extclk        (), .extclkena   ({4{1'b1}}), .fbin   (1'b1),
        .fbmimicbidir  (), .fbout       (), .fref    (), .icdrclk      (),
        .pfdena        (1'b1), .phasecounterselect({4{1'b1}}),
        .phasedone     (), .phasestep   (1'b1), .phaseupdown(1'b1), .pllena(1'b1),
        .scanaclr      (1'b0), .scanclk    (1'b0), .scanclkena (1'b1),
        .scandata      (1'b0), .scandataout(), .scandone   (),
        .scanread      (1'b0), .scanwrite  (1'b0),
        .sclkout0      (), .sclkout1    (), .vcooverrange(), .vcounderrange()
    );
    defparam
        pll_inst.bandwidth_type          = "AUTO",
        pll_inst.clk0_divide_by          = 1,
        pll_inst.clk0_duty_cycle         = 50,
        pll_inst.clk0_multiply_by        = 2,
        pll_inst.clk0_phase_shift        = "0",
        pll_inst.compensate_clock        = "CLK0",
        pll_inst.inclk0_input_frequency  = 20000,   // 20 000 ps = 50 MHz
        pll_inst.intended_device_family  = "MAX 10",
        pll_inst.lpm_type                = "altpll",
        pll_inst.operation_mode          = "NORMAL",
        pll_inst.pll_type                = "AUTO",
        pll_inst.port_activeclock        = "PORT_UNUSED",
        pll_inst.port_areset             = "PORT_USED",
        pll_inst.port_clkbad0            = "PORT_UNUSED",
        pll_inst.port_clkbad1            = "PORT_UNUSED",
        pll_inst.port_clkloss            = "PORT_UNUSED",
        pll_inst.port_clkswitch          = "PORT_UNUSED",
        pll_inst.port_configupdate       = "PORT_UNUSED",
        pll_inst.port_fbin               = "PORT_UNUSED",
        pll_inst.port_inclk0             = "PORT_USED",
        pll_inst.port_inclk1             = "PORT_UNUSED",
        pll_inst.port_locked             = "PORT_USED",
        pll_inst.port_pfdena             = "PORT_UNUSED",
        pll_inst.port_phasecounterselect = "PORT_UNUSED",
        pll_inst.port_phasedone          = "PORT_UNUSED",
        pll_inst.port_phasestep          = "PORT_UNUSED",
        pll_inst.port_phaseupdown        = "PORT_UNUSED",
        pll_inst.port_pllena             = "PORT_UNUSED",
        pll_inst.port_scanaclr           = "PORT_UNUSED",
        pll_inst.port_scanclk            = "PORT_UNUSED",
        pll_inst.port_scanclkena         = "PORT_UNUSED",
        pll_inst.port_scandata           = "PORT_UNUSED",
        pll_inst.port_scandataout        = "PORT_UNUSED",
        pll_inst.port_scandone           = "PORT_UNUSED",
        pll_inst.port_scanread           = "PORT_UNUSED",
        pll_inst.port_scanwrite          = "PORT_UNUSED",
        pll_inst.port_clk0               = "PORT_USED",
        pll_inst.port_clk1               = "PORT_UNUSED",
        pll_inst.port_clk2               = "PORT_UNUSED",
        pll_inst.port_clk3               = "PORT_UNUSED",
        pll_inst.port_clk4               = "PORT_UNUSED",
        pll_inst.port_clk5               = "PORT_UNUSED",
        pll_inst.port_clkena0            = "PORT_UNUSED",
        pll_inst.port_clkena1            = "PORT_UNUSED",
        pll_inst.port_clkena2            = "PORT_UNUSED",
        pll_inst.port_clkena3            = "PORT_UNUSED",
        pll_inst.port_clkena4            = "PORT_UNUSED",
        pll_inst.port_clkena5            = "PORT_UNUSED",
        pll_inst.port_extclk0            = "PORT_UNUSED",
        pll_inst.port_extclk1            = "PORT_UNUSED",
        pll_inst.port_extclk2            = "PORT_UNUSED",
        pll_inst.port_extclk3            = "PORT_UNUSED",
        pll_inst.self_reset_on_loss_lock = "OFF",
        pll_inst.width_clock             = 5;

    // For simulation, bypass PLL lock wait (just use clock enable from DIP switch)
    `ifdef SIMULATION
        logic clk_locked;
        assign clk_locked = !dip_sw1;  // Locked when not in reset
        initial $display("[SERVANT_ZVIBE_MAX10] SIMULATION mode: bypassing PLL lock");
    `else
        logic clk_locked;
        assign clk_locked = pll_locked;
        initial $display("[SERVANT_ZVIBE_MAX10] SYNTHESIS mode: using real PLL lock");
    `endif

    //========================================================================
    // Reset Generation
    //========================================================================
    // Power-on reset counter: Hold reset for 256 clocks after clock locks
    logic [7:0] por_counter = 8'h00;
    logic       por_reset   = 1'b1;

    // Synchronous reset version (avoids Questa simulation issues with async reset)
    always_ff @(posedge sys_clk) begin
        if (!clk_locked) begin
            por_counter <= 8'h00;
            por_reset <= 1'b1;
        end else if (por_counter != 8'hFF) begin
            por_counter <= por_counter + 1;
            por_reset <= 1'b1;
            `ifdef SIMULATION
            if (por_counter == 0 || por_counter == 1 || por_counter == 254)
                $display("[%0t] por_counter increment: %d -> %d (sys_clk period should be 20ns)",
                         $time, por_counter, por_counter + 1);
            `endif
        end else begin
            por_reset <= 1'b0;
        end
    end

    logic reset;
    logic reset_n;
    assign reset   = !clk_locked | por_reset;  // Active-high reset
    assign reset_n = !reset;                    // Active-low reset

    //========================================================================
    // UFM IP Core Instance (Read + Write mode)
    //========================================================================
    // Avalon-MM Data interface (read and write)
    logic [16:0] ufm_address;  // 17-bit address (vendor UFM IP uses 17 bits)
    logic        ufm_read;
    logic [31:0] ufm_readdata;
    logic        ufm_waitrequest;
    logic        ufm_readdatavalid;
    logic [3:0]  ufm_burstcount;

    // Avalon-MM Data write signals (from unified controller)
    logic        ufm_data_write;
    logic [31:0] ufm_data_writedata;

    // Avalon-MM CSR interface (control/status for erase/program)
    logic        ufm_csr_addr;
    logic        ufm_csr_read;
    logic        ufm_csr_write;
    logic [31:0] ufm_csr_writedata;
    logic [31:0] ufm_csr_readdata;

    //========================================================================
    // Pipeline Registers - Break Critical Path to UFM IP
    //========================================================================
    // Forward path: register UFM IP inputs from unified controller
    logic [16:0] ufm_data_addr_pipe;
    logic        ufm_data_read_pipe;
    logic        ufm_data_write_pipe;
    logic [31:0] ufm_data_writedata_pipe;
    logic [3:0]  ufm_data_burstcount_pipe;

    // Reverse path: delay UFM IP outputs back to unified controller by 1 cycle
    logic [31:0] ufm_readdata_pipe;
    logic        ufm_readdatavalid_pipe;

    // Forward path pipeline stage (unified controller → UFM IP)
    always_ff @(posedge sys_clk) begin
        if (reset) begin
            ufm_data_addr_pipe       <= 17'h0;
            ufm_data_read_pipe       <= 1'b0;
            ufm_data_write_pipe      <= 1'b0;
            ufm_data_writedata_pipe  <= 32'h0;
            ufm_data_burstcount_pipe <= 4'd1;
        end else begin
            // Pipeline all signals from unified controller (no mux needed)
            ufm_data_addr_pipe       <= ufm_address;
            ufm_data_read_pipe       <= ufm_read;
            ufm_data_write_pipe      <= ufm_data_write;
            ufm_data_writedata_pipe  <= ufm_data_writedata;
            ufm_data_burstcount_pipe <= ufm_burstcount;
        end
    end

    // Reverse path pipeline stage (UFM IP → unified controller)
    always_ff @(posedge sys_clk) begin
        if (reset) begin
            ufm_readdata_pipe      <= 32'h0;
            ufm_readdatavalid_pipe <= 1'b0;
        end else begin
            ufm_readdata_pipe      <= ufm_readdata;
            ufm_readdatavalid_pipe <= ufm_readdatavalid;
        end
    end

    // UFM IP core (Read + Write mode with CSR interface)
    ufm ufm_inst (
        .clock(sys_clk),
        .reset_n(reset_n),

        // Avalon-MM Data interface - PIPELINED INPUTS (critical path broken)
        .avmm_data_addr(ufm_data_addr_pipe),
        .avmm_data_read(ufm_data_read_pipe),
        .avmm_data_write(ufm_data_write_pipe),
        .avmm_data_writedata(ufm_data_writedata_pipe),
        .avmm_data_burstcount(ufm_data_burstcount_pipe),

        // Avalon-MM Data interface - DIRECT OUTPUTS (to pipeline stage)
        .avmm_data_readdata(ufm_readdata),
        .avmm_data_waitrequest(ufm_waitrequest),   // NOT pipelined - immediate feedback
        .avmm_data_readdatavalid(ufm_readdatavalid),

        // Avalon-MM CSR interface (not on critical path - direct connection)
        .avmm_csr_addr(ufm_csr_addr),
        .avmm_csr_read(ufm_csr_read),
        .avmm_csr_write(ufm_csr_write),
        .avmm_csr_writedata(ufm_csr_writedata),
        .avmm_csr_readdata(ufm_csr_readdata)
    );

    //========================================================================
    // UFM Unified Controller (XIP + CSR/Write at 0x80xxxxxx)
    //========================================================================
    // Wishbone slave interface (from SoC)
    logic        wb_flash_cyc;
    logic        wb_flash_stb;
    logic [31:0] wb_flash_adr;
    logic [31:0] wb_flash_dat;
    logic [3:0]  wb_flash_sel;
    logic        wb_flash_we;
    logic [31:0] wb_flash_rdt;
    logic        wb_flash_ack;
    logic        wb_flash_stall;

    max10_ufm_unified #(
        .UFM_ADDR_WIDTH(17),
        .DATA_WIDTH(32)
    ) ufm_unified (
        .i_clk(sys_clk),
        .i_reset(reset),

        // Wishbone slave interface (from SoC wb_flash)
        .i_wb_cyc(wb_flash_cyc),
        .i_wb_stb(wb_flash_stb),
        .i_wb_addr(wb_flash_adr),
        .i_wb_data(wb_flash_dat),
        .i_wb_sel(wb_flash_sel),
        .i_wb_we(wb_flash_we),
        .o_wb_data(wb_flash_rdt),
        .o_wb_ack(wb_flash_ack),
        .o_wb_stall(wb_flash_stall),

        // Avalon-MM CSR interface (direct to UFM IP)
        .o_avmm_csr_addr(ufm_csr_addr),
        .o_avmm_csr_read(ufm_csr_read),
        .o_avmm_csr_write(ufm_csr_write),
        .o_avmm_csr_writedata(ufm_csr_writedata),
        .i_avmm_csr_readdata(ufm_csr_readdata),

        // Avalon-MM Data interface (to pipeline stage)
        .o_avmm_data_addr(ufm_address),
        .o_avmm_data_read(ufm_read),
        .o_avmm_data_write(ufm_data_write),
        .o_avmm_data_writedata(ufm_data_writedata),
        .o_avmm_data_burstcount(ufm_burstcount),
        .i_avmm_data_readdata(ufm_readdata_pipe),           // PIPELINED
        .i_avmm_data_waitrequest(ufm_waitrequest),          // DIRECT
        .i_avmm_data_readdatavalid(ufm_readdatavalid_pipe)  // PIPELINED
    );

    //========================================================================
    // GPIO LED signals from SoC
    //========================================================================
    logic [2:0] soc_gpio_led;

    //========================================================================
    // Servant ZVibe SoC Instance - XIP Configuration
    //========================================================================
    // Pure XIP architecture:
    // - CPU boots at reset_pc=0x80000100 (UFM XIP, after 256B metadata)
    // - No RAM pre-initialization (memfile="")
    // - External flash WB interface connected to UFM XIP controller
    // - No bootloader FSM needed
    // UART prescaler: 100MHz / 115200 baud / 8 = 108
    localparam UART_PRESCALE = 108;

    servant_zvibe #(
        .memfile(""),                  // No preload - execute from UFM XIP
        .memsize(32*1024),             // 32KB RAM
        .reset_strategy("MINI"),
        .reset_pc(32'h80000100),       // Boot directly to XIP code at UFM offset 0x100 (after metadata)
        .with_csr(1),                  // Enable CSRs (needed for proper exception handling)
        .UART_PRESCALE(UART_PRESCALE), // 108 @ 100MHz
        .UART_FIFO_DEPTH(64),
        .FAST_UART(FAST_UART),         // Pass through for simulation
        .FLASH_SIZE(16),               // 16-bit UFM addresses (172KB)
        .USE_FAST_RAM_FLASH(0),        // Not using RAM mimic
        .USE_EXTERNAL_FLASH_WB(1)      // Use external UFM XIP controller
    ) soc (
        .wb_clk(sys_clk),
        .wb_rst(reset),
        .uart_rxd(uart_rxd),
        .uart_txd(uart_txd),
`ifdef SIMULATION
        .uart_rx_valid(uart_rx_valid),
        .uart_rx_char(uart_rx_char),
`else
        .uart_rx_valid(1'b0),
        .uart_rx_char(8'h00),
`endif

        // QSPI pins (not used for MAX10)
        .qspi_clk(),
        .qspi_cs_n(),
        .qspi_d(),

        // External flash Wishbone interface (to UFM Unified Controller)
        .o_wb_flash_cyc(wb_flash_cyc),
        .o_wb_flash_stb(wb_flash_stb),
        .o_wb_flash_adr(wb_flash_adr),
        .o_wb_flash_dat(wb_flash_dat),      // Now used for UFM writes
        .o_wb_flash_sel(wb_flash_sel),      // Now used for UFM writes
        .o_wb_flash_we(wb_flash_we),        // Now used for UFM writes
        .i_wb_flash_rdt(wb_flash_rdt),
        .i_wb_flash_ack(wb_flash_ack),
        .i_wb_flash_stall(wb_flash_stall),

        // Flash write command interface (not used on MAX10)
        .o_flash_wr_cmd_wren(),
        .o_flash_wr_cmd_se(),
        .o_flash_wr_cmd_pp(),
        .o_flash_wr_cmd_rdsr(),
        .o_flash_wr_addr(),
        .o_flash_wr_data(),
        .i_flash_wr_busy(1'b0),
        .i_flash_wr_error(1'b0),

        // UFM Write interface - REMOVED (now unified with wb_flash)
        .o_wb_ufm_cyc(),
        .o_wb_ufm_stb(),
        .o_wb_ufm_adr(),
        .o_wb_ufm_dat(),
        .o_wb_ufm_sel(),
        .o_wb_ufm_we(),
        .i_wb_ufm_rdt(32'h0),
        .i_wb_ufm_ack(1'b0),

        .gpio_led(soc_gpio_led)
    );

    //========================================================================
    // Heartbeat LED (LED4) - uses system clock
    //========================================================================
    // Counter size adjusted for clock frequency:
    // 100MHz: bit[26] = ~1.3Hz
    logic [26:0] heartbeat_counter = '0;

    always_ff @(posedge sys_clk) begin
        heartbeat_counter <= heartbeat_counter + 1;
    end

    //========================================================================
    // GPIO LED Assignment - UFM DEBUG INDICATORS
    //========================================================================
    // LEDs are active-low on this board (illuminate when driven to 0)
    // LED0: Wishbone flash cycle (CPU trying to read UFM)
    // LED1: UFM read valid (UFM returning data)
    // LED2: UFM waitrequest (UFM busy)
    // LED3: Reset signal (should be OFF after boot)
    // LED4: Heartbeat

    // Stretch wb_flash_cyc so it's visible on LED
    logic       wb_flash_active  = '0;
    logic [20:0] wb_activity_stretch = '0;

    always_ff @(posedge sys_clk) begin
        if (wb_flash_cyc) begin
            wb_flash_active      <= 1'b1;
            wb_activity_stretch  <= {21{1'b1}};  // Hold for ~20ms
        end else if (wb_activity_stretch != '0) begin
            wb_activity_stretch  <= wb_activity_stretch - 1;
        end else begin
            wb_flash_active <= 1'b0;
        end
    end

    // Stretch ufm_readdatavalid so it's visible
    logic        ufm_valid_active   = '0;
    logic [20:0] ufm_valid_stretch  = '0;

    always_ff @(posedge sys_clk) begin
        if (ufm_readdatavalid) begin
            ufm_valid_active  <= 1'b1;
            ufm_valid_stretch <= {21{1'b1}};  // Hold for ~20ms
        end else if (ufm_valid_stretch != '0) begin
            ufm_valid_stretch <= ufm_valid_stretch - 1;
        end else begin
            ufm_valid_active <= 1'b0;
        end
    end

    assign gpio_led[0] = ~wb_flash_active;       // LED0: CPU reading UFM
    assign gpio_led[1] = ~ufm_valid_active;      // LED1: UFM returning data
    assign gpio_led[2] = ~ufm_waitrequest;       // LED2: UFM busy (inverted logic)
    assign gpio_led[3] = ~reset;                 // LED3: Reset (should be OFF)
    assign gpio_led[4] = ~heartbeat_counter[26]; // LED4: Heartbeat (~1.3-1.7Hz)

endmodule

