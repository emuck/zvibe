#!/usr/bin/env bash
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
# run_regression.sh — ZVibe RTL simulation regression runner
#
# Usage:
#   ./run_regression.sh [--tier1] [--tier2-questa] [--tier2-xsim] [--all]
#
# Tiers:
#   --tier1          Verilator unit tests (no external tools required)
#   --tier2-questa   Questa vendor-model tests (vsim required)
#   --tier2-xsim     Vivado xsim tests (vivado required)
#   --all            All tiers
#
# Each test log is captured; the script greps for "^PASS:" lines.
# Exit code: 0 = all passed, 1 = one or more failed / timed out.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG_DIR="${SCRIPT_DIR}/regression_logs"

RUN_TIER1=0
RUN_QUESTA=0
RUN_XSIM=0

if [[ $# -eq 0 ]]; then
    echo "Usage: $0 [--tier1] [--tier2-questa] [--tier2-xsim] [--all]"
    exit 1
fi

for arg in "$@"; do
    case "$arg" in
        --tier1)         RUN_TIER1=1 ;;
        --tier2-questa)  RUN_QUESTA=1 ;;
        --tier2-xsim)    RUN_XSIM=1 ;;
        --all)           RUN_TIER1=1; RUN_QUESTA=1; RUN_XSIM=1 ;;
        *) echo "Unknown option: $arg"; exit 1 ;;
    esac
done

mkdir -p "${LOG_DIR}"

PASS_COUNT=0
FAIL_COUNT=0

# ---------------------------------------------------------------------------
# run_test <label> <log_file> <make_dir> <make_target> [make_flags...]
# ---------------------------------------------------------------------------
run_test() {
    local label="$1"
    local log="$2"
    local dir="$3"
    local target="$4"
    shift 4
    local flags=("$@")
    local run_cmd=(make)
    [[ "${TEST_TIMEOUT:-0}" -gt 0 ]] && run_cmd=(timeout "${TEST_TIMEOUT}" make)

    echo -n "  ${label} ... "
    if "${run_cmd[@]}" -C "${dir}" "${target}" "${flags[@]}" > "${log}" 2>&1; then
        if grep -q "^PASS:" "${log}"; then
            echo "PASS"
            PASS_COUNT=$(( PASS_COUNT + 1 ))
        else
            echo "FAIL (no PASS: line in log)"
            FAIL_COUNT=$(( FAIL_COUNT + 1 ))
        fi
    else
        echo "FAIL (make returned non-zero)"
        FAIL_COUNT=$(( FAIL_COUNT + 1 ))
    fi
}

