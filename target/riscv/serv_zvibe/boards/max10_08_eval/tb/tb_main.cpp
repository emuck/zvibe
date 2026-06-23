/*
 * Minimal Verilator C++ testbench wrapper
 */

#include <verilated.h>
#include "Vservant_zvibe_ufm_write_tb.h"

#if VM_TRACE
#include <verilated_vcd_c.h>
#endif

vluint64_t main_time = 0;

double sc_time_stamp() {
    return main_time;
}

int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);

    // Create top module
    Vservant_zvibe_ufm_write_tb *top = new Vservant_zvibe_ufm_write_tb;

#if VM_TRACE
    // Enable tracing
    Verilated::traceEverOn(true);
    VerilatedVcdC *tfp = new VerilatedVcdC;
    top->trace(tfp, 99);
    tfp->open("servant_zvibe_ufm_write_tb.vcd");
#endif

    // Run simulation
    while (!Verilated::gotFinish()) {
        top->eval();
#if VM_TRACE
        tfp->dump(main_time);
#endif
        main_time++;
    }

    // Cleanup
#if VM_TRACE
    tfp->close();
#endif
    delete top;

    return 0;
}
