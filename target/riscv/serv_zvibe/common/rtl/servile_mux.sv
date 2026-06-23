/*
 * servile_mux.sv : Simple Wishbone mux for the servile convenience wrapper.
 *
 * SPDX-FileCopyrightText: 2024 Olof Kindgren <olof.kindgren@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */


module servile_mux
  #(parameter [0:0]  sim = 1'b0, //Enable simulation features
    parameter [31:0] sim_sig_adr  = 32'h80000000,
    parameter [31:0] sim_halt_adr = 32'h90000000)
   (
    input  logic        i_clk,
    input  logic        i_rst,

    input  logic [31:0] i_wb_cpu_adr,
    input  logic [31:0] i_wb_cpu_dat,
    input  logic [3:0]  i_wb_cpu_sel,
    input  logic        i_wb_cpu_we,
    input  logic        i_wb_cpu_stb,
    output logic [31:0] o_wb_cpu_rdt,
    output logic        o_wb_cpu_ack,

    output logic [31:0] o_wb_mem_adr,
    output logic [31:0] o_wb_mem_dat,
    output logic [3:0]  o_wb_mem_sel,
    output logic        o_wb_mem_we,
    output logic        o_wb_mem_stb,
    input  logic [31:0] i_wb_mem_rdt,
    input  logic        i_wb_mem_ack,

    output logic [31:0] o_wb_ext_adr,
    output logic [31:0] o_wb_ext_dat,
    output logic [3:0]  o_wb_ext_sel,
    output logic        o_wb_ext_we,
    output logic        o_wb_ext_stb,
    input  logic [31:0] i_wb_ext_rdt,
    input  logic        i_wb_ext_ack);

   logic        sig_en;
   logic        halt_en;
   logic        sim_ack;

   // Modified address decoding for XIP:
   // 00 (0x00000000-0x3FFFFFFF) → MEM (RAM)
   // 01 (0x40000000-0x7FFFFFFF) → EXT (Peripherals: UART, Timer)
   // 10 (0x80000000-0xBFFFFFFF) → MEM (Flash for XIP)
   // 11 (0xC0000000-0xFFFFFFFF) → EXT
   logic        ext;
   assign ext = (i_wb_cpu_adr[31:30] == 2'b01) | (i_wb_cpu_adr[31:30] == 2'b11);

   assign o_wb_cpu_rdt = ext ? i_wb_ext_rdt : i_wb_mem_rdt;
   assign o_wb_cpu_ack = i_wb_ext_ack | i_wb_mem_ack | sim_ack;

   assign o_wb_mem_adr = i_wb_cpu_adr;
   assign o_wb_mem_dat = i_wb_cpu_dat;
   assign o_wb_mem_sel = i_wb_cpu_sel;
   assign o_wb_mem_we  = i_wb_cpu_we;
   assign o_wb_mem_stb = i_wb_cpu_stb & !ext & !(sig_en|halt_en);

   assign o_wb_ext_adr = i_wb_cpu_adr;
   assign o_wb_ext_dat = i_wb_cpu_dat;
   assign o_wb_ext_sel = i_wb_cpu_sel;
   assign o_wb_ext_we  = i_wb_cpu_we;
   assign o_wb_ext_stb = i_wb_cpu_stb & ext & !(sig_en|halt_en);

   generate
      if (sim) begin

	 integer      f = 0;

	 assign sig_en  = |f & i_wb_cpu_we & (i_wb_cpu_adr == sim_sig_adr);
	 assign halt_en = i_wb_cpu_we & (i_wb_cpu_adr == sim_halt_adr);

	 reg [1023:0] signature_file;

	 initial
	   /* verilator lint_off WIDTH */
	   if ($value$plusargs("signature=%s", signature_file)) begin
	      $display("Writing signature to %0s", signature_file);
	      f = $fopen(signature_file, "w");
	   end
	 /* verilator lint_on WIDTH */

	 always_ff @(posedge i_clk) begin
	    sim_ack <= 1'b0;
	    if (i_wb_cpu_stb & !sim_ack) begin
	       sim_ack <= sig_en|halt_en;
	       if (sig_en & (f != 0))
		 $fwrite(f, "%c", i_wb_cpu_dat[7:0]);
	       else if(halt_en) begin
		  $display("Test complete");
		  $finish;
	       end
	    end
	    if (i_rst)
	      sim_ack <= 1'b0;
	 end
      end else begin
	 assign sig_en  = 1'b0;
	 assign halt_en = 1'b0;
	 initial sim_ack = 1'b0;
      end
   endgenerate

endmodule

