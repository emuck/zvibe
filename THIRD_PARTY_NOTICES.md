# Third-Party Notices

This file lists every third-party component included in or derived from
in the ZVibe repository, along with the copyright notices and license
terms that must be preserved when distributing source or binaries.

The full text of each open-source license is reproduced in the
[Appendix](#appendix-full-license-texts) below.  The canonical `NOTICE`
file at the repository root also reproduces all required license texts.

---

## Component Summary

| # | Component | Path(s) | License | BSD-3 compatible |
|---|-----------|---------|---------|-----------------|
| 1 | [mojozork](#1-mojozork) | `src/core/`, `src/api/`, `include/` | zlib | ✅ |
| 2 | [SERV CPU](#2-serv-cpu) | `target/riscv/serv_zvibe/serv/` (submodule) | ISC | ✅ |
| 3 | [Servant RAM](#3-servant-ram) | `common/rtl/servant_ram.sv` | ISC | ✅ |
| 4 | [Servile](#4-servile) | `common/rtl/servile_mux.sv` | Apache-2.0 | ✅ |
| 5 | [verilog-uart](#5-verilog-uart) | `common/rtl/uart/uart_tx.sv`, `uart_rx.sv` | MIT | ✅ |
| 6 | [Microchip SAM E51 DFP](#6-microchip-sam-e51-dfp) | `target/same51/packs/ATSAME51J20A_DFP/` | Apache-2.0 | ✅ |
| 7 | [ARM CMSIS Core](#7-arm-cmsis-core) | `target/same51/packs/CMSIS/` | Apache-2.0 | ✅ |
| 8 | [Czech Z-machine test suite](#8-czech-z-machine-test-suite) | `games/catalog/czech.z3` | Public domain | ✅ |
| 9 | [Intel/Altera ALTPLL](#9-intelaltera-altpll-max10-pll-primitive) | *(primitive, no files)* | BSD-3-Clause | ✅ |
| 10 | [QFlexpress (removed)](#10-qflexpress-lgpl-30--removed) | *(not present in repo)* | LGPL-3.0 | ⚠️ |

---

## 1. mojozork

**Description:** Z-machine interpreter; forms the basis of `src/core/`.
ZVibe is a substantial rewrite of mojozork, retaining the original
algorithm structure and Z-machine opcode semantics.

**Author:** Ryan C. Gordon
**Source:** <https://github.com/icculus/mojozork>
**Copyright:** Copyright (c) 2015–2023 Ryan C. Gordon
**License:** zlib License

**Files derived from mojozork:**

```
src/core/zvibe_core.c
src/core/zvibe_ops_base.c
src/core/zvibe_ops_math.c
src/core/zvibe_ops_mem.c
src/core/zvibe_ops_text.c
src/core/zvibe_object.c
src/api/zvibe_api.c
include/zvibe.h
include/zvibe_api.h
include/zvibe_memory.h
```

**Required notice:** Retain the copyright notice in every file listed above
and in LICENSE.  See [Appendix A](#a-zlib-license--mojozork) for the full
zlib license text.

---

## 2. SERV CPU

**Description:** Bit-serial RV32I CPU core.  Included as a Git submodule;
not committed directly to this repository.

**Author:** Olof Kindgren
**Source:** <https://github.com/olofk/serv>
**Copyright:** Copyright (c) 2019 Olof Kindgren
**License:** ISC License
**Location:** `target/riscv/serv_zvibe/serv/` (Git submodule)

**Required notice:** Retain the ISC copyright notice and permission text
found in `target/riscv/serv_zvibe/serv/LICENSE`.  The full ISC text is
also reproduced in [Appendix B](#b-isc-license--serv--servant-ram).

---

## 3. Servant RAM

**Description:** Synchronous BRAM module from the Servant SoC (part of the
SERV project).  Adapted for use in the ZVibe SoC.

**Author:** Olof Kindgren
**Source:** <https://github.com/olofk/serv>
**Copyright:** Copyright (c) Olof Kindgren
**License:** ISC License
**Files:** `target/riscv/serv_zvibe/common/rtl/servant_ram.sv`

**Required notice:** Same as SERV CPU above.  See
[Appendix B](#b-isc-license--serv--servant-ram).

---

## 4. Servile

**Description:** Wishbone bus multiplexer for the Servant SoC convenience
wrapper.

**Author:** Olof Kindgren
**Source:** <https://github.com/olofk/servile>
**Copyright:** Copyright (c) 2024 Olof Kindgren
**License:** Apache License, Version 2.0
**Files:** `target/riscv/serv_zvibe/common/rtl/servile_mux.sv`

**Required notice:** Retain the `SPDX-FileCopyrightText` and
`SPDX-License-Identifier` header in `servile_mux.sv`.  The Apache-2.0
license text is reproduced in [Appendix C](#c-apache-license-version-20).
If you distribute a binary built from this file, you must reproduce the
above copyright notice and the Apache-2.0 license text (or a pointer to
it) in accompanying documentation or a NOTICE file.

---

## 5. verilog-uart

**Description:** UART transmitter and receiver RTL.  The ZVibe integration
adds a Wishbone bus interface (`uart_wb.sv`) and a synchronous FIFO
(`fifo_sync.sv`); those additions are BSD-3-Clause.

**Author:** Alex Forencich
**Source:** <https://github.com/alexforencich/verilog-uart>
**Copyright:** Copyright (c) 2014–2021 Alex Forencich
**License:** MIT License
**Files:**

```
target/riscv/serv_zvibe/common/rtl/uart/uart_tx.sv   (algorithm from verilog-uart)
target/riscv/serv_zvibe/common/rtl/uart/uart_rx.sv   (algorithm from verilog-uart)
```

**Required notice:** Retain the copyright notice and MIT permission text in
each file.  Full text in [Appendix D](#d-mit-license--verilog-uart).

---

## 6. Microchip SAM E51 DFP

**Description:** Device Family Pack headers for the ATSAME51J20A
microcontroller.  Provides peripheral register definitions, pin
mappings, and the top-level device include.

**Author:** Microchip Technology Inc.
**Source:** <https://packs.download.microchip.com/>
**Copyright:** Copyright (c) 2025 Microchip Technology Inc. and its
subsidiaries
**License:** Apache License, Version 2.0
**Location:** `target/same51/packs/ATSAME51J20A_DFP/`

All files in this directory carry the header:

```
SPDX-License-Identifier: Apache-2.0
```

**Required notice:** Retain the `SPDX-License-Identifier` header in each
file.  Apache-2.0 text in [Appendix C](#c-apache-license-version-20).

---

## 7. ARM CMSIS Core

**Description:** Cortex Microcontroller Software Interface Standard (CMSIS)
Core headers for Cortex-M4.

**Author:** Arm Limited
**Source:** <https://github.com/ARM-software/CMSIS_5>
**Copyright:** Copyright (c) 2009–2022 Arm Limited. All rights reserved.
**License:** Apache License, Version 2.0
**Location:** `target/same51/packs/CMSIS/`

**Required notice:** Retain copyright notice in each file.  Apache-2.0
text in [Appendix C](#c-apache-license-version-20).

---

## 8. Czech Z-machine Test Suite

**Description:** `czech.z3` is a Z-machine version 3 compliance test suite
used to validate opcode correctness.

**Author:** Mark Knibbs
**Copyright:** Public domain
**License:** Public domain — no restrictions on use, reproduction, or
distribution.
**Files:**

```
games/catalog/czech.z3
```

No notices are required.  The public-domain status is per the upstream
distribution of the Czech test suite.

---

## 9. Intel/Altera ALTPLL (MAX10 PLL primitive)

**Description:** ALTPLL is a standard MAX10 vendor primitive (analogous to
Xilinx BUFG/MMCM) that maps directly to on-chip PLL silicon.  It is
instantiated directly in the top-level RTL — no separate wrapper or IP
files are present in this repository.

**Files:** *(none — the `altpll` primitive is instantiated directly in
`target/riscv/serv_zvibe/boards/max10_08_eval/rtl/servant_zvibe_max10_08_eval_xip.sv`)*

**License:** BSD-3-Clause (project design code).

No additional notices are required.

---

## 10. QFlexpress (LGPL-3.0) — Removed

**Description:** An alternative QSPI flash XIP controller by Gisselquist
Technology, LLC, originally derived from the ZipCPU project.

**Author:** Dan Gisselquist, Ph.D., Gisselquist Technology, LLC
**Source:** <https://github.com/ZipCPU/qspiflash>
**Copyright:** Copyright (c) 2018–2021 Gisselquist Technology, LLC
**License:** GNU Lesser General Public License, version 3 (LGPL-3.0)

> **Status: The file `qflexpress.v` has been removed from this
> repository.**  It was present in archived testbenches during development
> and was never compiled into any ZVibe build.

> **Important:** LGPL-3.0 is a copyleft license.  If `qflexpress.v` (or
> any LGPL-licensed QSPI controller) were ever compiled into a ZVibe
> binary or FPGA bitstream, LGPL conditions would apply to the linked
> work, which is incompatible with the project's BSD-3-Clause outbound
> license.  **Do not reintroduce this file or any LGPL-licensed module
> into any Makefile, synthesis filelist, or simulation target.**
>
> ZVibe uses `s25fl_xip.sv` (BSD-3-Clause, Martin Raumann) in all active
> builds.

---

## Appendix: Full License Texts

### A. zlib License — mojozork

```
This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
```

Copyright (c) 2015–2023 Ryan C. Gordon

---

### B. ISC License — SERV / Servant RAM

```
ISC License

Copyright (c) 2019 Olof Kindgren

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE
USE OR PERFORMANCE OF THIS SOFTWARE.
```

---

### C. Apache License, Version 2.0

Applies to: Servile (Olof Kindgren), Microchip SAM E51 DFP (Microchip
Technology Inc.), ARM CMSIS Core (Arm Limited).

Full text: <https://www.apache.org/licenses/LICENSE-2.0>

The `NOTICE` file at the repository root reproduces the Apache-2.0 text
in full as required by section 4 of that license.

---

### D. MIT License — verilog-uart

```
MIT License

Copyright (c) 2014-2021 Alex Forencich

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
