# Save/Restore System

Persistent game saves with delta compression and round-robin wear leveling.
Verified on both MAX10 (UFM) and Arty S7-50 (QSPI flash) hardware.

## User Experience

```
> SAVE
Ok.

> RESTORE
Ok.

[game resumes from saved position]
```

Save is fast (typically under 2 seconds including flash erase). Restore is
nearly instant (<100ms — just a flash read + delta apply).

## Architecture

### Per-Game Save Rings (ZVGM TOC v2)

Each game has its own independent save ring. The offset and slot count are
stored in the game's ZVGM TOC entry (added in TOC format version 2):

```c
typedef struct __attribute__((packed)) {
    uint32_t offset;          /* Flash address of game data */
    uint32_t size;            /* Game size in bytes */
    char     id[16];          /* Game ID */
    char     name[48];        /* Display name */
    uint32_t saves_offset;    /* Flash offset of this game's save ring (0 = none) */
    uint32_t save_slot_count; /* Number of slots in this game's ring (0 = none) */
} flash_toc_entry_t;          /* 80 bytes */
```

When a game launches, `init_zvibe_game()` reads its TOC entry and calls
`flash_save_init(entry->saves_offset, entry->save_slot_count)`.  Games
are completely isolated — saves for Zork I cannot interfere with Zork II.

### Delta Compression

Only the bytes that differ from the original game data (in flash) are saved.
Typical Z-machine save = 400–1500 bytes vs ~14KB full memory dump (8–24x reduction).

**Delta format** (header = 16 bytes):
```
Magic 'DELT' (4B) | PC (4B) | stack_size (2B) | delta_count (2B) | base_ptr (4B)
[stack data: variable]
[delta entries: offset(2B) + length(1B) + data(length B), repeated]
```

On restore: copy original game data from XIP flash → apply delta patches → restore stack/PC.

### Wear Leveling

Round-robin rotation across all slots. Each new save goes to the next slot,
wrapping around. The slot with the highest sequence number is the current save.
Sequence numbers are scanned at `flash_save_init()` time.

### Flash API

```c
/* Initialize save ring for one game. Call once per game launch. */
int flash_save_init(uint32_t base, int num_slots);

/* Returns 1 if save ring has been initialized with num_slots > 0. */
int flash_save_has_slots(void);

/* Write save (erases next slot, writes header + delta data). */
int flash_save_write(const void *data, size_t len);

/* Read most recent save into buf. Returns bytes read or SAVE_ERR_*. */
int flash_save_read(void *buf, size_t buf_len);

/* Returns 1 if a valid save exists in the ring. */
int flash_save_exists(void);
```

## Platform Details

### Arty S7-50 (QSPI Flash)

- **Slot size**: 64KB (one S25FL128S erase sector per slot)
- **Slots per game**: 4 (fixed; set by `SAVES_PER_GAME` in `build_flash.py`)
- **Allocation**: Sequential — game 0 slots at `saves_start`, game 1 after, etc.
- **Total (24 games)**: 96 slots × 64KB = 6MB

Layout computed by `boards/arty_s7_50/build_flash.py`:
```python
SAVES_PER_GAME = 4
for i, game in enumerate(games):
    game['saves_offset']    = saves_start + i * SAVES_PER_GAME * SLOT_SIZE
    game['save_slot_count'] = SAVES_PER_GAME
```

### MAX10 08 Eval (On-Chip UFM)

- **Slot size**: 2KB (one UFM page per slot)
- **Slots per game**: all remaining UFM after firmware + game data
- **Typical**: 5 slots for Zork I (83KB game), more for smaller games
- **Allocation**: Single game gets all remaining space

```python
DELTA_SLOT_SIZE = 2048
remaining   = UFM_TOTAL - saves_start
slot_count  = remaining // DELTA_SLOT_SIZE
game['saves_offset']    = saves_start
game['save_slot_count'] = slot_count
```

## File Structure

```
common/sw/
├── flash_save.c/h          Platform-agnostic ring + wear leveling
├── save_delta.c/h          Delta encode/decode
├── flash_hal.h             HAL interface (erase_sector, program_bytes, read_bytes)
└── game_registry_riscv.c   Reads TOC entry, calls flash_save_init() per game

boards/max10_08_eval/sw/
├── flash_hal_max10.c       MAX10 HAL → UFM CSR driver
└── flash_driver_max10_ufm.c UFM low-level read/write/erase

boards/arty_s7_50/sw/
└── flash_driver.c          Arty HAL → QSPI SPI commands

boards/max10_08_eval/build_flash.py   Per-game slot allocation (UFM)
boards/arty_s7_50/build_flash.py      Per-game slot allocation (QSPI)
common/sw/zvibe_flash_lib.py          Serializes TOC v2 with saves_offset/save_slot_count
```

## Status

| Platform | Status | Notes |
|----------|--------|-------|
| MAX10 UFM | ✅ Verified Feb 12, 2026 | Hardware: SAVE/RESTORE end-to-end |
| Arty QSPI | ✅ Verified Feb 15, 2026 | Hardware: per-game rings confirmed |

## References

- Intel MAX10 UFM User Guide (UG-M10UFM 683180)
- [S25FL128S Datasheet](https://www.infineon.com/assets/row/public/documents/10/49/infineon-s25fl128s-s25fl256s-128-mb-16-mb-256-mb-32-mb-fl-s-flash-spi-multi-io-3-v-datasheet-en.pdf)
- `boards/max10_08_eval/README.md`
- `boards/arty_s7_50/README.md`
