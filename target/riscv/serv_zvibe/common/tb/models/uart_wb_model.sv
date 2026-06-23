// Copyright (c) 2025 Martin R. Raumann
// SPDX-License-Identifier: BSD-3-Clause

`default_nettype none
//
// uart_wb_model.sv
//
// Fast Wishbone UART model for simulation
// Bypasses bit-level serialization - captures writes instantly
//

module uart_wb_model #(
    parameter OUTPUT_FILE = "uart_output.txt"
) (
    input  wire  logic        i_clk,
    input  wire  logic        i_reset,

    // Wishbone interface
    input  wire  logic        i_wb_cyc,
    input  wire  logic        i_wb_stb,
    input  wire  logic        i_wb_we,
    input  wire  logic [31:0] i_wb_addr,
    input  wire  logic [31:0] i_wb_data,
    output logic [31:0] o_wb_data,
    output logic        o_wb_ack,

    // RX input interface (for testbench to inject characters)
    input  wire  logic        rx_valid,
    input  wire  logic [7:0]  rx_char
);

    int outfile;
    int char_count = 0;

    // RX FIFO (simple single-entry buffer for now)
    logic [7:0] rx_buffer;
    logic       rx_buffer_valid;

    // Open output file
    initial begin
        outfile = $fopen(OUTPUT_FILE, "w");
        if (outfile == 0) begin
            $display("[UART_MODEL] ERROR: Could not open %s for writing", OUTPUT_FILE);
            $finish;
        end
        $display("[UART_MODEL] Opened %s for UART output", OUTPUT_FILE);
    end

    // RX buffer management
    always_ff @(posedge i_clk) begin
        if (i_reset) begin
            rx_buffer <= 8'h0;
            rx_buffer_valid <= 1'b0;
        end else begin
            // Receive character from testbench
            if (rx_valid && !rx_buffer_valid) begin
                rx_buffer <= rx_char;
                rx_buffer_valid <= 1'b1;
                $display("[UART_MODEL %t] RX: 0x%02h '%c'",
                         $time, rx_char,
                         (rx_char >= 32 && rx_char < 127) ? rx_char : ".");
            end
            // Clear buffer when read
            else if (i_wb_cyc && i_wb_stb && !i_wb_we && i_wb_addr[7:0] == 8'h04) begin
                rx_buffer_valid <= 1'b0;
            end
        end
    end

    // Wishbone transaction handling
    always_ff @(posedge i_clk) begin
        if (i_reset) begin
            o_wb_ack <= 1'b0;
            o_wb_data <= 32'h0;
        end else begin
            o_wb_ack <= 1'b0;  // Default: no ack

            if (i_wb_cyc && i_wb_stb && !o_wb_ack) begin
                o_wb_ack <= 1'b1;  // Acknowledge immediately

                if (i_wb_we) begin
                    // Write to UART TX register (offset 0x00)
                    if (i_wb_addr[7:0] == 8'h00) begin
                        // Write character to file
                        $fwrite(outfile, "%c", i_wb_data[7:0]);
                        $fflush(outfile);
                        char_count = char_count + 1;

                        // Also display for debugging
                        if (i_wb_data[7:0] >= 32 && i_wb_data[7:0] < 127) begin
                            $display("[UART_MODEL %t] TX: 0x%02h '%c' (total: %0d chars)",
                                     $time, i_wb_data[7:0], i_wb_data[7:0], char_count);
                        end else begin
                            $display("[UART_MODEL %t] TX: 0x%02h (total: %0d chars)",
                                     $time, i_wb_data[7:0], char_count);
                        end
                    end
                end else begin
                    // Read from UART registers
                    if (i_wb_addr[7:0] == 8'h04) begin
                        // RX data register (offset 0x04)
                        o_wb_data <= {24'h0, rx_buffer};
                    end else if (i_wb_addr[7:0] == 8'h08) begin
                        // Status register (offset 0x08)
                        // TX_READY=1 (bit 0), RX_READY=rx_buffer_valid (bit 1)
                        o_wb_data <= {30'h0, rx_buffer_valid, 1'b1};
                    end else begin
                        o_wb_data <= 32'h0;
                    end
                end
            end
        end
    end

    // Close file on finish
    final begin
        if (outfile != 0) begin
            $fclose(outfile);
            $display("[UART_MODEL] Closed output file. Total characters: %0d", char_count);
        end
    end

endmodule

`default_nettype wire
