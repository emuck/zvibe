# webterm — Z-Machine Web Terminal Target

`webterm` is a platform-agnostic Z-machine web terminal for embedded WiFi devices.
It serves a browser-based text adventure terminal over WebSocket, so any device
with a web browser and network access to the hardware can play Z-machine games —
no app, no companion software, no cloud.

## Design Philosophy

This target defines **a contract, not a complete application.** The parts that
live here are the parts that work identically on any platform:

- **`webterm_session`** — the session layer: zvibe I/O callbacks, output
  buffering, input queuing, history ring buffer, session state machine.
- **`webterm_platform.h`** — the interface a hardware target must implement:
  send a JSON frame, optionally persist save data.
- **`PROTOCOL.md`** — the WebSocket message specification.
- **`zterm.js`** — the browser's screen for version 4 stories (see below).
- **`platform/posix/`** — a POSIX reference server: development tool and
  second reference implementation alongside the tests.
- **`tests/`** — host-runnable test suite using a mock platform.

Everything hardware-specific (WiFi stack, HTTP server, flash storage, RTOS
task setup, game file management) belongs in the consuming project, not here.

## Repository Split

```
zvibe/target/webterm/          ← lives here
  webterm_session.h/c          platform-agnostic session layer
  webterm_platform.h           platform interface contract
  PROTOCOL.md                  WebSocket message specification
  tests/                       unit + integration tests (mock platform)
  platform/posix/              POSIX reference server (dev/testing)

your-hardware-project/         ← lives in your repo
  components/zvibe/            zvibe core as a library component
  src/zmachine_service.*       platform adapter (implements webterm_platform.h)
  src/webserver.*              HTTP + WebSocket endpoint wiring
  www/zmachine.html            terminal UI (copy from platform/posix/www/)
```

The `platform/posix/` server is included here because it is the primary
development and testing vehicle — you build and test the full browser
experience on a laptop before touching hardware. The HTML/JS terminal UI
(`platform/posix/www/zmachine.html`) is the canonical client; copy it into
your hardware project's web asset storage.

## Architecture

```
Browser (any device)
  │  WebSocket frames (JSON)
  ▼
Platform adapter          ← your hardware project implements this
  │  webterm_platform_t
  ▼
webterm_session           ← webterm_session.h / webterm_session.c
  │  zvibe callbacks
  ▼
zvibe core                ← ../../src/
```

## Session Lifecycle

```
IDLE ──load──► RUNNING ──quit/end──► IDLE
                  │
              disconnect
                  │
                  ▼
               PAUSED ──reconnect──► RUNNING (history replayed)
                  │
              load/reset
                  │
                  ▼
               RUNNING (new game)
```

A session is created once at startup and persists for the life of the process
or device. It is not tied to any individual WebSocket connection.

## Single-Session Policy

This target is designed for embedded devices where one interactive session
at a time is the natural model (a clock, a handheld, a kiosk). The policy is:

- **One active client.** A second client that connects while a session is in
  use receives `{"type":"busy"}` and the connection is immediately closed. (The POSIX reference server handles one connection at a time, so there a second client waits until the first leaves; see `PROTOCOL.md`.)
- **Sessions survive disconnects.** If the client drops, the session pauses —
  the zvibe context and all game state remain in RAM. The next client to
  connect resumes the session from where it was.
- **Output history.** A ring buffer of recent output frames is kept so a
  reconnecting client can see context (`WEBTERM_HISTORY_FRAMES`, default 50).
- **Explicit reset.** A client can send `{"type":"reset"}` to abandon the
  current game and return to IDLE.

A platform that wants multiple concurrent sessions (a more capable host) can
manage a pool of `webterm_session_t` instances. The session layer is
single-instance and unaware of concurrency above it.

## Implementing a New Platform

Three things are required.

### 1. Implement `webterm_platform_send`

```c
static void my_send(webterm_platform_t *p, const char *json, size_t len) {
    // write `json` to the active WebSocket connection
    // if no client is connected, drop silently
}
```

