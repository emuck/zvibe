/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*******************************************************************************
  smarteeprom.h - the SAM E51's SmartEEPROM (16 KB virtual: SBLK=3, 512-byte pages) as a byte store.

  What the hardware does not do, found on the board, and what this module therefore does:
   - a single-byte store is not persisted: writes are whole 32-bit words, the last partial word padded
     with ones;
   - zero words at the END of a write are not persisted over older non-zero data: the caller ends every
     write with a non-zero word (smarteeprom_write_end_mark);
   - a write is only in the page buffer until smarteeprom_flush() commits it.
*******************************************************************************/

#ifndef SMARTEEPROM_H
#define SMARTEEPROM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* SEE_API is empty: the functions are external. A V3-only firmware defines it as `static` and includes
 * smarteeprom.c into its own file, so that image is exactly what it was before the code had a file of its own. */
#ifndef SEE_API
#define SEE_API
#endif

#define SMARTEEPROM_BASE_ADDR   0x44000000UL
#define SMARTEEPROM_SIZE        16384       /* bytes */
#define SMARTEEPROM_PAGE        512         /* bytes: the unit the hardware commits */
#define SAVE_END_MARK_BYTES     4           /* the non-zero word smarteeprom_write_end_mark adds */

/* Set the fuses (SBLK=3, PSZ=7: the device resets itself once if they are wrong), unlock and configure.
 * False if the SmartEEPROM cannot be used (wrong fuses, locked). */
SEE_API bool smarteeprom_init(void);

/* Whole 32-bit words from `data` at `offset` (a multiple of 4) into the page buffer; the last partial
 * word is padded with ones. Not stored until smarteeprom_flush(). */
SEE_API bool smarteeprom_write_raw(uint32_t offset, const void *data, size_t size);

/* A non-zero word right after data that ended at data_end_offset (see above). */
SEE_API bool smarteeprom_write_end_mark(uint32_t data_end_offset);

/* Commit the page buffer and wait until the SmartEEPROM is done (false if it did not finish in time). */
SEE_API bool smarteeprom_flush(void);

SEE_API bool smarteeprom_read_data(uint32_t offset, void *buffer, size_t size);

#endif /* SMARTEEPROM_H */
