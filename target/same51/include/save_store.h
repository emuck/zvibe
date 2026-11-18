/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*******************************************************************************
  save_store.h - saved games in the SmartEEPROM that survive an interrupted save.

  Layout: `games` areas of area_size bytes, one per game, then one more area, the scratch.
  A save is written to the scratch first and read back; only then is it copied into the game's
  own area and read back again. At start, save_store_recover() finishes a copy that was
  interrupted. So at every moment either the previous save or the new one is whole in the
  game's area or in the scratch: a power cut, a reset or a flash error can lose the save in
  progress, never the one before it, and a save is reported as stored only after it was read
  back from the SmartEEPROM.

  Each record is a 24-byte header (magic, a sequence number, the story's tag, the game, the
  length, a CRC-32 of the data and a CRC-32 of the header), the data, and the non-zero end
  mark the SmartEEPROM needs (see smarteeprom.h). The tag identifies the story the data
  belongs to (the caller chooses it, e.g. a CRC of the story's dynamic memory); a record
  made for another story is reported as such, never returned.
*******************************************************************************/

#ifndef SAVE_STORE_H
#define SAVE_STORE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef enum {
    SAVE_OK = 0,
    SAVE_E_UNAVAILABLE,       /* the SmartEEPROM did not start */
    SAVE_E_TOO_LARGE,         /* more than save_store_capacity() */
    SAVE_E_WRITE,             /* the SmartEEPROM did not take the write */
    SAVE_E_VERIFY             /* read back, it was not what was written */
} save_result_t;

typedef enum {
    LOAD_OK = 0,
    LOAD_UNAVAILABLE,
    LOAD_NONE,                /* nothing saved for this game */
    LOAD_DAMAGED,             /* a record is there but is not whole */
    LOAD_OTHER_STORY          /* a whole record, made for another story or release */
} load_result_t;

/* Lay out the store: `games` areas of area_size bytes (a multiple of 4) and the scratch after them.
 * available: false if smarteeprom_init() failed; every call then reports it. */
void save_store_init(uint32_t area_size, int games, bool available);

/* Finish a save that was interrupted after the scratch was complete. Call once after save_store_init(). */
void save_store_recover(void);

/* The largest data save_store_write() takes. */
size_t save_store_capacity(void);

save_result_t save_store_write(int game, uint32_t tag, const void *data, size_t len);

/* The newest whole record for `game` that was made for `tag`: its data into buf (at most cap bytes). */
load_result_t save_store_read(int game, uint32_t tag, void *buf, size_t cap, size_t *len);

/* A short message for the player ("" for SAVE_OK / LOAD_OK) */
const char *save_store_save_message(save_result_t r);
const char *save_store_load_message(load_result_t r);

#ifdef SAVE_TEST_HOOKS
/* Tests only: simulate a power cut at a point of a save (the firmware stops there). 0 = none.
 *  1 before anything is written   2 scratch written, perhaps not committed (the device may commit it)   3 scratch committed
 *  4 area written, not committed  5 area committed, not yet read back */
extern volatile int save_store_fault;
#endif

#endif /* SAVE_STORE_H */
