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
  │◄── {"type":"resumed",              │
  │      "history":"...","prompt":true}│
  │── {"type":"input","text":"look"} ──►│
  │  ...
```
