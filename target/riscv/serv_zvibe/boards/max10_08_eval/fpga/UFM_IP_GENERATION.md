# UFM IP Generation Guide

## Overview

The UFM (User Flash Memory) IP simulation models are **NOT checked into git** because they are machine-generated files. Instead, we provide a script to generate them from the `ufm.qsys` file.

## Quick Start

```bash
cd target/riscv/serv_zvibe/boards/max10_08_eval/fpga
./generate_ufm_ip.sh
```

This generates:
- `ip/ufm/simulation/submodules/*.v` - Simulation models (for Questa/ModelSim)
- `ip/ufm/synthesis/submodules/*.v` - Synthesis files (for FPGA builds)

## When to Run

**Run this script:**
- After cloning the repository for the first time
- After pulling changes that modify `ufm.qsys`
- If simulation/synthesis files are missing or out-of-date

## What Gets Generated

```
ip/ufm/
├── simulation/
│   ├── submodules/
│   │   ├── altera_onchip_flash.v
│   │   ├── altera_onchip_flash_avmm_csr_controller.v
│   │   ├── altera_onchip_flash_avmm_data_controller.v
│   │   └── altera_onchip_flash_util.v
│   ├── mentor/msim_setup.tcl
│   ├── aldec/rivierapro_setup.tcl
│   └── synopsys/...
├── synthesis/
│   ├── submodules/
│   │   ├── altera_onchip_flash.v
│   │   ├── altera_onchip_flash_avmm_csr_controller.v
│   │   ├── altera_onchip_flash_avmm_data_controller.v
│   │   ├── altera_onchip_flash_util.v
│   │   └── rtl/altera_onchip_flash_block.v
│   ├── ufm.qip  ← Referenced by build.tcl
│   └── ufm.v
└── ufm.bsf (block symbol file)
```

## Requirements

- **Quartus Prime Lite Edition** installed
- Default path: `/mnt/kingston/Altera/quartus`
- Or set `QSYS_GENERATE` environment variable to custom path

## Troubleshooting

### Error: "qsys-generate not found"

**Problem**: Script can't find qsys-generate tool

**Solution**:
```bash
export QSYS_GENERATE=/path/to/quartus/sopc_builder/bin/qsys-generate
./generate_ufm_ip.sh
```

### Error: "Unknown device part"

**Problem**: Wrong Quartus edition (Pro instead of Lite)

**Solution**: Use Quartus Lite Edition for MAX 10 devices

### Files Already Exist

The script will overwrite existing files. To force regeneration:
```bash
rm -rf ip/ufm/simulation ip/ufm/synthesis
./generate_ufm_ip.sh
```

## Why Not Commit Generated Files?

**Benefits of script-based generation:**
1. **Smaller repository** - Generated files are ~100KB+
2. **No merge conflicts** - Machine-generated code causes spurious diffs
3. **Always fresh** - Regenerate when qsys changes
4. **Standard practice** - Quartus .gitignore excludes simulation/synthesis

**Downsides:**
- Requires Quartus installation to simulate
- Extra step after clone

**Decision**: Script-based generation is standard practice for Quartus IP.

## Integration with Build System

The Makefile does NOT auto-generate IP. You must run the script manually.

**Future enhancement**: Add Makefile target to check if IP exists and auto-run script.

## Technical Details

### qsys-generate Commands Used

**Simulation models:**
```bash
qsys-generate ufm.qsys \
  --simulation=VERILOG \
  --allow-mixed-language-simulation \
  --output-directory=ip/ufm
```

**Synthesis files:**
```bash
qsys-generate ufm.qsys \
  --synthesis=VERILOG \
  --output-directory=ip/ufm
```

**Block symbol:**
```bash
qsys-generate ufm.qsys \
  --block-symbol-file \
  --output-directory=ip/ufm
```

### Device Configuration

- **Family**: MAX 10
- **Part**: 10M08SAE144C8GES
- **Speed Grade**: 8
- Configured automatically from `ufm.qsys`

## See Also

- `ufm.qsys` - IP configuration (checked into git)
- `.gitignore` - Excludes `ip/*/simulation/` and `ip/*/synthesis/`
- `generate_ufm_ip.sh` - Generation script
- `ip/README.md` - General IP core documentation

## Last Updated

2026-02-10 - Initial version after moving to script-based IP generation
