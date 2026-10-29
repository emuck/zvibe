# webterm WebSocket Protocol

All messages are UTF-8 encoded JSON text frames. There are no binary frames.

## Client → Server

### Load a game
```json
{"type": "load", "game": "restaurant.z3"}
```
Loads the named game from the platform's game storage. If a game is already
running, it is abandoned and the new game starts from the beginning. Responds
with one or more `output` frames containing the game's opening text.

### Send player input
```json
{"type": "input", "text": "go north"}
```
Delivers one line of input to the Z-machine. Must only be sent when the
session is waiting for input (i.e. after the `prompt` field appears in an
`output` frame). Sending input when not prompted has no effect.

### Send a key (version 4 stories)
```json
{"type": "key", "code": 13}
```
Delivers one key to a game waiting in `read_char`, as a ZSCII code: 13 Return,
8 Delete, 27 Escape, 129-132 up/down/left/right, or the character's code. Only
valid after a `screen` frame with `"key": true`; a `key` message at any other
time, and an `input` message while a key is awaited, have no effect. The game
prints whatever it wants to show for the key; nothing is echoed.

### Save game
```json
{"type": "save"}
```
Requests a save. The session writes the current Z-machine state to platform
storage. Responds with `{"type":"saved"}` on success or
`{"type":"error","message":"..."}` on failure.

### Restore game
```json
{"type": "restore"}
```
Restores the most recent save. The game resumes from the saved state.
Responds with output from the game at the restored point, or
`{"type":"error","message":"No save found"}` if no save exists.

### Reset session
```json
{"type": "reset"}
```
Abandons the current game and returns the session to IDLE. The client
should present the game picker. A subsequent `load` starts a fresh game.

### List available games
```json
{"type": "games"}
```
Requests the list of available game files. Responds with a `games` frame.

## Server → Client

### Game output
```json
{"type": "output", "text": "West of House\nYou are standing..."}
```
One or more paragraphs of Z-machine output. The `text` field contains raw
UTF-8 text with newlines. The client appends this to the terminal display.

The session buffers output until the Z-machine yields for input, then flushes
everything accumulated since the last input as a single `output` frame. This
means one player input produces exactly one `output` frame in response.

### Prompt indicator
```json
{"type": "output", "text": "...", "prompt": true}
```
The `prompt` field is present and `true` on the output frame that immediately
precedes the Z-machine blocking for input. The client uses this to show the
input field and `>` cursor.

### Screen frames (version 4 stories)
```json
{"type": "screen", "reset": true, "rows": 24, "cols": 80}
{"type": "screen", "data": "\u001b[1;1H...", "prompt": true}
{"type": "screen", "data": "...", "key": true}
```
A version 4 story draws its own screen (an upper window, cursor positioning,
text styles), so it is not sent as `output` text. The server runs the ANSI
screen layer (`src/screen/zvibe_screen.c`) on a `rows` x `cols` screen and sends
its terminal bytes in `data`; the client draws them with a small VT100 emulator
(`zterm.js`). The bytes use only cursor positioning (`CSI H`), erase (`CSI J`,
`CSI K`), scroll regions (`CSI r`), SGR 0/1/4/7, auto-wrap switching (`CSI ?7h/l`),
CR, LF and BS.

- `reset` (no `data`) is sent when a story loads, and again on reconnect before
  the replay: the client discards its screen, sizes a new one and draws what
  follows. `story` and `resumed` frames work as for version 3.
- A long run of output is split over several `screen` frames of at most 1024
  bytes; an escape sequence may be split between frames.
- `prompt` and `key` mark the last frame before the game waits for a line or for
  one key, as for `output` frames.
- **Echo:** the client echoes the characters the player types into its own screen
  (a Return sends `{"type":"input","text":...}`); the server does not echo them
  back but moves to the next line itself, so the first bytes after an `input` are
  the newline. The client limits a line to the columns left on the cursor's row.
- A reconnecting client is sent the bytes since the story last cleared the whole
  screen (or the most recent history if that is longer than the history ring).
- Version 3 stories still use `output` frames, `status` and `room`.

### Status line update
```json
{"type": "status", "location": "West of House", "score": 0, "turns": 1}
```
Emitted whenever the Z-machine updates its status line (room name,
score/turns or time). Sent before the `output` frame for the same turn.
Clients may display this in a header bar or ignore it entirely.

