// Copyright (c) 2025 Martin R. Raumann
// SPDX-License-Identifier: BSD-3-Clause
//
// Simulation stub for the Altera ALTPLL megafunction.
//
// Three parameters determine the output frequency:
//
//   inclk0_input_frequency  — input clock period in picoseconds
//                             (20000 ps = 50 MHz)
//   clk0_multiply_by        — VCO multiply ratio
//   clk0_divide_by          — VCO divide ratio
//
// Output period = inclk0_input_frequency × clk0_divide_by / clk0_multiply_by  (ps)
// e.g.  20000 × 1 / 2 = 10 000 ps = 100 MHz
//
// Questa/ModelSim: generates clk[0] at the parametrized frequency using #delay.
// In Verilator:    passes inclk[0] through unchanged (RTL #delay not supported).
//                  FAST_UART=1 is always set in Verilator sims, so baud-rate
//                  timing differences between 50 MHz and 100 MHz don't matter.

`timescale 1ns/1ps

/* verilator lint_off DECLFILENAME */
module altpll #(
    // -----------------------------------------------------------------------
    // Frequency parameters — updated by defparam in the top-level wrapper
    // -----------------------------------------------------------------------
    parameter integer inclk0_input_frequency = 20000,   // ps  (50 MHz)
    parameter integer clk0_multiply_by       = 2,
    parameter integer clk0_divide_by         = 1,

    // -----------------------------------------------------------------------
    // Remaining wizard parameters — declared so defparam assignments compile
    // without "parameter not found" warnings; values are ignored in simulation.
    // -----------------------------------------------------------------------
    parameter bandwidth_type              = "AUTO",
    parameter integer clk0_duty_cycle    = 50,
    parameter clk0_phase_shift           = "0",
    parameter compensate_clock           = "CLK0",
    parameter intended_device_family     = "MAX 10",
    parameter lpm_type                   = "altpll",
    parameter operation_mode             = "NORMAL",
    parameter pll_type                   = "AUTO",
    parameter port_activeclock           = "PORT_UNUSED",
    parameter port_areset                = "PORT_USED",
    parameter port_clkbad0               = "PORT_UNUSED",
    parameter port_clkbad1               = "PORT_UNUSED",
    parameter port_clkloss               = "PORT_UNUSED",
    parameter port_clkswitch             = "PORT_UNUSED",
    parameter port_configupdate          = "PORT_UNUSED",
    parameter port_fbin                  = "PORT_UNUSED",
    parameter port_inclk0                = "PORT_USED",
    parameter port_inclk1                = "PORT_UNUSED",
    parameter port_locked                = "PORT_USED",
    parameter port_pfdena                = "PORT_UNUSED",
    parameter port_phasecounterselect    = "PORT_UNUSED",
    parameter port_phasedone             = "PORT_UNUSED",
    parameter port_phasestep             = "PORT_UNUSED",
    parameter port_phaseupdown           = "PORT_UNUSED",
    parameter port_pllena                = "PORT_UNUSED",
    parameter port_scanaclr              = "PORT_UNUSED",
    parameter port_scanclk               = "PORT_UNUSED",
    parameter port_scanclkena            = "PORT_UNUSED",
    parameter port_scandata              = "PORT_UNUSED",
    parameter port_scandataout           = "PORT_UNUSED",
    parameter port_scandone              = "PORT_UNUSED",
    parameter port_scanread              = "PORT_UNUSED",
    parameter port_scanwrite             = "PORT_UNUSED",
    parameter port_clk0                  = "PORT_USED",
    parameter port_clk1                  = "PORT_UNUSED",
    parameter port_clk2                  = "PORT_UNUSED",
    parameter port_clk3                  = "PORT_UNUSED",
    parameter port_clk4                  = "PORT_UNUSED",
    parameter port_clk5                  = "PORT_UNUSED",
    parameter port_clkena0               = "PORT_UNUSED",
    parameter port_clkena1               = "PORT_UNUSED",
    parameter port_clkena2               = "PORT_UNUSED",
    parameter port_clkena3               = "PORT_UNUSED",
    parameter port_clkena4               = "PORT_UNUSED",
    parameter port_clkena5               = "PORT_UNUSED",
    parameter port_extclk0               = "PORT_UNUSED",
    parameter port_extclk1               = "PORT_UNUSED",
    parameter port_extclk2               = "PORT_UNUSED",
    parameter port_extclk3               = "PORT_UNUSED",
    parameter self_reset_on_loss_lock    = "OFF",
    parameter integer width_clock        = 5
)(
    // -----------------------------------------------------------------------
    // Ports — must match the instantiation in servant_zvibe_max10_08_eval_xip.sv
    // -----------------------------------------------------------------------
    input  wire        areset,
    input  wire [1:0]  inclk,
    output wire [4:0]  clk,
    output wire        locked,
    // Clock enable (5 used, driven with 6 bits from top-level; upper bit ignored)
    input  wire [5:0]  clkena,
    // Unused control inputs (accepted to avoid port-mismatch elaboration errors)
    input  wire        clkswitch,
    input  wire        configupdate,
    input  wire        fbin,
    input  wire [3:0]  extclkena,
    input  wire        pfdena,
    input  wire [3:0]  phasecounterselect,
    input  wire        phasestep,
    input  wire        phaseupdown,
    input  wire        pllena,
    input  wire        scanaclr,
    input  wire        scanclk,
    input  wire        scanclkena,
    input  wire        scandata,
    input  wire        scanread,
    input  wire        scanwrite,
    // Unused outputs (driven to safe constants)
    output wire        activeclock,
    output wire [1:0]  clkbad,
    output wire        clkloss,
    output wire        enable0,
    output wire        enable1,
    output wire [3:0]  extclk,
    inout  wire        fbmimicbidir,
    output wire        fbout,
    output wire        fref,
    output wire        icdrclk,
    output wire        phasedone,
    output wire        scandataout,
    output wire        scandone,
    output wire        sclkout0,
    output wire        sclkout1,
    output wire        vcooverrange,
    output wire        vcounderrange
);

`ifdef VERILATOR
    // -----------------------------------------------------------------------
    // In Verilator: pass inclk[0] through to all clock outputs.
    // RTL #delay is not supported by Verilator; the testbench owns clock gen.
    // -----------------------------------------------------------------------
    assign clk = {5{inclk[0]}};

