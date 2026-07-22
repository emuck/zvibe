# zVibe Cartographer — Integration Spec
## Live auto-map for the webterm target

This document is the complete integration brief for adding the Cartographer
map engine to `target/webterm`. It covers the C-side hook, the JS module
boundary, the data contract, and the key design decisions with their
rationale. Read this before touching any files.

---

## Design philosophy

The mapper is **a passive observer of the interpreter's existing state**.
It never parses the story file, never touches object tables, and never
guesses at exit lists. It watches one thing: when `global[0]` (the player
location object) changes after a turn, it fires an event carrying the old
command, the new room's object number, and its decoded short name.

This approach is:
- **Game-agnostic** — works on every v3/v4/v5 story without per-game
  knowledge.
- **Spoiler-free** — the map grows only where the player has been.
- **Zero-cost on other targets** — the C hook is ~20 lines; non-webterm
  builds compile it out with a no-op macro.
- **Correct for non-Euclidean geometry** — one-way passages, asymmetric
  exits, and teleports are all rendered honestly rather than inferred.

---

## C-side: the interpreter hook

### Where to add it

After each turn completes (i.e., after `z_read` / the top-level opcode
dispatch loop returns control to the main loop), compare `global[0]` to its
value at the start of the turn.

Exact location depends on zVibe's main loop structure, but the pattern is:

```c
// at top of turn loop — snapshot location before the turn
zword prev_location = zmp[h_globals];   // global[0] is at h_globals offset

// ... run the turn (z_read, opcode dispatch, etc.) ...

// at bottom of turn loop — detect location change
zword new_location  = zmp[h_globals];
if (new_location != prev_location) {
    cart_room_changed(last_input_buf, new_location);
}
```

`last_input_buf` is whatever buffer holds the raw player input string for
this turn. If the location changed without a player command (demon moves,
"pray" teleports, opening-scene room placement), pass `NULL`; the mapper
handles it gracefully.

### The hook function

Add to `target/webterm/cartographer.c` (or a suitable .h for the inline):

```c
/*
 * cart_room_changed — called by the interpreter main loop whenever
 * global[0] changes after a turn.
 *
 *   cmd      raw input line the player typed (UTF-8, null-terminated),
 *            or NULL if the move was not player-initiated.
 *   room_id  the new value of global[0] — the Z-machine object number.
 *
 * This function decodes the object's short name and emits a JSON event
 * over the webterm channel. It is a no-op on non-webterm targets.
 */
void cart_room_changed(const char *cmd, zword room_id)
{
    /* Decode the short name from the object's property table.
       zVibe already has a short-name decoder for the status line — reuse
       it here rather than duplicating.  The function signature below is
       illustrative; adapt to whatever zVibe exposes. */
    char name_buf[256];
    get_object_short_name(room_id, name_buf, sizeof(name_buf));

    /* Emit over the webterm WebSocket / postMessage channel.
       Adapt to however webterm currently sends data to the JS side. */
    webterm_send_json(
        "{\"type\":\"room\",\"cmd\":%s,\"id\":%u,\"name\":\"%s\"}",
        cmd ? quoted_json_string(cmd) : "null",
        (unsigned)room_id,
        json_escape(name_buf)
    );
}
```

**Compile guard** — wrap with:

```c
#ifdef TARGET_WEBTERM
void cart_room_changed(const char *cmd, zword room_id) { /* ... */ }
#else
#define cart_room_changed(cmd, id) ((void)0)
#endif
```

### What the short-name decoder needs to do

zVibe's status-line code already reads the short name from object property 1
(v3) or object property 18 (v5, the `name` property). Use exactly that path.
The decoded string should be the same text that appears in the status bar.

---

## JS-side: module structure

The Cartographer is three classes in one file: `cartographer.js`.
Drop it in `target/webterm/` alongside the existing webterm JS.

### File layout

```
target/webterm/
  index.html          (or equivalent — add <script src="cartographer.js">)
  cartographer.js     ← new file (source below)
  ...existing files...
```

### cartographer.js public surface