# ---------------------------------------------------------------------------
# Tier 1 — Verilator unit tests
# ---------------------------------------------------------------------------
if [[ ${RUN_TIER1} -eq 1 ]]; then
    echo ""
    echo "========================================"
    echo "Tier 1: Verilator unit tests"
    echo "========================================"

    # UART WB unit test
    run_test "uart_wb_tb" \
        "${LOG_DIR}/uart_wb_tb.log" \
        "${SCRIPT_DIR}/common/tb/unit/uart" \
        "run" -f Makefile.uart

    # Mux unit tests
    run_test "servant_mem_mux_tb" \
        "${LOG_DIR}/servant_mem_mux_tb.log" \
        "${SCRIPT_DIR}/common/tb/unit/mux" \
        "test-mem-mux"

    run_test "servant_zvibe_mux_tb" \
        "${LOG_DIR}/servant_zvibe_mux_tb.log" \
        "${SCRIPT_DIR}/common/tb/unit/mux" \
        "test-zvibe-mux"

    # QSPI XIP WB unit test (single-word reads)
    run_test "s25fl_xip_wb_tb" \
        "${LOG_DIR}/s25fl_xip_wb_tb.log" \
        "${SCRIPT_DIR}/common/tb/unit/flash" \
        "run" -f Makefile.xip_wb

    # QSPI burst unit test (BURST_WORDS=4 — distinct FSM path from single-word XIP)
    run_test "s25fl_xip_burst_tb" \
        "${LOG_DIR}/s25fl_xip_burst_tb.log" \
        "${SCRIPT_DIR}/common/tb/unit/flash" \
        "burst" -f Makefile.xip_wb

    # QSPI BRAM cache unit test (22 sub-tests: miss, hit, eviction, stall, reset)
    run_test "qspi_cache_bram_tb" \
        "${LOG_DIR}/qspi_cache_bram_tb.log" \
        "${SCRIPT_DIR}/common/tb/unit/flash" \
        "cache-bram" -f Makefile.xip_wb

    # UFM behavioral model unit test (erase/write/verify — Tier 1 coverage of MAX10 UFM write path)
    run_test "ufm_model_unit_tb" \
        "${LOG_DIR}/ufm_model_unit_tb.log" \
        "${SCRIPT_DIR}/boards/max10_08_eval/tb" \
        "test-ufm" -f Makefile.ufm_unit_test

    # UFM unified controller unit test (Wishbone→Avalon-MM bridge, 7 test cases)
    run_test "max10_ufm_unified_tb" \
        "${LOG_DIR}/max10_ufm_unified_tb.log" \
        "${SCRIPT_DIR}/boards/max10_08_eval/tb" \
        "test-bridge" -f Makefile.ufm_unit_test

    # MAX10 board-level Verilator TB
    run_test "max10_board_xip_tb" \
        "${LOG_DIR}/max10_board_xip_tb.log" \
        "${SCRIPT_DIR}/boards/max10_08_eval/tb" \
        "run"

    # Arty SoC-level XIP echo (QSPI flash, direct boot)
    run_test "arty_xip_echo" \
        "${LOG_DIR}/arty_xip_echo.log" \
        "${SCRIPT_DIR}/boards/arty_s7_50/tb/xip" \
        "arty-echo" -f Makefile.xip

    # Arty SoC-level XIP echo with 4KB BRAM cache
    run_test "arty_xip_echo_cache" \
        "${LOG_DIR}/arty_xip_echo_cache.log" \
        "${SCRIPT_DIR}/boards/arty_s7_50/tb/xip" \
        "arty-echo-cache" -f Makefile.xip

    # S25FL QSPI flash R/W model unit test
    run_test "s25fl_flash_rw_tb" \
        "${LOG_DIR}/s25fl_flash_rw_tb.log" \
        "${SCRIPT_DIR}/common/tb/unit/flash_rw" \
        "run"

    # Arty SoC flash write integration test (s25fl_write + qspi_mux arbitration)
    run_test "arty_soc_flash_write_tb" \
        "${LOG_DIR}/arty_soc_flash_write_tb.log" \
        "${SCRIPT_DIR}/boards/arty_s7_50/tb/flash_write" \
        "run"
fi

# ---------------------------------------------------------------------------
# Tier 2 — Questa vendor-model tests (vsim required)
# ---------------------------------------------------------------------------
if [[ ${RUN_QUESTA} -eq 1 ]]; then
    echo ""
    echo "========================================"
    echo "Tier 2 (Questa): vendor UFM model tests"
    echo "========================================"

    if ! command -v vsim &>/dev/null; then
        echo "  SKIP: vsim not found in PATH"
    else
        TEST_TIMEOUT=900  # 15-minute wall-clock timeout per Questa test
        # uart-echo XIP test
        run_test "max10_xip_questa_uart_echo" \
            "${LOG_DIR}/max10_xip_questa_uart_echo.log" \
            "${SCRIPT_DIR}/boards/max10_08_eval/sim" \
            "uart-echo" -f Makefile.vsim

        # UFM write/verify test
        run_test "max10_xip_questa_ufm_write" \
            "${LOG_DIR}/max10_xip_questa_ufm_write.log" \
            "${SCRIPT_DIR}/boards/max10_08_eval/sim" \
            "ufm-write" -f Makefile.vsim
    fi
fi

# ---------------------------------------------------------------------------
# Tier 2 — Vivado xsim tests (placeholder — no infrastructure yet)
# ---------------------------------------------------------------------------
if [[ ${RUN_XSIM} -eq 1 ]]; then
    echo ""
    echo "========================================"
    echo "Tier 2 (xsim): Vivado xsim tests"
    echo "========================================"
    echo "  SKIP: xsim simulation infrastructure not yet implemented"
fi

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
echo ""
echo "========================================"
echo "Regression Summary"
echo "========================================"
echo "  Passed: ${PASS_COUNT}"
echo "  Failed: ${FAIL_COUNT}"
echo ""
if [[ "${FAIL_COUNT}" -eq 0 ]]; then
    echo "PASS: all regression tests passed"
    exit 0
else
    echo "FAIL: ${FAIL_COUNT} test(s) failed — check logs in ${LOG_DIR}/"
    exit 1
fi
