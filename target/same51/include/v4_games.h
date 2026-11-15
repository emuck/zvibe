#ifndef V4_GAMES_H
#define V4_GAMES_H
#include <stdint.h>
#include <stddef.h>

/* One story of the multi-game V4 firmware (generated table: tools/v4_games_to_c.py) */
typedef struct {
    const char *name;
    const uint8_t *data;
    size_t size;
} v4_game_t;

extern const v4_game_t v4_games[];
extern const int v4_num_games;

#endif
