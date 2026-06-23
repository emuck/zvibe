// Copyright (c) 2025 Martin R. Raumann
// SPDX-License-Identifier: BSD-3-Clause

// Wrapper for internal oscillator IP core
// Maps the zvibe_osc interface to the underlying int_osc module

module zvibe_osc (
    input  wire oscena,
    output wire clkout
);

    int_osc int_osc_inst (
        .oscena(oscena),
        .clkout(clkout)
    );

endmodule
