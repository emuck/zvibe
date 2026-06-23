///////////////////////////////////////////////////////////////////////////////
// qspi_mux.sv
//
// QSPI Pin Multiplexer
//
// Arbitrates access to QSPI pins between XIP controller (read) and
// Write controller (write/erase operations).
//
// Copyright (c) 2025 Martin R. Raumann
// SPDX-License-Identifier: BSD-3-Clause
//
// Priority: XIP controller has priority - writes wait for reads to complete.
//
// Interface:
// - Selects between XIP and Write controller signals based on o_active
// - Routes QSPI pins (SCK, CS#, DAT, OE) to flash
// - Routes flash input data back to both controllers
//
///////////////////////////////////////////////////////////////////////////////

`timescale 1ns / 1ps

module qspi_mux (
    // Control
    input  logic        i_write_active,  // Write controller has control (from write controller o_active)

    // XIP Controller Signals
    input  logic        i_xip_sck,
    input  logic        i_xip_cs_n,
    input  logic [3:0]  i_xip_dat,
    input  logic [3:0]  i_xip_oe,

    // Write Controller Signals
    input  logic        i_write_sck,
    input  logic        i_write_cs_n,
    input  logic [3:0]  i_write_dat,
    input  logic [3:0]  i_write_oe,

    // QSPI Pins (to flash)
    output logic        o_qspi_sck,
    output logic        o_qspi_cs_n,
    output logic [3:0]  o_qspi_dat,
    output logic [3:0]  o_qspi_oe,
    input  logic [3:0]  i_qspi_dat,

    // Feedback to controllers
    output logic [3:0]  o_xip_dat_in,
    output logic [3:0]  o_write_dat_in
);

    //========================================================================
    // Multiplexer Logic
    //========================================================================
    // When write controller is active, route its signals
    // Otherwise, route XIP controller signals (XIP has priority)

    assign o_qspi_sck  = i_write_active ? i_write_sck  : i_xip_sck;
    assign o_qspi_cs_n = i_write_active ? i_write_cs_n : i_xip_cs_n;
    assign o_qspi_dat  = i_write_active ? i_write_dat  : i_xip_dat;
    assign o_qspi_oe   = i_write_active ? i_write_oe   : i_xip_oe;

    // Route flash input data to both controllers (they can ignore when inactive)
    assign o_xip_dat_in    = i_qspi_dat;
    assign o_write_dat_in  = i_qspi_dat;

endmodule

