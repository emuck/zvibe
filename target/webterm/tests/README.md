# webterm Tests

All tests run on the host — no network, no browser, no hardware.

## Structure

```
tests/
├── Makefile             # build and run all tests
├── mock_platform.c      # fake webterm_platform_t for testing
├── mock_platform.h
├── test_session.c       # unit tests for session layer
└── test_czech.sh        # Czech Z3 conformance via webterm session
```

## Mock Platform

`mock_platform` implements `webterm_platform_t` with no networking:

- **send** — appends JSON frames to an in-memory output buffer
- **save** — writes to a heap buffer
- **restore** — reads from the same buffer

Tests feed scripted input via `webterm_session_on_input()` and assert
against the captured output frames.

## Unit Tests (`test_session.c`)

| Test | What it checks |
|------|---------------|
| `test_output_buffering` | Chars emitted by zvibe are held until input is needed, then flushed as one `output` frame |
| `test_output_flush_on_prompt` | `prompt: true` appears on the frame that precedes input blocking |
| `test_input_delivery` | Input fed via `on_input()` reaches the Z-machine read callback |
| `test_session_init` | Fresh session is in IDLE state, no output |
| `test_game_start` | Load a game, run, verify opening text appears |
| `test_game_restart` | Reset and reload same game, output matches first run |
| `test_save_restore` | Save at turn N, restore, game state matches turn N |
| `test_no_save_callback` | Save/restore with NULL callbacks sends error frames |
| `test_accept_busy` | Second accept while active returns 0 |
| `test_disconnect_resume` | Disconnect pauses session; re-accept replays history |
| `test_history_ring` | History does not overflow; oldest frames drop correctly |
| `test_status_frame` | Status line updates emit `{"type":"status",...}` frames |

## Czech Conformance (`test_czech.sh`)

Runs the Czech Z3 conformance suite (368 tests) through the webterm session
layer using the mock platform. This is the primary correctness gate: if
Czech passes, the I/O callback wiring is correct.

The test compares output against the known-good console target output. Since
zvibe is deterministic, the output must be byte-for-byte identical.

## Running

```sh
make test           # all tests
make test-unit      # unit tests only
make test-czech     # Czech suite only
```

## Adding a New Test

1. Add a `void test_foo(void)` function to `test_session.c`
2. Call it from `main()` in the same file
3. Use the helpers from `mock_platform.h` to set up scripted input and
   capture output frames
4. `assert()` your expectations — the test binary exits non-zero on failure

No test framework dependency. Keep it simple.
