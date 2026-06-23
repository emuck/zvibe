# zVibe Test Suite

This directory contains integration tests for the zVibe Z-machine interpreter using real Z-machine games to validate functionality.

## Test Structure

### Unit Tests
- **`test_split_memory.c`** - Comprehensive unit tests for split memory architecture

### Integration Tests
- **`test_czech.sh`** - Comprehensive Z-machine functionality test using czech.z3
- **`test_zvibe_lib.py`** - Python-based automated game testing using shared library

## Test Files

### `test_czech.sh`
Comprehensive Z-machine functionality test using the czech.z3 test file with `zvibe_minimal`.

**What it tests:**
- **Game startup and initialization**
- **Interpreter error detection**
- **Z-machine opcode testing** (jumps, variables, arithmetic, memory operations)
- **Automated test suite execution**
- **Game completion without crashes**
- **Czech test suite results validation**

**Test coverage:**
- 9 individual functionality checks
- Validates core Z-machine interpreter operations
- Tests all major opcode categories automatically
- Verifies interpreter handles test suite without errors
- No script input required - Czech runs its tests automatically


### `test_split_memory.c`
Comprehensive unit tests for the split memory architecture.

**What it tests:**
- **Memory initialization** - RAM and Flash region setup
- **Memory validation** - Address bounds checking and write protection
- **Byte operations** - Reading from RAM/Flash, writing to RAM
- **Word operations** - 16-bit access across memory regions
- **Stack operations** - Push/pop/peek with overflow protection
- **Pointer access** - Direct memory pointers with safety checks
- **Contiguous memory** - Seamless access across RAM/Flash boundary
- **Memory statistics** - RAM and stack usage tracking

**Coverage:**
- 125+ individual test assertions
- All memory system operations validated
- Cross-boundary access verification
- Error condition testing

### `test_zvibe_lib.py`
Python-based automated game testing using the shared library (.so) interface.

**What it tests:**
- **Shared library integration** - Tests the libzvibe.so library
- **Python ctypes interface** - Validates the Python-to-C library binding
- **Automated game completion** - Runs complete games with pre-written scripts
- **Cross-platform library loading** - Works with .so (Linux), .dylib (macOS), .dll (Windows)
- **Game-specific validation** - Checks for game completion markers

**Features:**
- **Automated game testing** with pre-written command scripts
- **Game completion verification** using configurable success markers
- **Dynamic game file downloads** for testing convenience
- **Flexible library selection** with default path support

**Usage:**
```bash
# Use default library (../build/lib/libzvibe.so)
python3 test_zvibe_lib.py

# Specify custom library
python3 test_zvibe_lib.py /path/to/libzvibe.so
```

## Building and Running Tests

### Prerequisites
- GCC compiler (for building zVibe binaries and shared libraries)
- Make (for automated building and testing)
- wget or curl (for downloading game files)
- Python 3 (for Python-based tests)

### Quick Start
```bash
# From the zVibe root directory
cd target/console/tests

# Run all tests (unit and integration)
make test

# Run unit tests only
make test-unit

# Run integration tests only
make test-integration

# Run specific tests
make test-split-memory
make test-czech

# Get help with available targets
make help
```

### Test Targets

#### Main Targets
- `make test` - Run all unit and integration tests
- `make test-unit` - Run unit tests only
- `make test-integration` - Run integration tests only
- `make clean` - Remove all build artifacts

#### Individual Tests
- `make test-split-memory` - Run split memory unit tests
- `make test-czech` - Run Czech Z3 functionality test (uses `zvibe_minimal`)
- `make test-restaurant` - Run Restaurant game-completion test (game ships with repo)
- `make test-plundered` - Run Plundered Hearts test (downloads game file)
- `make test-seastalker` - Run Seastalker test (downloads game file)

#### Analysis Targets
- `make coverage` - Run unit tests with coverage analysis
- `make debug` - Build unit tests with debug symbols
- `make memcheck` - Run unit tests with valgrind memory checking
- `make profile` - Run unit tests with performance profiling

