# Development

Coding standards and repository conventions.

## Code Style

### C Code

- Indent: 4 spaces (no tabs)
- Braces: Opening brace on same line
- Line length: 100 characters preferred, 120 maximum
- Comments: `/* */` for blocks, `//` for inline

```c
static void example_function(int param) {
    if (param > 0) {
        do_something();
    }
}
```

### Naming Conventions

| Type | Convention | Example |
|------|------------|---------|
| Functions | `snake_case` | `zmem_read_byte()` |
| Types | `CamelCase` with suffix | `zvibeContext`, `zmem_state_t` |
| Macros | `UPPER_SNAKE_CASE` | `ZVIBE_MEMORY_BUFFER_SIZE` |
| Local variables | `snake_case` | `byte_count` |
| Global state | Single letter | `G` (global state pointer) |

### Python Code

- Follow PEP 8
- Indent: 4 spaces
- Docstrings for public functions

## File Organization

### Source Files

```
src/
├── core/       # Platform-independent interpreter
│   └── zvibe_*.c
└── api/        # Public API
    └── zvibe_api.c
```

### Target Files

```
target/<name>/
├── README.md           # Target documentation
├── Makefile           # Build system
└── <implementation>   # Target-specific code
```

## Adding Code

### New Opcodes

1. Add handler to appropriate `zvibe_ops_*.c`
2. Register in opcode table in `zvibe_core.c`
3. Add test case if possible

### New Targets

1. Create `target/<name>/` directory
2. Implement entry point with callbacks
3. Create Makefile
4. Add README.md with build instructions
5. Update `docs/TARGETS.md`

## Commit Messages

Format:
```
<component>: <brief description>

<detailed explanation if needed>

Co-Authored-By: <if applicable>
```

Examples:
```
zvibe_memory: Fix stack overflow check

The previous check used >= instead of >, allowing one extra
word to be pushed past the limit.
```

```
target/riscv: Add delta compression for saves

Reduces save size by storing only differences from original
game data. Typical save is now 500-1500 bytes instead of 12KB.
```

## Build System

### Console/Windows

GNU Make with standard targets:
- `make all` - Build everything
- `make clean` - Remove build artifacts
- `make test` - Run tests (where applicable)

### RISC-V FPGA

TCL scripts (Quartus or Vivado) invoked via Make:
- `make build` - Synthesize and implement
- `make program` - Program FPGA SRAM
- `make program-complete` - Build firmware + program bitstream + flash (both boards)

Board-specific notes:
- MAX10: uses Quartus (`quartus_sh`, `quartus_pgm`)
- Arty S7-50: uses Vivado + openFPGALoader

### Dependencies

All targets should build with:
- No internet connection (after initial clone)
- Standard toolchains listed in INSTALL.md
- No proprietary tools except Vivado (Arty) or Quartus (MAX10) for FPGA

## Testing

### Before Committing

```bash
make validate   # Console build + unit tests + czech suite
```

Or individually:
1. `cd target/console && make clean && make`
2. `cd target/console/tests && make test-unit && make test-czech`
3. `cd target/riscv/serv_zvibe && ./run_regression.sh --tier1`

### For RISC-V Changes

1. Run simulation: `cd target/riscv/serv_zvibe && ./run_regression.sh --tier1`
2. Build bitstream: `cd target/riscv/serv_zvibe/boards/max10_08_eval/fpga && make build`
3. Test on hardware if available

### For SAM E51 Changes

1. Build multi-game: `cd games && ./build_games.py same51 && cd ../target/same51 && make`
2. Build single-game: `make clean && make STORY_FILE=../../games/catalog/czech.z3`
3. Test on hardware if available

## Documentation

### Requirements

- Every target must have a README.md
- Public APIs must have header documentation
- Non-obvious code must have comments explaining why

### Doxygen Standards

All C code follows the conventions in `docs/DOCUMENTATION_STANDARD.md`:

- File headers: `@file`, `@brief`, `@ingroup`
- Functions: `@brief`, `@param[in/out/in,out]`, `@return`
- Structures: Document each field with `/**< description */`
- Macros: Document with `@brief` on the `#define` line
- Error codes: Document all possible return values

See `docs/groups.dox` for module organization.

### Building API Documentation

Generate HTML documentation and LaTeX source:

```bash
make docs
```

Outputs:
- `docs/html/index.html` - HTML documentation (browse with any browser)
- `docs/latex/refman.tex` - LaTeX source

The equivalent direct Doxygen command is:

```bash
cd docs
doxygen Doxyfile
```

Graph generation requires GraphViz's `dot` command on `PATH`.

To build the optional PDF from the generated LaTeX source, install a TeX
distribution that provides `pdflatex`, then run:

```bash
cd docs/latex
make
```

The PDF output is `docs/latex/refman.pdf`.

Configuration:
- `docs/Doxyfile` - Doxygen configuration
- `docs/groups.dox` - Module group definitions
- `docs/ARCHITECTURE.md` - Used as main page

Build profiles:
- Default: Full-featured desktop build (SAVE_RESTORE=1, STATUS_LINE=1)
- Minimal: Edit Doxyfile PREDEFINED for embedded build documentation

### Markdown Style

Documentation standards:
- Technical, matter-of-fact tone
- No emoji or hype language
- Active voice, present tense
- Concrete claims over adjectives

## Version Control

### Branches

- `main` - Stable, tested code
- Feature branches for development
- Merge via pull request for significant changes

### Submodules

SERV CPU is a submodule at `target/riscv/serv_zvibe/serv/`. Do not commit changes to submodule content.

### Ignored Files

See `.gitignore`. Key exclusions:
- Build artifacts (`*.o`, `*.bin`, `*.bit`)
- Simulation outputs (`*.vcd`, `*.wdb`)
- Downloaded games (`games/catalog/*.z3` except czech.z3)
- Generated code (`*_data.c`, `game_registry.c`)
