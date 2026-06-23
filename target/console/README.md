# ZVibe Console Target

Desktop and server Z-machine interpreter with terminal interface and persistent saves.

## Applications

| Binary | Description |
|--------|-------------|
| `zvibe_console` | Full-featured: status line, persistent file saves |
| `zvibe_minimal` | Lightweight host build: file loading enabled, status line and save/restore compiled out |
| `libzvibe.so` | Shared library for integration with other applications |

## Quick Start

```bash
cd target/console
make all

./build/bin/zvibe_console path/to/game.z3
./build/bin/zvibe_minimal path/to/game.z3
```

## Build Targets

```bash
make all        # Build everything (default)
make console    # zvibe_console only
make minimal    # zvibe_minimal only
make shared     # libzvibe.so/.dylib
make clean
```

Build output:
```
build/
├── bin/zvibe_console
├── bin/zvibe_minimal
└── lib/libzvibe.so
```

## Usage

### Console application

```bash
./build/bin/zvibe_console game.z3
```

Commands: `north`, `take lamp`, `inventory`, `save`, `restore`, `quit`

Save files are created as `<game>.sav` in the same directory as the game file.

### Minimal application

```bash
./build/bin/zvibe_minimal game.z3
```

No status line, no save/restore. The same interpreter core — useful for
scripted input, automated testing, and CI.

Build profile:
- `ZVIBE_ENABLE_SAVE_RESTORE=0`
- `ZVIBE_ENABLE_STATUS_LINE=0`
- `ZVIBE_ENABLE_FILE_IO=1`

### Python interface

```python
import ctypes
lib = ctypes.CDLL('./build/lib/libzvibe.so')
```

See `zvibe_console.py` for a complete example.

## Memory Architecture

The console target uses the same split memory architecture as the embedded targets:
- Dynamic memory (game state): 2–14 KB (all known v3 titles fit in 16 KB)
- Static memory: direct read from game file
- Total RAM: ~25 KB (vs 128 KB+ in traditional interpreters)

No `malloc()` in the interpreter core — fixed-size buffers, predictable usage.

## Testing

```bash
cd tests
make test               # All tests
make test-unit          # Memory unit tests (119 tests)
make test-czech         # Czech Z-machine conformance suite (368 tests)
make test-integration   # Game completion tests
```

See [tests/README.md](tests/README.md) for details.

## API

```c
#include "zvibe_api.h"

zvibeContext *ctx = zvibe_create(output_func);
zvibe_set_status_callback(ctx, status_func);   // console only
zvibe_load_story(ctx, "game.z3");
while (1) {
    zvibeResult result = zvibe_run(ctx);
    if (result == ZVIBE_WAIT_FOR_INPUT) {
        zvibe_input(ctx, get_user_input());
    } else if (result == ZVIBE_SAVE_REQUESTED) {
        handle_save(ctx);
    } else {
        break;
    }
}
zvibe_destroy(ctx);
```

## License

BSD-3-Clause. See [../../LICENSE](../../LICENSE).
