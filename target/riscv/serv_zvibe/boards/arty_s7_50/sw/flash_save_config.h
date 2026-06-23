/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Flash Save Configuration — Arty S7-50 (S25FL128S, 16MB QSPI)
 *
 * The Arty QSPI flash is shared between the bitstream (physical 0x000000)
 * and user data (physical 0x100000 onward). The ZVIF header lives at the
 * start of the user region:
 *
 *   Physical 0x100000  → XIP virtual 0x80100000  ← ZVIF header (256 bytes)
 *   Physical 0x100100  → XIP virtual 0x80100100  ← Firmware entry point
 *
 * FLASH_METADATA_VADDR: XIP virtual address of the ZVIF header (direct pointer).
 * FLASH_XIP_BASE: base for physical offset → virtual address translation (covers
 *                 all of QSPI from physical 0x0).
 */

#ifndef FLASH_SAVE_CONFIG_H
#define FLASH_SAVE_CONFIG_H

/* Arty user region starts at physical 0x100000 (after bitstream) */
#define FLASH_METADATA_VADDR    0x80100000U     /* XIP virtual addr of ZVIF header */
/* FLASH_XIP_BASE stays at default 0x80000000 — covers full QSPI from physical 0 */

/* Slot size matches S25FL128S erase granularity (64KB sectors) */
#define SLOT_SIZE           65536U
#define SECTOR_SIZE         SLOT_SIZE

/* Save slot count and base come from ZVIF metadata at runtime (dynamic layout) */

#define SAVE_HEADER_SIZE    12                      /* magic(4)+seq(4)+size(2)+crc(2) */
#define MAX_SAVE_DATA       (SLOT_SIZE - SAVE_HEADER_SIZE)
#define SAVE_MAGIC          0x5A564942U             /* "ZVIB" */

#endif /* FLASH_SAVE_CONFIG_H */
