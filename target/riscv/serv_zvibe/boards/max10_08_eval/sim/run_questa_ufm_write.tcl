# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
# run_questa_ufm_write.tcl — Questa batch simulation for MAX10 UFM write test
#
# Validates runtime UFM erase/write/verify against the Intel vendor UFM model.
# Firmware (ufm_write_xip_test.c) runs XIP from UFM, erases/writes pages,
# verifies data, prints results over UART.
#
# Usage (from boards/max10_08_eval/sim/):
#   vsim -c -do run_questa_ufm_write.tcl
# Or via Makefile.vsim:
#   make -f Makefile.vsim ufm-write
#
# Prerequisites:
#   - test_ufm_write.dat must exist (built by: make -f Makefile.vsim ufm-write-firmware)
#   - UFM vendor IP simulation files: ../fpga/ip/ufm/simulation/mentor/msim_setup.tcl
#     (generate with: cd ../fpga && make generate-ip)

set QUARTUS_INSTALL_DIR "/mnt/kingston/Altera/quartus"
set QSYS_SIMDIR "../fpga/ip/ufm/simulation"
source "$QSYS_SIMDIR/mentor/msim_setup.tcl"

echo "=== Compiling Altera device libraries ==="
dev_com

echo "=== Compiling vendor UFM IP ==="
com

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
    servant_zvibe_ufm_write_tb.sv

# vendor ufm.v loads "altera_onchip_flash.dat" from cwd — copy our .dat to that name
if {![file exists test_ufm_write.dat]} {
    puts "ERROR: test_ufm_write.dat not found."
    puts "Build with: make -f Makefile.vsim ufm-write-firmware"
    quit -code 1
}
file copy -force test_ufm_write.dat altera_onchip_flash.dat
echo "=== UFM init: test_ufm_write.dat → altera_onchip_flash.dat ==="

echo "=== Elaborating servant_zvibe_ufm_write_tb ==="
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
    work.servant_zvibe_ufm_write_tb

echo "=== Running simulation (max 60ms sim time) ==="
run 60ms

echo "=== Simulation complete at $now ==="
quit -f