`else
    // -----------------------------------------------------------------------
    // Questa / ModelSim: generate clk[0] at the parametrised frequency.
    //
    // Output half-period (ns):
    //   inclk0_input_frequency [ps] × clk0_divide_by / clk0_multiply_by / 2 / 1000
    //
    // Example (our design):  20000 × 1 / 2 / 2 / 1000 = 5.0 ns  → 100 MHz
    // -----------------------------------------------------------------------
    localparam real CLK_OUT_HALF_PERIOD_NS =
        (1.0 * inclk0_input_frequency * clk0_divide_by) /
        (1.0 * clk0_multiply_by * 2 * 1000);

    reg clk_out = 1'b0;
    always #(CLK_OUT_HALF_PERIOD_NS) clk_out = ~clk_out;

    assign clk = {5{clk_out}};

`endif // VERILATOR

    // -----------------------------------------------------------------------
    // Lock after 10 input-clock cycles (or immediately on areset release)
    // -----------------------------------------------------------------------
    reg [7:0] lock_cnt = 8'h00;
    reg       locked_r = 1'b0;

    always @(posedge inclk[0] or posedge areset) begin
        if (areset) begin
            lock_cnt <= 8'h00;
            locked_r <= 1'b0;
        end else if (!locked_r) begin
            if (lock_cnt == 8'd9)
                locked_r <= 1'b1;
            else
                lock_cnt <= lock_cnt + 8'd1;
        end
    end

    assign locked = locked_r;

    // -----------------------------------------------------------------------
    // Unused outputs — tie to safe values
    // -----------------------------------------------------------------------
    assign activeclock   = 1'b0;
    assign clkbad        = 2'b00;
    assign clkloss       = 1'b0;
    assign enable0       = 1'b0;
    assign enable1       = 1'b0;
    assign extclk        = 4'b0000;
    assign fbmimicbidir  = 1'bz;
    assign fbout         = 1'b0;
    assign fref          = 1'b0;
    assign icdrclk       = 1'b0;
    assign phasedone     = 1'b1;
    assign scandataout   = 1'b0;
    assign scandone      = 1'b0;
    assign sclkout0      = 1'b0;
    assign sclkout1      = 1'b0;
    assign vcooverrange  = 1'b0;
    assign vcounderrange = 1'b0;

endmodule
/* verilator lint_on DECLFILENAME */