### 2. Wire incoming WebSocket frames to the session

Parse incoming JSON and call the appropriate session function:

```c
// {"type":"input","text":"go north"}
webterm_session_on_input(session, text, strlen(text));

// {"type":"load","game":"foo.z3"}
webterm_session_load(session, game_data, game_size);

// {"type":"save"}
webterm_session_on_save(session);

// {"type":"restore"}
webterm_session_on_restore(session);

// {"type":"reset"}
webterm_session_on_reset(session);

// {"type":"games"}  — handle at the platform layer, not the session layer
//   query your storage and send {"type":"games","list":[...]} directly
```

### 3. Optionally provide save/restore storage

```c
static int my_save(webterm_platform_t *p, const void *data, size_t len) {
    // write to NVS, flash, filesystem, etc.
    return 1; // success
}

static size_t my_restore(webterm_platform_t *p, void *buf, size_t max) {
    // read from storage
    return bytes_read; // 0 = no save found
}
```

Pass `NULL` for both to disable save/restore. The session will respond to
the Z-machine's save/restore requests with a polite failure.

### Minimal platform checklist

- [ ] Implement `webterm_platform_send`
- [ ] Call `webterm_session_on_input` on `{"type":"input"}` frames
- [ ] Call `webterm_session_load` on `{"type":"load"}` frames
- [ ] Send `{"type":"busy"}` and close connection if session already active
- [ ] Serve `zmachine.html` over HTTP (copy from `platform/posix/www/`)
- [ ] Optionally: implement save/restore storage callbacks

## Testing

```
make test          # unit tests + Czech conformance via mock platform
make test-unit     # session unit tests only
make test-czech    # Czech Z3 suite through the webterm session layer
```

All tests run on the host — no network, no browser, no hardware needed.
The mock platform (`tests/mock_platform.c`) captures output and feeds
scripted input, reusing the same playthrough scripts as the console target.

The Czech conformance suite is the primary correctness gate: if all 368
Czech tests pass through the webterm session layer, the I/O wiring is correct.

See `tests/README.md` for full details.

## Version 4 stories

Build with `-DZVIBE_ENABLE_V4=1` (the POSIX server: `make V4=1`; it adds
`zvibe_ops_v4.c`, `src/screen/zvibe_screen.c` and a 72 KB `ZVIBE_MEMORY_BUFFER_SIZE`).
The server then lists `.z4` files and accepts stories up to 256 KB; a V4 save can
be over 24 KB, so the session's save buffer grows to 64 KB.

A V4 story draws its own screen, so the session runs the ANSI screen layer on a
fixed 24x80 screen (`WEBTERM_SCREEN_ROWS`, `WEBTERM_SCREEN_COLS`) and sends its
terminal bytes in `screen` frames. `zterm.js` (next to `cartographer.js`; the
POSIX server serves it at `/zterm.js`, `-z` sets the path) is the browser's VT100
screen: it draws the bytes, echoes typed characters, and sends lines and
`read_char` keys. The upper window, cursor addressing and styles all work, so
Bureaucracy's form plays. A platform's page needs `<script src="zterm.js">`, a
host element for `new ZTerm(el, send)`, and handling of `screen` frames as in
`platform/posix/www/zvibe.html`. `zvibe-duo.html` is version 3 only. On a phone the
keyboard does not appear yet: keys come from the document's `keydown`, and nothing is
focused. Planned upgrade: a hidden input that takes focus on a tap and is read through
`input` events, plus on-screen arrow and Escape keys. The cartographer follows version 3 stories only.

`make test` also runs the session tests in a V4 build (`make test-v4`) and the
screen tests (`make test-screen`: the screen frames drawn by the console tests'
VT100 emulator, and `zterm.js` checked against that emulator on a story and 200
random screens; the JS part needs `node`).

## Protocol

See `PROTOCOL.md` for the complete WebSocket message specification.
