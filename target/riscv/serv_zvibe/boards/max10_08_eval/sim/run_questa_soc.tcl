# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
# run_questa_soc.tcl — Self-contained Questa batch simulation for MAX10 XIP SoC
#
# Usage (from boards/max10_08_eval/sim/):
#   vsim -c -do run_questa_soc.tcl
# Or via Makefile.vsim:
#   make -f Makefile.vsim questa-batch
#
# Prerequisites:
#   - Run ../fpga/generate_ufm_ip.sh to create fpga/ip/ufm/simulation/
#   - ufm.v must have INIT_FILENAME_SIM = "altera_onchip_flash.dat"
#   - altera_onchip_flash.dat must exist in this directory:
#     riscv64-unknown-elf-objcopy -O verilog --verilog-data-width=4 \
#       --change-addresses=-0x80000000 firmware.elf altera_onchip_flash.dat

# Set Quartus install dir (for device sim libraries)
set QUARTUS_INSTALL_DIR "/mnt/kingston/Altera/quartus"

# Source the UFM IP setup script (remaps 'work' to ./libraries/work/)
set QSYS_SIMDIR "../fpga/ip/ufm/simulation"
source "$QSYS_SIMDIR/mentor/msim_setup.tcl"

# Compile Altera device libraries (altera_ver, fiftyfivenm_ver, etc.)
echo "=== Compiling Altera device libraries ==="
dev_com

# Compile vendor UFM IP into onchip_flash_0 library
echo "=== Compiling vendor UFM IP ==="
com

# Compile our RTL + TB into 'work' (now mapped to ./libraries/work/)
echo "=== Compiling SERV CPU ==="
vlog -sv +define+SIMULATION \
    ../../../serv/rtl/serv_top.v \
    ../../../serv/rtl/serv_state.v \
    ../../../serv/rtl/serv_decode.v \
    ../../../serv/rtl/serv_ctrl.v \
    ../../../serv/rtl/serv_alu.v \
    ../../../serv/rtl/serv_rf_top.v \
    ../../../serv/rtl/serv_rf_if.v \
    ../../../serv/rtl/serv_mem_if.v \
    ../../../serv/rtl/serv_csr.v \
    ../../../serv/rtl/serv_rf_ram_if.v \
    ../../../serv/rtl/serv_rf_ram.v \
    ../../../serv/rtl/serv_immdec.v \
    ../../../serv/rtl/serv_bufreg.v \
    ../../../serv/rtl/serv_bufreg2.v \
    ../../../serv/rtl/serv_compdec.v \
    ../../../serv/servile/servile.v \
    ../../../serv/servile/servile_arbiter.v \
    ../../../serv/servant/servant_timer.v

echo "=== Compiling common RTL ==="
vlog -sv +define+SIMULATION \
    +incdir+../../../common/rtl \
    +incdir+../../../common/rtl/ufm \
    ../../../common/rtl/servant_zvibe.sv \
    ../../../common/rtl/servant_zvibe_mux.sv \
    ../../../common/rtl/servant_mem_mux.sv \
    ../../../common/rtl/servile_mux.sv \
    ../../../common/rtl/servant_ram.sv \
    ../../../common/rtl/gpio_leds.sv \
    ../../../common/rtl/uart/uart_wb.sv \
    ../../../common/rtl/uart/uart_tx.sv \
    ../../../common/rtl/uart/uart_rx.sv \
    ../../../common/rtl/uart/fifo_sync.sv \
    ../../../common/rtl/qspi/wb_ram_flash_mimic.sv \
    ../../../common/rtl/ufm/max10_ufm_unified.sv

echo "=== Compiling board RTL and simulation models ==="
vlog -sv +define+SIMULATION +define+FAST_UART=1 \
    +incdir+../rtl \
    ../rtl/servant_zvibe_max10_08_eval_xip.sv \
    altpll_model.sv \
    ../../../common/tb/models/uart_wb_model.sv \
    max10_xip_tb.sv

# Elaborate and run
echo "=== Elaborating max10_xip_tb ==="
vsim -voptargs=+acc -t ps \
    -L work \
    -L work_lib \
    -L onchip_flash_0 \
    -L altera_ver \
    -L lpm_ver \
    -L sgate_ver \
    -L altera_mf_ver \
    -L altera_lnsim_ver \
    -L fiftyfivenm_ver \
    work.max10_xip_tb

echo "=== Running simulation (max 15ms sim time) ==="
run 15ms

echo "=== Simulation complete at $now ==="
quit -f
