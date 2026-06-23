##
## Arty S7-50 Constraints for Servant ZVibe
##

## Clock (100MHz DDR3 Clock)
## Arty S7-50 uses R2 for 100MHz clock (NOT E3 like Arty A7!)
set_property -dict {PACKAGE_PIN R2 IOSTANDARD SSTL135} [get_ports clk_100mhz]
create_clock -period 10.000 -name clk_100mhz [get_ports clk_100mhz]

## Generated Clock from MMCM
## Vivado automatically derives the generated clock from the MMCM primitive
## based on CLKFBOUT_MULT_F and CLKOUT0_DIVIDE_F parameters
## Current configuration: 100MHz × (10.0 / 6.0) = 166.66MHz (6.0ns period)
## No manual constraint needed - Vivado handles this automatically

## Asynchronous Clock Domain Crossings
## Constrain the 2-FF synchronizers for reset signals crossing from clk_100mhz to sys_clk
## Use max_delay = 1.0 * destination_period for proper synchronizer timing
set_max_delay -datapath_only -from [get_pins por_reset_reg/C] -to [get_pins por_reset_sync1_reg/D] 6.0
set_max_delay -datapath_only -from [get_pins {btn_debounce_reg[2]/C}] -to [get_pins btn_reset_sync1_reg/D] 6.0

## MMCM reset false path
## o_rst deasserts exactly once after MMCM lock — timing analysis is meaningless.
## High fanout to SERV register file reset pins causes false violations at 166MHz.
set_false_path -from [get_pins clock_gen/o_rst_reg/C]

## Reset Button (BTN0)
## Arty S7-50 uses G15 for BTN0 (NOT C2 like Arty A7!)
## Debounced in RTL and used to create reset/reset_n signals
set_property -dict {PACKAGE_PIN G15 IOSTANDARD LVCMOS33} [get_ports btn0]

## UART (USB-UART Bridge - FTDI FT2232HQ)
## R12 = FPGA OUTPUT (uart_txd) - transmit to PC
## V12 = FPGA INPUT (uart_rxd) - receive from PC
set_property -dict {PACKAGE_PIN V12 IOSTANDARD LVCMOS33} [get_ports uart_rxd]
set_property -dict {PACKAGE_PIN R12 IOSTANDARD LVCMOS33} [get_ports uart_txd]

## LEDs (LD2-LD5)
## LD2: Heartbeat (clock is running)
## LD3: GPIO - Boot stub running in RAM
## LD4: GPIO - XIP code running from flash
## LD5: GPIO - Additional debug
set_property -dict {PACKAGE_PIN E18 IOSTANDARD LVCMOS33} [get_ports heartbeat_led]
set_property -dict {PACKAGE_PIN F13 IOSTANDARD LVCMOS33} [get_ports {gpio_led[0]}]
set_property -dict {PACKAGE_PIN E13 IOSTANDARD LVCMOS33} [get_ports {gpio_led[1]}]
set_property -dict {PACKAGE_PIN H15 IOSTANDARD LVCMOS33} [get_ports {gpio_led[2]}]

## QSPI Flash (for XIP - execute in place)
## Note: qspi_clk uses STARTUPE2 primitive, not a regular pin constraint
set_property -dict {PACKAGE_PIN M13 IOSTANDARD LVCMOS33} [get_ports qspi_cs_n]
set_property -dict {PACKAGE_PIN K17 IOSTANDARD LVCMOS33} [get_ports {qspi_d[0]}]
set_property -dict {PACKAGE_PIN K18 IOSTANDARD LVCMOS33} [get_ports {qspi_d[1]}]
set_property -dict {PACKAGE_PIN L14 IOSTANDARD LVCMOS33} [get_ports {qspi_d[2]}]
set_property -dict {PACKAGE_PIN M15 IOSTANDARD LVCMOS33} [get_ports {qspi_d[3]}]

## QSPI Timing Constraints
## CLK_DIV=2: qspi_clk_enable fires every 6 sys_clk cycles (166.66MHz/6 = 27.78MHz).
## SCK = qspi_clk_enable/2 = 13.89MHz (72ns period), routed via STARTUPE2.
## S25FL128S specs @ 13.89MHz: tCLQV=7ns max, tDSU=2ns min, tDH=2ns min
##
## Data changes every 6 sys_clk cycles (one qspi_clk_enable period).
## Multicycle -setup 6 / -hold 5 correctly models this.

# Output delays (FPGA → Flash): Account for flash setup/hold requirements
# Relaxed: Flash samples on QSPI clock (37.5ns period), not sys_clk (6.25ns period)
# Max delay: 3ns (board traces + flash setup time)
# Min delay: 0ns (flash hold time is met by FPGA output delay)
set_output_delay -clock sys_clk -max 3.0 [get_ports qspi_cs_n]
set_output_delay -clock sys_clk -min 0.0 [get_ports qspi_cs_n]
set_output_delay -clock sys_clk -max 3.0 [get_ports {qspi_d[*]}]
set_output_delay -clock sys_clk -min 0.0 [get_ports {qspi_d[*]}]

# Input delays (Flash → FPGA): Account for flash clock-to-output delay
# Max delay: 9ns (tCLQV=7ns + board traces=1-2ns)
# Min delay: 2ns (minimum flash output delay)
set_input_delay -clock sys_clk -max 9.0 [get_ports {qspi_d[*]}]
set_input_delay -clock sys_clk -min 2.0 [get_ports {qspi_d[*]}]

# Multicycle paths: QSPI interface operates at sys_clk/6 = 16.67MHz
# Data has 6 system clock cycles to propagate
set_multicycle_path -setup 6 -to [get_ports qspi_cs_n]
set_multicycle_path -setup 6 -to [get_ports {qspi_d[*]}]
set_multicycle_path -hold 5 -to [get_ports qspi_cs_n]
set_multicycle_path -hold 5 -to [get_ports {qspi_d[*]}]

# Multicycle paths for input (flash → FPGA)
set_multicycle_path -setup 6 -from [get_ports {qspi_d[*]}]
set_multicycle_path -hold 5 -from [get_ports {qspi_d[*]}]

## Configuration
set_property CONFIG_VOLTAGE 3.3 [current_design]
set_property CFGBVS VCCO [current_design]

## Bitstream options
set_property BITSTREAM.GENERAL.COMPRESS TRUE [current_design]
set_property BITSTREAM.CONFIG.CONFIGRATE 33 [current_design]
set_property CONFIG_MODE SPIx4 [current_design]