For time-mode games:
```json
{"type": "status", "location": "The Orient Express", "hours": 9, "minutes": 47}
```

### Story identity
```json
{"type": "story", "release": 88, "serial": "840726", "checksum": 32587}
```
Sent once right after a game is loaded, and again right after a client
resumes a paused/waiting session (before history replay). `(release,
serial, checksum)` together identify the exact story file build — a
Cartographer client uses this as a stable key to persist and restore its
map (e.g. in `localStorage`) across page reloads, since map state is not
otherwise part of the session's output history. See CARTOGRAPHER.md.

### Room change (Cartographer)
```json
{"type": "room", "cmd": "north", "id": 75, "name": "Forest Path"}
```
Emitted whenever the player's location changes after a turn completes.
`cmd` is the raw input that produced the move, or `null` if the move
wasn't player-initiated (opening placement, a restore, or a client
reconnect re-announcing the current room). `id` is the Z-machine object
number of the new location — stable and globally unique within a story,
even across rooms that share a display name (e.g. a maze). `name` is the
decoded short name. Sent before the `output` frame for the same turn,
like `status`. See CARTOGRAPHER.md for the client-side map engine this
feeds.

### Game list
```json
{"type": "games", "list": ["restaurant.z3", "zork1.z3"]}
```
Response to a `{"type":"games"}` request. The list contains filenames only,
not full paths. An empty list means no games are installed.

### Session busy
```json
{"type": "busy"}
```
Sent immediately after connection if another client is already active. The
server closes the connection after sending this frame. The client should
display a message like "A game is already in progress."

### Session resumed
```json
{"type": "resumed", "history": "...last N lines of output..."}
```
Sent when a client connects and a paused session exists. The `history` field
contains recent output so the player has context. The session resumes; the
client should display the history and show the input field if `prompt` is
also present.

```json
{"type": "resumed", "history": "...", "prompt": true}
```

### Save confirmed
```json
{"type": "saved"}
```

### Error
```json
{"type": "error", "message": "Game not found: zork1.z3"}
```
Non-fatal. The session remains in its current state.

## Session State Machine

```
IDLE
 │  client connects
 ▼
CONNECTED (no game)
 │  {"type":"load","game":"..."}         → loads game
 │  {"type":"games"}                     → responds with game list
 ▼
RUNNING
 │  {"type":"output",...,"prompt":true}  → client shows input
 │  {"type":"input","text":"..."}        → game advances
 │  {"type":"status",...}               → optional header update
 │
 │  client disconnects
 ▼
PAUSED (game state preserved in RAM)
 │  client reconnects
 ▼
RUNNING (resumed, history replayed)
 │
 │  {"type":"reset"}  or  game ends
 ▼
IDLE
```

## Output History

The session maintains a ring buffer of the last 50 output frames (not lines).
This is used only for the `resumed` replay on reconnect. The buffer is not
persisted across device reboots.

The 50-frame default is a compile-time constant (`WEBTERM_HISTORY_FRAMES`)
that platforms may override.

## Connection Sequence (normal flow)

```
Client                              Server
  │── WebSocket upgrade ──────────────►│
  │◄── 101 Switching Protocols ────────│
  │                                    │  (check if session busy)
  │◄── {"type":"games","list":[...]} ──│  (send game list on connect)
  │── {"type":"load","game":"foo.z3"} ─►│
  │◄── {"type":"story",...} ───────────│  (Cartographer key)
  │◄── {"type":"status",...} ──────────│
  │◄── {"type":"room",...} ────────────│  (opening placement, cmd:null)
  │◄── {"type":"output","text":"..."   │
  │         "prompt":true} ────────────│
  │── {"type":"input","text":"look"} ──►│
  │◄── {"type":"status",...} ──────────│
  │◄── {"type":"output","text":"..."   │
  │         "prompt":true} ────────────│
  │  ...
```

## Reconnect Sequence

```
Client                              Server
  │── WebSocket upgrade ──────────────►│
  │◄── 101 Switching Protocols ────────│
  │                                    │  (session found, paused)
  │◄── {"type":"resumed","prompt":true}│
  │◄── {"type":"story",...} ───────────│  (Cartographer key)
  │◄── {"type":"room",...} ────────────│  (re-announce, cmd:null)
  │◄── {"type":"output","text":"..."}  │  (history replay)
  │── {"type":"input","text":"look"} ──►│
  │  ...
```
