/*
 * servant_ram.sv : RAM module for Servant SoC
 *
 * From SERV Servant SoC by Olof Kindgren (ISC License)
 * See: https://github.com/olofk/serv
 *
 * Modifications (if any) Copyright (c) 2025 Martin R. Raumann
 * SPDX-License-Identifier: BSD-3-Clause
 */

module servant_ram
  #(//Memory parameters
    parameter depth = 256,
    parameter aw    = $clog2(depth),
    parameter RESET_STRATEGY = "",
    parameter memfile = "")
   (input  logic           i_wb_clk,
    input  logic           i_wb_rst,
    input  logic [aw-1:2]  i_wb_adr,  // Parameterized address width
    input  logic [31:0]    i_wb_dat,
    input  logic [3:0]     i_wb_sel,
    input  logic           i_wb_we,
    input  logic           i_wb_cyc,
    output logic [31:0]    o_wb_rdt,
    output logic           o_wb_ack);

   logic [3:0]     we;
   assign we = {4{i_wb_we & i_wb_cyc}} & i_wb_sel;

   logic [31:0]    mem [0:depth/4-1] /* verilator public */;

   logic [aw-3:0]  addr;
   assign addr = i_wb_adr;  // Port already has correct bits [aw-1:2]

   always_ff @(posedge i_wb_clk)
     if (i_wb_rst & (RESET_STRATEGY != "NONE"))
       o_wb_ack <= 1'b0;
     else
       o_wb_ack <= i_wb_cyc & !o_wb_ack;

   always_ff @(posedge i_wb_clk) begin
      if (we[0]) mem[addr][7:0]   <= i_wb_dat[7:0];
      if (we[1]) mem[addr][15:8]  <= i_wb_dat[15:8];
      if (we[2]) mem[addr][23:16] <= i_wb_dat[23:16];
      if (we[3]) mem[addr][31:24] <= i_wb_dat[31:24];
      o_wb_rdt <= mem[addr];
   end

   initial
     if(|memfile) begin
`ifndef ISE
`ifndef CCGM
	$display("Preloading %m from %s", memfile);
`endif
`endif
	$readmemh(memfile, mem);
     end

endmodule