```js
// Construction — call once after the WebSocket/channel is established.
const cart = new Cartographer(stageElement);
// stageElement: the <div> that will contain the SVG map.

// Called by the webterm message handler on every room event.
cart.observe(cmd, roomId, roomName);
// cmd:      string | null   — the raw player input, or null for scripted moves
// roomId:   number          — the Z-machine object number (globally unique)
// roomName: string          — decoded short name

// Called when the player switches story (new game / restore to different story).
cart.reset();

// Programmatic visibility — or let the user drive via the UI.
cart.show();
cart.hide();   // map keeps updating silently; chip pulses on changes
```

Everything else (rendering, camera, level tabs, fit button) is internal.

---

## The engine (pure logic — ~133 lines, no DOM)

This block must be preserved exactly as-is. It is the tested core and has
no DOM dependencies, making it unit-testable in Node with no browser shim.

**Key data structures:**

```js
// node  — one entry per distinct room ID
{
  id:         string | number,  // the Z-machine object number
  name:       string,           // decoded short name
  level:      number,           // 0 = surface, +1 = up, -1 = down
  x:          number,           // grid column (integer)
  y:          number,           // grid row (integer, +y = south)
  displaced:  boolean,          // true if pushed off ideal cell by conflict
  crossLinks: [{to, dir}],      // level-crossing passages (up/down/in to new level)
}

// edge  — one entry per observed directional passage
{
  from:   id,       to: id,
  dir:    string | null,  // "n"|"s"|"e"|"w"|"ne"|"nw"|"se"|"sw"|"up"|"down"|"in"|"out" | null
  oneWay: boolean,        // false once the reverse has also been walked
  dashed: boolean,        // true for non-directional moves (enter, touch mirror, etc.)
  level:  number,         // level of both endpoints (cross-level passages are crossLinks, not edges)
}
```

**Direction normalization** (`normalizeDir(cmd)`) handles:
- Cardinal full-words: `north` → `n`, `northeast` → `ne`, etc.
- `up`/`down`/`in`/`out` and common aliases (`climb` → `up`, `enter` → `in`).
- Multi-word commands: only the first word is tested, so `go north`, `climb
  tree` (→ `up` trigger), `enter window` (→ `in`) all work.
- Returns `null` for non-movement commands; `null` cmd → `null` dir, which
  produces a dashed undirected edge if the room changes.

**Grid placement** (`place()`):
- New room from a cardinal direction: offset one cell in that direction.
- New room via `up`/`down`: same x,y as origin, new level.
- New room via `in`/`out` or null: nearest free neighbor of origin.
- Conflict (cell already occupied): ring-search outward (Chebyshev distance
  1, 2, … 8) for the nearest free cell; sets `displaced = true`.
- Fallback (truly pathological — more than 8 rings of density): overlap
  rather than lose the room. Has never triggered in practice.

**Edge merging** (`link()`):
- Reciprocal walks (`n` then `s` back) upgrade the existing one-way edge to
  two-way (`oneWay = false`) rather than creating a second edge.
- Duplicate observations (walking the same passage again) are silently
  dropped.
- Cross-level passages become `crossLinks` on both nodes and render as ▲/▼
  badge glyphs with click-to-jump, not as SVG edges.

**What makes the maze work**: Zork's maze rooms are all named "Maze" but have
distinct object numbers. Keying nodes by ID (not name) makes the mapper
automatically distinguish them and draw the one-way topology faithfully.

---

## Webterm message handler integration

In whatever function currently handles messages from the C side, add:

```js
ws.addEventListener("message", ev => {
    const msg = JSON.parse(ev.data);

    // ... existing message handling ...

    if (msg.type === "room") {
        cart.observe(msg.cmd, msg.id, msg.name);
    }
});
```

That's the entire integration seam. One message type, three fields.

---

## Layout: the two-pane split

The intended layout is a **vertical split**: terminal transcript on the left,
map on the right (default ~42% of viewport width). A draggable gutter between
them. On viewports ≤ 880px, this becomes a **horizontal split** with the map
band above the transcript.

The map can be hidden entirely (pure-text focus) via a toggle in the UI; when
hidden, `cart.observe()` still runs and the map builds silently. A UI chip
pulses amber when the hidden map has been updated.

