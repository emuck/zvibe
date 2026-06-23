// Copyright (c) 2025 Martin R. Raumann
// SPDX-License-Identifier: BSD-3-Clause

/*
 * MMCM Clock Generator for Arty S7-50
 *
 * Input:  100MHz from board oscillator
 * Output: Configurable frequency (default: 100MHz)
 *
 * MMCM Configuration:
 * - VCO frequency range: 600-1200 MHz
 * - Input period: 10.0 ns (100MHz)
 *
 * To change output frequency, modify CLKFBOUT_MULT_F and CLKOUT0_DIVIDE_F:
 *   Output frequency = (Input frequency * CLKFBOUT_MULT_F) / CLKOUT0_DIVIDE_F
 *   VCO frequency = Input frequency * CLKFBOUT_MULT_F (must be 600-1200 MHz)
 *
 * Examples:
 *   100MHz: MULT=10.0, DIV=10.0  (VCO=1000MHz)
 *   50MHz:  MULT=10.0, DIV=20.0  (VCO=1000MHz)
 *   133MHz: MULT=10.0, DIV=7.5   (VCO=1000MHz)
 *   200MHz: MULT=12.0, DIV=6.0   (VCO=1200MHz)
 */
module servant_zvibe_arty_s7_50_clock_gen
  (input  logic i_clk,      // 100MHz input from board
   input  logic i_rst,      // Reset input (optional, can tie to 0)
   output logic o_clk,      // Generated clock output
   output logic o_rst);     // Reset output (asserted until MMCM locks)

   logic  clkfb;
   logic  locked;
   logic  locked_r;

   // MMCM Configuration
   // Default: 100MHz output (pass-through equivalent)
   // VCO = 100MHz * 10.0 = 1000MHz (within 600-1200MHz range)
   // Output = 1000MHz / 10.0 = 100MHz
   parameter real CLKFBOUT_MULT_F  = 10.000;  // VCO multiplier (2.0 to 64.0)
   parameter real CLKOUT0_DIVIDE_F = 10.000;  // Output divider (1.0 to 128.0)
   parameter real CLKIN1_PERIOD    = 10.000;  // Input period in ns (100MHz = 10ns)

   MMCME2_BASE
     #(.CLKIN1_PERIOD   (CLKIN1_PERIOD),
       .CLKFBOUT_MULT_F (CLKFBOUT_MULT_F),
       .CLKOUT0_DIVIDE_F(CLKOUT0_DIVIDE_F),
       .DIVCLK_DIVIDE   (1),                 // Input divider (1-106)
       .STARTUP_WAIT    ("FALSE"))
   mmcm_inst
     (.CLKIN1   (i_clk),
      .RST      (i_rst),
      .CLKOUT0  (o_clk),
      .CLKOUT0B (),
      .CLKOUT1  (),
      .CLKOUT1B (),
      .CLKOUT2  (),
      .CLKOUT2B (),
      .CLKOUT3  (),
      .CLKOUT3B (),
      .CLKOUT4  (),
      .CLKOUT5  (),
      .CLKOUT6  (),
      .LOCKED   (locked),
      .CLKFBOUT (clkfb),
      .CLKFBOUTB(),
      .CLKFBIN  (clkfb),
      .PWRDWN   (1'b0));

   // Generate reset synchronized to output clock
   // Reset is asserted until MMCM locks
   always_ff @(posedge o_clk) begin
      locked_r <= locked;
      o_rst    <= !locked_r;
   end

endmodule
