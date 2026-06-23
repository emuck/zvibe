/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/*******************************************************************************
  Game Registry - Multi-Game Z-Machine Support
  Defines the structure and interface for multiple game support
*******************************************************************************/

#ifndef GAME_REGISTRY_H
#define GAME_REGISTRY_H

#include <stdint.h>
#include <stddef.h>

/* Game entry structure */
typedef struct {
    const char* name;           /* Display name of the game */
    const uint8_t* data;        /* Pointer to game data array */
    size_t size;                /* Size of game data in bytes */
} game_entry_t;

/* External declarations - defined by generated game_registry.c */
extern const game_entry_t games[];
extern const int num_games;

#endif /* GAME_REGISTRY_H */