### CSS custom properties to respect

```css
--mapw: 42%;          /* right-pane map width (desktop) — user-draggable */
--maph: 32vh;         /* top-pane map height (narrow) — user-draggable */
```

Both are set on `document.documentElement` by the gutter drag handler.

---

## Camera behavior

The camera defaults to **auto-fit**: every animation frame it computes the
bounding box of all nodes on the active level and frames them with 70px
padding, zoom clamped to `[0.2, 1.0]`. As the player explores, the view
zooms out organically to keep the whole known level visible.

Manual overrides:
- **Scroll wheel** — exits auto-fit, zooms `[0.2, 2.4]`.
- **Drag** — pans freely.
- **Fit button / `f` key** — restores auto-fit.

When auto-fit is off, the Fit button glows amber as a visible "way back".

The camera continues to recenter on the current room during auto-fit
(it's included in the bounding box computation), so the current position
is never lost even as the map grows.

---

## Persistence (not yet implemented — plan)

Key: `cart:${storyRelease}-${storySerial}-${storyChecksum}` in `localStorage`.
Value: JSON of `{ nodes: [...mapper.nodes.values()], edges: mapper.edges }`.

Write on every `observe()` call (debounced 500ms). Read on init and
call `cart.restore(data)` before the first `observe()`.

On RESTORE opcode: the interpreter knows the new room from global[0] after
the restore completes — just fire the normal room-change event with `null`
cmd, and the mapper will highlight the correct node without re-placing it.

The story header checksum is the right key because it uniquely identifies
the exact build of the story file, not just the title.

---

## Visual constants (match exactly)

```js
const CW = 156, CH = 112;      // grid cell size (px at zoom 1.0)
const RW = 118, RH = 62;       // room rect width/height
const RX = 9;                  // room rect corner radius
```

These are tuned for readability at the default zoom. Changing them requires
re-tuning the edge trim calculations in `Renderer.trim()`.

---

## What Claude Code should NOT change

- The `Mapper` class and its public `observe(cmd, id, name)` method — tested.
- The `normalizeDir()` function — the direction alias table is intentionally
  comprehensive and was validated against the full Zork I walkthrough.
- The edge upgrade logic in `Mapper.link()` — the one-way → two-way promotion
  is subtle; breaking it makes reciprocal passages look asymmetric.
- The ring-search fallback in `Mapper.resolve()` — the Chebyshev ring order
  matters for minimizing curved edges.

These are **integration points**, not implementation suggestions. Adapt the
surrounding webterm code to call into them; do not refactor them to fit the
surrounding code.

---

## Files to create

| File | Contents |
|------|----------|
| `target/webterm/cartographer.js` | `Mapper`, `Renderer`, `Camera`, `Cartographer` (public wrapper). Extract from prototype. |
| `target/webterm/cartographer.c` | `cart_room_changed()` with `#ifdef TARGET_WEBTERM` guard. |
| `target/webterm/cartographer.h` | Declaration + `#else` no-op macro. |

## Files to modify

| File | Change |
|------|--------|
| `target/webterm/index.html` | Add `<script src="cartographer.js">`, map pane `<div id="cart-stage">`, gutter div, CSS custom properties, toggle chip in header. |
| `target/webterm/*.js` (main webterm JS) | Wire `ws.message` → `cart.observe()`. Add gutter drag handler. Add level-tab render call. |
| `target/webterm/Makefile` (if applicable) | Add `cartographer.c` to `WEBTERM_OBJS`. |
| Main interpreter loop | Add `prev_location` snapshot + `cart_room_changed()` call. |

---

## Prototype reference

The working prototype is in the conversation that produced this document.
The key files are `zvibe-mapper.html` (standalone map demo) and
`zvibe-duo.html` (two-pane layout with live transcript). Both are single
self-contained HTML files and can be opened in a browser to verify behaviour
before and after integration. The engine section (delimited by
`// ENGINE-BEGIN` and `// ENGINE-END`) is the canonical copy to extract into
`cartographer.js`.
