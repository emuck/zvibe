# Timing Constraints for MAX10 08 Evaluation Board
# Servant ZVibe SoC

# Clock definitions
# External 50 MHz input → PLL → 100 MHz system clock (all logic)
# Heartbeat LED counter runs on sys_clk (no separate oscillator IP)

# External 50 MHz input clock (CLK0p)
create_clock -period 20.000 -name clk_50mhz [get_ports {clk_50mhz}]

# Derive PLL clocks automatically
# Direct altpll instantiation → derived clock: pll_inst|auto_generated|pll1|clk[0]
derive_pll_clocks

# Clock uncertainty (jitter + skew)
derive_clock_uncertainty

# Input/output delays
# UART timing: Very relaxed - UART runs at 115200 baud (8.68us per bit)
# Much slower than our 100MHz clock (10ns period), so timing is not critical
set_false_path -from [get_ports {uart_rxd}]
set_false_path -to [get_ports {uart_txd}]

# False paths
# All LEDs are for debug/visual feedback - no tight timing required
set_false_path -to [get_ports {gpio_led[*]}]

# Power-on reset counter is relaxed - no tight timing needed
set_false_path -from [get_registers {por_counter[*]}]
set_false_path -from [get_registers {por_reset}]

# DIP Switch 1 is asynchronous (connected to PLL areset)
# Note: SW1 button (DEV_CLRN) handles normal device resets automatically
set_false_path -from [get_ports {dip_sw1}]

# Heartbeat counter: false path (purely cosmetic, visual-rate counter)
set_false_path -from [get_registers {heartbeat_counter[*]}]

# Multicycle paths
# Peripheral read data path: UART/GPIO/Timer → CPU can take 2 cycles
# This relaxes the critical combinatorial path through the wishbone mux
set_multicycle_path -from [get_registers {*uart*|*fifo*|wr_ptr*}] -to [get_registers {*cpu*|*bufreg*}] -setup 2
set_multicycle_path -from [get_registers {*uart*|*fifo*|wr_ptr*}] -to [get_registers {*cpu*|*bufreg*}] -hold 1
