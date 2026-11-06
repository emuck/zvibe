/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * zvibe_riscv_v4.h - version 4 story player for the RISC-V (SERV) firmware
 *
 * A V4 story draws its own screen (status line in an upper window, cursor
 * addressing, text styles) and may ask for single keys, so it is played through
 * the ANSI screen layer (src/screen) on the serial terminal instead of the plain
 * line-oriented path the V3 stories use.
 *
 * The module is independent of the board: it needs two terminal functions from
 * the firmware (or from the host test, tests/host_v4_player.c) and, for saves,
 * the flash_save.h API. Built only with ZVIBE_ENABLE_V4=1.
 */

#ifndef ZVIBE_RISCV_V4_H
#define ZVIBE_RISCV_V4_H

#include <stddef.h>
#include <stdint.h>
#include "zvibe.h"            /* ZVIBE_MEMORY_BUFFER_SIZE */
#include "zvibe_config.h"

#if ZVIBE_ENABLE_V4

/* ---- provided by the firmware ------------------------------------------------ */

/** Send one byte to the terminal (blocking). */
void rv_put_byte(uint8_t b);

/** The next received byte, or -1 if none is waiting. Never blocks. */
int rv_rx_get(void);

/** Called while the player waits for a byte (the firmware does nothing; the host test
 *  uses it to stop at the end of its scripted input). */
void rv_idle(void);

/**
 * Busy-wait iterations that last about one millisecond. The wait for an escape
 * sequence's tail and for the terminal-size reply count loop iterations, so this
 * needs calibrating on the real core (SERV is bit-serial and slow). A value that
 * is too small cuts those waits short (arrow keys may arrive as an Escape plus
 * letters, the size probe falls back to 24 x 80); one that is too large only makes
 * them slower. Override with -DRV_SPIN_PER_MS=...
 */
#ifndef RV_SPIN_PER_MS
#define RV_SPIN_PER_MS 40UL
#endif

/* ---- player ----------------------------------------------------------------- */

/** Largest RAM the packed save may use (what goes into one flash save slot): 16 KB, or the
 *  story buffer if that is smaller (a small story cannot change more bytes than it has; a
 *  save that does not fit fails cleanly with a message). */
#ifndef RV_V4_PACKED_MAX
#define RV_V4_PACKED_MAX ((ZVIBE_MEMORY_BUFFER_SIZE) < 16384 ? (ZVIBE_MEMORY_BUFFER_SIZE) : 16384)
#endif

typedef enum {
    RV_V4_FINISHED = 0,   /**< the story ended (quit, or it fell off the end) */
    RV_V4_RESET,          /**< the player typed ~reset~ (test scripts): start it afresh */
    RV_V4_ERROR           /**< the story could not be loaded or the interpreter failed */
} rv_v4_result;

/** True if the story file is a version 4 story (header byte 0). */
int rv_v4_is_v4_story(const uint8_t *story, size_t size);

/**
 * Play a version 4 story to its end on the terminal: probe the terminal size, run
 * the interpreter, handle line input, single keys, save, restore and restart.
 * `story` is the whole story file (it may be in flash: it is only read). Returns
 * with the terminal's modes restored.
 */
rv_v4_result rv_v4_play(const uint8_t *story, size_t size);

/** RAM the player's save buffers need, for the memory budget (see ram_budget.py). */
size_t rv_v4_save_buffers_bytes(void);

#endif /* ZVIBE_ENABLE_V4 */
#endif /* ZVIBE_RISCV_V4_H */
