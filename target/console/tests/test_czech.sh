#!/bin/bash
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause

# Czech Z3 Basic Functionality Test Script
# Tests basic Z-machine interpreter functionality using czech.z3

set -e  # Exit on any error

# Configuration
TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$TESTS_DIR/.." && pwd)"
ZVIBE_MINIMAL="$PROJECT_ROOT/build/bin/zvibe_minimal"
GAME_FILE="$TESTS_DIR/../../../games/catalog/czech.z3"
SCRIPT_FILE="$TESTS_DIR/czech-script.txt"
LOG_FILE="$TESTS_DIR/czech_test_output.log"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Step 1: Check if zvibe_minimal exists
check_minimal() {
    if [ ! -f "$ZVIBE_MINIMAL" ]; then
        log_error "zvibe_minimal not found at: $ZVIBE_MINIMAL"
        log_error "Please build the project first"
        exit 1
    fi
    log_info "Found zvibe_minimal"
}

# Step 2: Check game file
check_game() {
    if [ ! -f "$GAME_FILE" ]; then
        log_error "Czech game file not found: $GAME_FILE"
        exit 1
    fi
    log_info "Czech game file found"
}

# Step 3: No script needed for zvibe_minimal (runs automatically)
prepare_test() {
    log_info "Preparing Czech test (no script needed for zvibe_minimal)"
}

# Step 4: Run the test
run_test() {
    log_info "Running Czech Z3 basic functionality test..."
    rm -f "$LOG_FILE"
    
    # Czech runs automatically with zvibe_minimal, just provide quit to exit cleanly
    echo "quit" | "$ZVIBE_MINIMAL" "$GAME_FILE" > "$LOG_FILE" 2>&1
    local exit_code=$?
    
    if [ $exit_code -ne 0 ]; then
        log_error "Game execution failed with exit code $exit_code - see $LOG_FILE"
        return 1
    fi
    
    log_info "Game execution completed"
    return 0
}

# Step 5: Verify basic functionality and check for errors
verify_functionality() {
    log_info "Verifying basic Z-machine functionality and checking for errors..."
    
    local passed=0
    local failed=0
    
    # Test 1: Check for game startup
    if grep -q -i "czech" "$LOG_FILE"; then
        echo -e "${GREEN}PASS:${NC} Game started successfully"
        passed=$((passed + 1))
    else
        echo -e "${RED}FAIL:${NC} Game startup not detected"
        failed=$((failed + 1))
    fi
    
    # Test 2: Check for interpreter errors or crashes (excluding success messages)
    if grep -q -i "error\|segmentation\|abort\|fatal" "$LOG_FILE" && ! grep -q "Didn't crash" "$LOG_FILE"; then
        echo -e "${RED}FAIL:${NC} Interpreter errors detected"
        failed=$((failed + 1))
    else
        echo -e "${GREEN}PASS:${NC} No interpreter errors detected"
        passed=$((passed + 1))
    fi
    
    # Test 3: Check Z-machine opcodes tested
    if grep -q "Jumps.*jump" "$LOG_FILE"; then
        echo -e "${GREEN}PASS:${NC} Jump opcodes tested"
        passed=$((passed + 1))
    else
        echo -e "${RED}FAIL:${NC} Jump opcodes not tested"
        failed=$((failed + 1))
    fi
    
    # Test 4: Check variable operations
    if grep -q "Variables.*push/pull" "$LOG_FILE"; then
        echo -e "${GREEN}PASS:${NC} Variable operations tested"
        passed=$((passed + 1))
    else
        echo -e "${RED}FAIL:${NC} Variable operations not tested"
        failed=$((failed + 1))
    fi
    
    # Test 5: Check arithmetic operations
    if grep -q "Arithmetic ops.*add.*sub" "$LOG_FILE"; then
        echo -e "${GREEN}PASS:${NC} Arithmetic operations tested"
        passed=$((passed + 1))
    else
        echo -e "${RED}FAIL:${NC} Arithmetic operations not tested"
        failed=$((failed + 1))
    fi
    
    # Test 6: Check memory operations
    if grep -q "Memory.*loadw.*loadb" "$LOG_FILE"; then
        echo -e "${GREEN}PASS:${NC} Memory operations tested"
        passed=$((passed + 1))
    else
        echo -e "${RED}FAIL:${NC} Memory operations not tested"
        failed=$((failed + 1))
    fi
    
    # Test 7: Check test completion and results
    if grep -q "Performed.*tests" "$LOG_FILE"; then
        echo -e "${GREEN}PASS:${NC} Test suite completed"
        passed=$((passed + 1))
        
        # Check for test failures in Czech output
        if grep -q "Failed: 0" "$LOG_FILE"; then
            echo -e "${GREEN}PASS:${NC} All Czech tests passed (no failures)"
            passed=$((passed + 1))
        else
            echo -e "${RED}FAIL:${NC} Some Czech tests failed"
            failed=$((failed + 1))
        fi
    else
        echo -e "${RED}FAIL:${NC} Test suite did not complete"
        failed=$((failed + 1))
    fi
    
    # Test 8: Check successful completion message
    if grep -q "Didn't crash: hooray!" "$LOG_FILE"; then
        echo -e "${GREEN}PASS:${NC} Game completed without crashes"
        passed=$((passed + 1))
    else
        echo -e "${RED}FAIL:${NC} Game may have crashed or not completed properly"
        failed=$((failed + 1))
    fi
    
    echo
    echo "=== CZECH TEST SUMMARY ==="
    echo "Tests passed: $passed"
    echo "Tests failed: $failed"
    echo "=========================="
    
    if [ $failed -eq 0 ]; then
        echo -e "${GREEN}All Czech functionality tests PASSED!${NC}"
        return 0
    else
        echo -e "${RED}Some Czech functionality tests FAILED!${NC}"
        echo
        echo "=== LAST 20 LINES OF OUTPUT ==="
        tail -20 "$LOG_FILE"
        echo "==============================="
        return 1
    fi
}

# Cleanup function (no script file to clean up)
cleanup() {
    # No cleanup needed for zvibe_minimal (no script file)
    :
}

# Main execution
main() {
    log_info "Starting Czech Z3 basic functionality test..."
    
    # Set up cleanup trap
    trap cleanup EXIT
    
    check_minimal
    check_game
    prepare_test
    run_test
    verify_functionality
    
    if [ $? -eq 0 ]; then
        exit 0
    else
        exit 1
    fi
}

main "$@"