### Manual Build and Run
```bash
# Unit tests
./test_split_memory                 # Run split memory unit tests

# Integration tests (requires zvibe binaries to be built)
./test_czech.sh                     # Uses zvibe_minimal - runs automatically
```

### Expected Output

```
=== Running Integration Tests ===

Running Czech Z3 functionality test...
[INFO] Starting Czech Z3 basic functionality test...
[INFO] Found zvibe_minimal
[INFO] Czech game file found
[INFO] Preparing Czech test (no script needed for zvibe_minimal)
[INFO] Running Czech Z3 basic functionality test...
[INFO] Game execution completed
[INFO] Verifying basic Z-machine functionality and checking for errors...
PASS: Game started successfully
PASS: No interpreter errors detected
PASS: Jump opcodes tested
PASS: Variable operations tested
PASS: Arithmetic operations tested
PASS: Memory operations tested
PASS: Test suite completed
PASS: All Czech tests passed (no failures)
PASS: Game completed without crashes

=== CZECH TEST SUMMARY ===
Tests passed: 9
Tests failed: 0
All Czech functionality tests PASSED!


=== Integration Tests Complete ===
```

## Test Architecture

### Test Binaries and Libraries
- **test_split_memory**: Unit test executable for split memory architecture
- **../build/bin/zvibe_minimal**: Used for Czech test - minimal interpreter, automatic execution
- **../build/bin/zvibe_console**: Full console with script support for interactive testing
- **../build/lib/libzvibe.so**: Full-featured shared library used by Python tests - includes save/restore and status line support

### Test Games
- **czech.z3**: Comprehensive Z-machine opcode test suite (368 tests)
- **Various Z3 games**: Auto-downloaded for integration testing

### Test Execution
- **Czech test**: Runs automatically, no user input required
- **Python tests**: Use shared library via ctypes, automated script execution with game files

## Debugging Failed Tests

If tests fail:

1. **Check binaries**: Ensure `zvibe_minimal` is built (`make minimal` from `target/console/`)
2. **Check game files**: Verify `games/catalog/czech.z3` exists
3. **Review log files**: Check `czech_test_output.log` for detailed output
4. **Test manually**:
   - Unit tests: `./test_split_memory`
   - Czech: `./test_czech.sh`

Common issues:
- **Build failures**: `zvibe_minimal` not built or not found
- **Czech test**: Interpreter errors or opcode test failures reported in the log
- **Unit tests**: Compilation errors, missing headers, or test assertion failures

## Log Files

Each test creates its own log file:
- **czech_test_output.log**: Czech functionality test output
- **test_output.log**: Integration test output

## Contributing

### Adding Unit Tests

Unit tests are located in the tests directory and use a simple test framework with macros:

- Add new test functions following the `test_*` naming pattern
- Use `TEST_ASSERT(condition)` macro for assertions
- Use `TEST_SECTION("name")` for test organization
- Add new test functions to the main() function in test_split_memory.c
- Use the Makefile analysis targets for debugging:
  - `make coverage` - Generate code coverage reports
  - `make debug` - Build with debug symbols
  - `make memcheck` - Run with valgrind
  - `make profile` - Generate performance profiles

### Adding Integration Tests

When adding new integration tests:

1. Create a new shell script following the pattern of existing tests
2. Add the script to the Makefile integration targets
3. Add a specific target (e.g., `test-newgame`) to the Makefile
4. Update this README with test descriptions
5. Ensure proper cleanup of temporary files and game downloads
6. Test both success and failure scenarios

### Adding Games to Python Tests

To add a new game to `test_zvibe_lib.py`:

1. Update the `GAME_CONFIG` dictionary with the new game details
2. Add completion markers for the new game
3. Create a corresponding script file with the complete walkthrough
4. Test the new game to verify completion detection works

### Test Structure Guidelines

- **Integration tests**: Test end-to-end functionality with real Z-machine games
- **Validation**: Include both positive and negative test cases  
- **Cleanup**: Always clean up temporary files and resources
- **Documentation**: Update README when adding new tests
- **Binary selection**: Use `zvibe_minimal` for automatic tests, `zvibe_console` for interactive tests
- **Game selection**: Use publicly available games that respect copyright
