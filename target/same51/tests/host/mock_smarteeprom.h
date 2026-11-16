/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/* A model of the SmartEEPROM for the host tests of save_store.c: a 16 KB array committed a 512-byte page at a
 * time (a page commit is atomic: it happens or it does not), a page buffer that the writes go to, and the same
 * commit rules as the hardware (a write to another page, or smarteeprom_flush(), commits the page in the buffer).
 * Faults: a power cut before the k-th commit (everything not yet committed is lost: the caller is left through a
 * longjmp), and a commit that is silently dropped. */
#ifndef MOCK_SMARTEEPROM_H
#define MOCK_SMARTEEPROM_H
#include <setjmp.h>
#include <stdint.h>
#include <stddef.h>

extern uint8_t mock_mem[16384];       /* what is committed */
extern int mock_commits;              /* page commits so far (since mock_reset_counters) */
extern int mock_cut_at;               /* power is cut before this commit number; -1 = never */
extern int mock_drop_at;              /* this commit is silently lost; -1 = never */
extern int mock_drop_count;           /* how many commits from mock_drop_at on are lost (default 1) */
extern jmp_buf mock_power_cut;        /* longjmp target when the power is cut */
extern int mock_available;            /* what smarteeprom_init() reports (unused by save_store) */

void mock_erase(void);                /* all 0xFF, no faults */
void mock_reset_counters(void);
void mock_reboot(void);               /* the page buffer is lost (as at a reset); the counters and faults are cleared */
#endif
