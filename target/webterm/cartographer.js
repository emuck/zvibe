/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/**
 * cartographer.js — live auto-map for the webterm target.
 *
 * See CARTOGRAPHER.md for the full integration spec. Drop this file in
 * alongside the existing webterm JS and add:
 *
 *   <script src="cartographer.js"></script>
 *   <div id="cart-stage"></div>
 *
 * then, once per session:
 *
 *   const cart = new Cartographer(document.getElementById("cart-stage"));
 *
 * and on every {"type":"room"} message from the WebSocket:
 *
 *   cart.observe(msg.cmd, msg.id, msg.name);
 *
 * Cartographer builds all of its own DOM (SVG, level tabs, fit button,
 * legend) inside the stage element and injects its own styles — the host
 * page only needs to size/position #cart-stage (see --mapw / --maph) and
 * doesn't need to write map-specific CSS.
 */
"use strict";

/* ══════════════════════════════════════════════════════════
   ENGINE — pure logic, no DOM. Unit-testable in Node with no
   browser shim. Preserve this block exactly — see "What Claude
   Code should NOT change" in CARTOGRAPHER.md.
   ══════════════════════════════════════════════════════════ */
// ENGINE-BEGIN

const DIR = {
  n:[0,-1], s:[0,1], e:[1,0], w:[-1,0],
  ne:[1,-1], nw:[-1,-1], se:[1,1], sw:[-1,1],
};
const ALIAS = {
  north:"n", south:"s", east:"e", west:"w",
  northeast:"ne", northwest:"nw", southeast:"se", southwest:"sw",
  u:"up", d:"down", up:"up", down:"down", in:"in", out:"out",
  enter:"in", exit:"out", climb:"up",
};
const OPP = { n:"s", s:"n", e:"w", w:"e", ne:"sw", sw:"ne", nw:"se", se:"nw",
              up:"down", down:"up", in:"out", out:"in" };

function normalizeDir(cmd) {
  const w = cmd.trim().toLowerCase().split(/\s+/)[0];
  if (DIR[w] || w === "up" || w === "down" || w === "in" || w === "out") return ALIAS[w] || w;
  return ALIAS[w] || null;
}

/* Mapper: consumes room-change events, owns the graph. */
class Mapper {
  constructor() {
    this.nodes = new Map();       // id → {id, name, level, x, y, displaced, crossLinks:[]}
    this.edges = [];              // {from, to, dir|null, oneWay, dashed, level}
    this.current = null;
  }

  /* One event = the player's location global changed after `cmd`. */
  observe(cmd, id, name) {
    const prev = this.current ? this.nodes.get(this.current) : null;
    const dir = cmd == null ? null : normalizeDir(cmd);
    let node = this.nodes.get(id);
    const added = [];

    if (!node) {
      node = this.place(id, name, prev, dir);
      this.nodes.set(id, node);
      added.push({ type: "node", node });
    }
    if (prev && prev.id !== id) {
      const e = this.link(prev, node, dir);
      if (e) added.push({ type: e.cross ? "cross" : "edge", edge: e });
    }
    this.current = id;
    return added;
  }

  /* Grid placement with spiral fallback on conflict. */
  place(id, name, prev, dir) {
    if (!prev) return { id, name, level: 0, x: 0, y: 0, crossLinks: [] };

    const level = dir === "up"   ? prev.level + 1
                : dir === "down" ? prev.level - 1
                : prev.level;

    let ideal;
    if (dir === "up" || dir === "down") {
      ideal = [prev.x, prev.y];                       // same footprint, new level
    } else if (DIR[dir]) {
      ideal = [prev.x + DIR[dir][0], prev.y + DIR[dir][1]];
    } else {                                          // in/out/unclassified
      ideal = this.firstFreeNeighbor(prev, level) || [prev.x + 1, prev.y];
    }

    const [x, y, displaced] = this.resolve(ideal[0], ideal[1], level);
    return { id, name, level, x, y, displaced, crossLinks: [] };
  }

  firstFreeNeighbor(prev, level) {
    for (const d of ["e", "w", "s", "n"]) {
      const p = [prev.x + DIR[d][0], prev.y + DIR[d][1]];
      if (!this.occupied(p[0], p[1], level)) return p;
    }
    return null;
  }

  occupied(x, y, level) {
    for (const n of this.nodes.values())
      if (n.level === level && n.x === x && n.y === y) return true;
    return false;
  }

  /* Ring-search outward from the ideal cell for the nearest free one. */
  resolve(x, y, level) {
    if (!this.occupied(x, y, level)) return [x, y, false];
    for (let r = 1; r <= 8; r++) {
      const ring = [];
      for (let dx = -r; dx <= r; dx++)
        for (let dy = -r; dy <= r; dy++)
          if (Math.max(Math.abs(dx), Math.abs(dy)) === r)
            ring.push([x + dx, y + dy, dx*dx + dy*dy]);
      ring.sort((a, b) => a[2] - b[2]);
      for (const [cx, cy] of ring)
        if (!this.occupied(cx, cy, level)) return [cx, cy, true];
    }
    return [x, y, true];  // pathological; overlap rather than lose the room
  }

  /* Record an observed passage. Merges reciprocal pairs into two-way. */
  link(a, b, dir) {
    if (a.level !== b.level) {                        // level link → badges
      if (!a.crossLinks.some(c => c.to === b.id)) {
        a.crossLinks.push({ to: b.id, dir: dir || "?" });
        b.crossLinks.push({ to: a.id, dir: dir ? OPP[dir] : "?" });
        return { from: a.id, to: b.id, cross: true };
      }
      return null;
    }
    // Reciprocal of an existing one-way? Upgrade it.
    const rev = this.edges.find(e =>
      e.from === b.id && e.to === a.id && e.oneWay &&
      dir && e.dir && e.dir === OPP[dir]);
    if (rev) { rev.oneWay = false; rev.upgraded = true; return null; }

    // Exact duplicate observation?
    if (this.edges.some(e => e.from === a.id && e.to === b.id &&
        (e.dir === dir || (!e.dir && !dir)))) return null;

    const e = { from: a.id, to: b.id, dir, oneWay: !!dir, dashed: !dir,
                level: a.level };
    if (!dir) e.oneWay = false;                       // dashed = undirected
    this.edges.push(e);
    return e;
  }

  levels() {
    const s = new Set([...this.nodes.values()].map(n => n.level));
    return [...s].sort((a, b) => b - a);
  }
}
// ENGINE-END

/* Node/CommonJS export for headless unit testing of the engine block. */
if (typeof module !== "undefined" && module.exports) {
  module.exports = { Mapper, normalizeDir, DIR, ALIAS, OPP };
}

/* ══════════════════════════════════════════════════════════
   RENDERER — SVG, grid-paper field, lantern-lit current room.
   Visual constants match CARTOGRAPHER.md exactly.
   ══════════════════════════════════════════════════════════ */
const CW = 156, CH = 112;          // grid cell
const RW = 118, RH = 62, RX = 9;   // room rect
const NS = "http://www.w3.org/2000/svg";

const cx = n => n.x * CW, cy = n => n.y * CH;

const el = (tag, attrs = {}, parent) => {
  const n = document.createElementNS(NS, tag);
  for (const [k, v] of Object.entries(attrs)) n.setAttribute(k, v);
  if (parent) parent.appendChild(n);
  return n;
};

class Renderer {
  /* onJump(id) is called when a cross-level badge is clicked. */
  constructor(svg, mapper, onJump, reduced) {
    this.svg = svg; this.m = mapper; this.onJump = onJump;
    this.reduced = reduced;
    this.levelGroups = new Map();
    this.activeLevel = 0;
    this.buildDefs();
    el("rect", { fill: "url(#cart-grid)", x: -6000, y: -6000,
                 width: 12000, height: 12000 }, svg);
    this.world = el("g", {}, svg);
    this.lamp = el("circle", { r: 130, fill: "url(#cart-lamp)",
                               style: "pointer-events:none" });
    this.marker = null;
  }

  buildDefs() {
    const defs = el("defs", {}, this.svg);
    const pat = el("pattern", { id: "cart-grid", width: CW/2, height: CH/2,
                                patternUnits: "userSpaceOnUse" }, defs);
    el("path", { d: `M ${CW/2} 0 L 0 0 0 ${CH/2}`, fill: "none",
                 stroke: "var(--cart-grid-line)", "stroke-width": 1 }, pat);
    const lamp = el("radialGradient", { id: "cart-lamp" }, defs);
    el("stop", { offset: "0%",  "stop-color": "var(--cart-lamp-0)" }, lamp);
    el("stop", { offset: "55%", "stop-color": "var(--cart-lamp-55)" }, lamp);
    el("stop", { offset: "100%","stop-color": "var(--cart-lamp-100)" }, lamp);
    const glow = el("filter", { id: "cart-glow", x: "-60%", y: "-60%",
                                width: "220%", height: "220%" }, defs);
    el("feGaussianBlur", { stdDeviation: 5, result: "b" }, glow);
    const mg = el("feMerge", {}, glow);
    el("feMergeNode", { in: "b" }, mg);
    el("feMergeNode", { in: "SourceGraphic" }, mg);
    const mk = el("marker", { id: "cart-arrow", viewBox: "0 0 10 10",
      refX: 8, refY: 5, markerWidth: 6.5, markerHeight: 6.5,
      orient: "auto-start-reverse" }, defs);
    el("path", { d: "M 0 0 L 10 5 L 0 10 z", fill: "var(--cart-ink-dim)" }, mk);
  }

  group(level) {
    if (!this.levelGroups.has(level)) {
      const g = el("g", { opacity: level === this.activeLevel ? 1 : 0,
                          style: "transition: opacity .35s" }, this.world);
      g.style.pointerEvents = level === this.activeLevel ? "auto" : "none";
      this.levelGroups.set(level, { g,
        edges: el("g", {}, g), nodes: el("g", {}, g) });
    }
    return this.levelGroups.get(level);
  }

  setLevel(level) {
    this.activeLevel = level;
    for (const [lv, { g }] of this.levelGroups) {
      g.setAttribute("opacity", lv === level ? 1 : 0);
      g.style.pointerEvents = lv === level ? "auto" : "none";
    }
  }

  /* ── nodes ── */
  drawNode(n) {
    const { nodes } = this.group(n.level);
    const g = el("g", { transform: `translate(${cx(n)},${cy(n)})`,
                        "data-id": n.id }, nodes);
    el("rect", { x: -RW/2, y: -RH/2, width: RW, height: RH, rx: RX,
                 fill: "var(--cart-room-fill)", stroke: "var(--cart-ink-dim)",
                 "stroke-width": 1.25 }, g);
    const lines = this.wrap(n.name, 15);
    const t = el("text", { "text-anchor": "middle", fill: "var(--cart-ink)",
                 "font-size": 11.5, "font-family": "var(--cart-mono)",
                 y: lines.length > 1 ? -3 : 4 }, g);
    lines.forEach((ln, i) =>
      el("tspan", { x: 0, dy: i ? 14 : 0 }, t).textContent = ln);
    if (!this.reduced) {
      g.animate([{ opacity: 0, transform:
        `translate(${cx(n)}px,${cy(n)}px) scale(.6)` },
        { opacity: 1, transform:
        `translate(${cx(n)}px,${cy(n)}px) scale(1)` }],
        { duration: 420, easing: "cubic-bezier(.2,1.4,.4,1)" });
    }
    return g;
  }

  drawCrossBadges(n) {
    const g = this.group(n.level).nodes
      .querySelector(`g[data-id="${n.id}"]`);
    if (!g) return;
    g.querySelectorAll(".cart-badge").forEach(b => b.remove());
    n.crossLinks.forEach((c, i) => {
      const up = c.dir === "up";
      const b = el("text", { class: "cart-badge", x: RW/2 - 12 - i * 14,
        y: -RH/2 + 13, "font-size": 11, fill: "var(--cart-lantern)",
        "text-anchor": "middle", style: "cursor:pointer" }, g);
      b.textContent = up ? "▲" : c.dir === "down" ? "▼" : "◆";
      b.addEventListener("click", ev => {
        ev.stopPropagation();
        if (this.onJump) this.onJump(c.to);
      });
      el("title", {}, b).textContent =
        (up ? "Up to " : c.dir === "down" ? "Down to " : "Leads to ") +
        (this.m.nodes.get(c.to)?.name ?? "?");
    });
  }

  /* ── edges ── */
  drawEdge(e) {
    const a = this.m.nodes.get(e.from), b = this.m.nodes.get(e.to);
    const { edges } = this.group(e.level);

    // Parallel edges between the same pair get opposite bows.
    const twin = this.m.edges.some(o => o !== e &&
      o.from === e.to && o.to === e.from && o.level === e.level);
    const straightAligned = e.dir && DIR[e.dir] &&
      b.x - a.x === DIR[e.dir][0] && b.y - a.y === DIR[e.dir][1];
    const bow = twin ? 20 : (straightAligned && !e.dashed ? 0 : 16);

    const p1 = this.trim(a, b), p2 = this.trim(b, a);
    let d;
    if (bow === 0) {
      d = `M ${p1.x} ${p1.y} L ${p2.x} ${p2.y}`;
    } else {
      const mx = (p1.x + p2.x)/2, my = (p1.y + p2.y)/2;
      const dx = p2.x - p1.x, dy = p2.y - p1.y;
      const len = Math.hypot(dx, dy) || 1;
      const ox = -dy/len * bow, oy = dx/len * bow;
      d = `M ${p1.x} ${p1.y} Q ${mx+ox} ${my+oy} ${p2.x} ${p2.y}`;
    }
    const path = el("path", { d, fill: "none", stroke: "var(--cart-ink-dim)",
      "stroke-width": 1.5,
      "marker-end": e.oneWay ? "url(#cart-arrow)" : "none" }, edges);
    if (e.dashed) path.setAttribute("stroke-dasharray", "4 6");

    if (e.oneWay && e.dir) {                          // tiny direction tag
      const mx = (p1.x + p2.x)/2, my = (p1.y + p2.y)/2;
      const dx = p2.x-p1.x, dy = p2.y-p1.y, len = Math.hypot(dx,dy)||1;
      const off = bow ? bow + 9 : 10;
      el("text", { x: mx - dy/len*off, y: my + dx/len*off + 3,
        "font-size": 9, fill: "var(--cart-ink-faint)", "text-anchor": "middle",
        "font-family": "var(--cart-mono)" }, edges)
        .textContent = e.dir.toUpperCase();
    }
    if (!this.reduced && !e.dashed) {                 // ink-in animation
      const L = path.getTotalLength();
      path.style.strokeDasharray = L;
      path.style.strokeDashoffset = L;
      path.animate([{ strokeDashoffset: L }, { strokeDashoffset: 0 }],
        { duration: 380, easing: "ease-out" })
        .finished.then(() => {
          path.style.strokeDasharray = "none";
          path.style.strokeDashoffset = 0;
        });
    }
    e.el = path;
    return path;
  }

  /* Upgraded one-way → two-way: drop the arrowhead. */
  refreshUpgraded() {
    for (const e of this.m.edges)
      if (e.upgraded && e.el) {
        e.el.setAttribute("marker-end", "none");
        e.upgraded = false;
      }
  }

  /* Shorten segment endpoint to the border of room rect at node n. */
  trim(n, toward) {
    const dx = cx(toward) - cx(n), dy = cy(toward) - cy(n);
    const sx = dx === 0 ? Infinity : (RW/2 + 4) / Math.abs(dx);
    const sy = dy === 0 ? Infinity : (RH/2 + 4) / Math.abs(dy);
    const s = Math.min(sx, sy, 0.5);
    return { x: cx(n) + dx * s, y: cy(n) + dy * s };
  }

  /* ── current-room lantern ── */
  highlight(id) {
    const n = this.m.nodes.get(id);
    if (!n) return;
    const grp = this.group(n.level);
    if (this.marker) this.marker.remove();
    if (this.lamp.parentNode) this.lamp.remove();
    grp.edges.before(this.lamp);
    this.lamp.setAttribute("cx", cx(n));
    this.lamp.setAttribute("cy", cy(n));
    this.marker = el("rect", { x: cx(n)-RW/2-4, y: cy(n)-RH/2-4,
      width: RW+8, height: RH+8, rx: RX+3, fill: "none",
      stroke: "var(--cart-lantern)", "stroke-width": 1.75,
      filter: "url(#cart-glow)", style: "pointer-events:none" }, grp.nodes);
  }

  pulse(id) {
    const n = this.m.nodes.get(id);
    if (!n) return;
    const g = this.group(n.level).nodes.querySelector(`g[data-id="${id}"]`);
    if (g && !this.reduced)
      g.animate([{ opacity: 1 }, { opacity: 0.25 }, { opacity: 1 },
                 { opacity: 0.25 }, { opacity: 1 }], { duration: 900 });
  }

  wrap(text, width) {
    if (text.length <= width) return [text];
    const words = text.split(" "), lines = [""];
    for (const w of words) {
      const cur = lines[lines.length-1];
      if ((cur + " " + w).trim().length > width && cur) lines.push(w);
      else lines[lines.length-1] = (cur + " " + w).trim();
    }
    return lines.slice(0, 2);
  }
}

/* ══════════════════════════════════════════════════════════
   CAMERA — auto-fit frames the explored level; wheel/drag take
   manual control; fit() restores. Zoom and position both lerp.
   ══════════════════════════════════════════════════════════ */
class Camera {
  constructor(svg, stage, fitFn, reduced) {
    this.svg = svg; this.stage = stage; this.fitFn = fitFn;
    this.reduced = reduced;
    this.x = 0; this.y = 0; this.zoom = 1;
    this.tx = 0; this.ty = 0; this.tzoom = 1;
    this.fitMode = true;      // default: frame everything explored
    this.manual = false;      // true while user is panning
    this._raf = null;
    this.bindInput();
    const tick = () => {
      if (this.fitMode) {
        const t = this.fitFn();
        if (t) { this.tx = t.x; this.ty = t.y; this.tzoom = t.zoom; }
      }
      if (!this.manual) {
        if (this.reduced) { this.x = this.tx; this.y = this.ty; this.zoom = this.tzoom; }
        else {
          this.x += (this.tx - this.x) * 0.08;
          this.y += (this.ty - this.y) * 0.08;
          this.zoom += (this.tzoom - this.zoom) * 0.08;
        }
      }
      this.stage.dataset.cartFit = this.fitMode;
      this.apply();
      this._raf = requestAnimationFrame(tick);
    };
    this._raf = requestAnimationFrame(tick);
  }
  follow(px, py) {            // new room: resume auto motion
    this.manual = false;
    if (!this.fitMode) { this.tx = px; this.ty = py; }
  }
  fit() { this.fitMode = true; this.manual = false; }
  apply() {
    const w = Math.max(1, this.stage.clientWidth) / this.zoom;
    const h = Math.max(1, this.stage.clientHeight) / this.zoom;
    this.svg.setAttribute("viewBox",
      `${this.x - w/2} ${this.y - h/2} ${w} ${h}`);
  }
  bindInput() {
    let drag = null;
    this.stage.addEventListener("pointerdown", e => {
      if (e.target.closest("button")) return;
      drag = { x: e.clientX, y: e.clientY };
      this.stage.classList.add("cart-dragging");
      this.manual = true; this.fitMode = false;
    });
    window.addEventListener("pointermove", e => {
      if (!drag) return;
      this.x -= (e.clientX - drag.x) / this.zoom;
      this.y -= (e.clientY - drag.y) / this.zoom;
      drag = { x: e.clientX, y: e.clientY };
    });
    window.addEventListener("pointerup", () => {
      drag = null; this.stage.classList.remove("cart-dragging");
    });
    this.stage.addEventListener("wheel", e => {
      e.preventDefault();
      if (this.fitMode) {     // leave fit from wherever fit put us
        this.tx = this.x; this.ty = this.y;
        this.fitMode = false;
      }
      const f = e.deltaY < 0 ? 1.12 : 1/1.12;
      this.tzoom = Math.min(2.4, Math.max(0.2, this.tzoom * f));
    }, { passive: false });
  }
  destroy() {
    if (this._raf) cancelAnimationFrame(this._raf);
  }
}

/* ══════════════════════════════════════════════════════════
   Self-contained styles, injected once. The host page only needs
   to size/position #cart-stage (see README / CARTOGRAPHER.md for
   the --mapw / --maph custom properties); everything visual inside
   the pane is owned by Cartographer.
   ══════════════════════════════════════════════════════════ */
const CART_CSS = `
#cart-stage {
  --cart-field:      #10131d;
  --cart-grid-line:  rgba(122,138,180,0.07);
  --cart-ink:        #e9e3d0;
  --cart-ink-dim:    #8d8a79;
  --cart-ink-faint:  #55534a;
  --cart-lantern:    #f5b04c;
  --cart-lantern-hi: #ffd9a0;
  --cart-rule:       rgba(233,227,208,0.08);
  --cart-mono: ui-monospace, "SF Mono", "Cascadia Code", Menlo, Consolas, monospace;
  --cart-room-fill:  rgba(20,24,38,0.92);
  --cart-lamp-0:     rgba(245,176,76,0.16);
  --cart-lamp-55:    rgba(245,176,76,0.05);
  --cart-lamp-100:   rgba(245,176,76,0);
  position: relative; overflow: hidden;
  background: var(--cart-field); cursor: grab;
  font-family: var(--cart-mono);
}
#cart-stage.cart-dragging { cursor: grabbing; }
#cart-stage svg { display: block; width: 100%; height: 100%; touch-action: none; }
#cart-stage .cart-vignette {
  position: absolute; inset: 0; pointer-events: none;
  background: radial-gradient(ellipse 75% 70% at 50% 46%,
    transparent 55%, rgba(6,7,12,0.55) 88%, rgba(4,5,9,0.85) 100%);
}
#cart-stage nav.cart-tabs {
  display: flex; gap: 6px; position: absolute; top: 12px; left: 14px; z-index: 5;
}
#cart-stage nav.cart-tabs button {
  font-family: var(--cart-mono); font-size: 11px; letter-spacing: 0.08em;
  background: rgba(16,19,29,0.85); color: var(--cart-ink-dim);
  border: 1px solid rgba(233,227,208,0.14); border-radius: 4px;
  padding: 5px 12px; cursor: pointer; backdrop-filter: blur(4px);
  transition: color .2s, border-color .2s;
}
#cart-stage nav.cart-tabs button:hover { color: var(--cart-ink); }
#cart-stage nav.cart-tabs button.active {
  color: var(--cart-lantern); border-color: rgba(245,176,76,0.5);
}
#cart-stage .cart-legend {
  position: absolute; right: 14px; bottom: 12px; z-index: 5;
  font-size: 10px; color: var(--cart-ink-dim);
  display: flex; flex-direction: column; gap: 5px;
  background: rgba(10,12,20,0.7); backdrop-filter: blur(4px);
  border: 1px solid var(--cart-rule); border-radius: 6px; padding: 9px 11px;
}
#cart-stage .cart-legend .row { display: flex; align-items: center; gap: 8px; }
#cart-stage .cart-legend svg { width: 30px; height: 10px; overflow: visible; }
#cart-stage .cart-fitbtn {
  position: absolute; top: 12px; right: 12px; z-index: 6;
  font-family: var(--cart-mono); font-size: 11px; letter-spacing: 0.08em;
  background: rgba(16,19,29,0.85); color: var(--cart-ink-dim);
  border: 1px solid rgba(233,227,208,0.14); border-radius: 4px;
  padding: 5px 12px; cursor: pointer; backdrop-filter: blur(4px);
  transition: color .2s, border-color .2s;
}
#cart-stage[data-cart-fit="false"] .cart-fitbtn {
  color: var(--cart-lantern); border-color: rgba(245,176,76,0.5);
}
@media (prefers-reduced-motion: reduce) {
  #cart-stage nav.cart-tabs button, #cart-stage .cart-fitbtn { transition: none; }
}
`;

function injectStylesOnce() {
  if (document.getElementById("cartographer-styles")) return;
  const style = document.createElement("style");
  style.id = "cartographer-styles";
  style.textContent = CART_CSS;
  document.head.appendChild(style);
}

const LEVEL_NAME = lv => lv === 0 ? "Surface"
  : lv > 0 ? `Above${lv > 1 ? " +" + lv : ""}` : `Below${lv < -1 ? " " + lv : ""}`;

/* ══════════════════════════════════════════════════════════
   Cartographer — public wrapper. See CARTOGRAPHER.md § JS-side
   for the full contract. Public surface:
     new Cartographer(stageElement)
     cart.observe(cmd, roomId, roomName)
     cart.reset()
     cart.show() / cart.hide()
     cart.serialize() / cart.restore(data)   — see § Persistence
   Everything else here (DOM construction, camera, tabs, fit
   button) is an internal implementation detail.
   ══════════════════════════════════════════════════════════ */
class Cartographer {
  constructor(stageElement) {
    if (!stageElement) throw new Error("Cartographer: stageElement is required");
    this.stage = stageElement;
    this.hidden = false;
    this.reduced = matchMedia("(prefers-reduced-motion: reduce)").matches;

    injectStylesOnce();
    this._buildDom();

    this.mapper = new Mapper();
    this.renderer = new Renderer(this.svg, this.mapper,
      id => this.jumpTo(id), this.reduced);
    this.camera = new Camera(this.svg, this.stage,
      () => this._fitTarget(), this.reduced);

    this.fitBtn.onclick = () => this.camera.fit();
    this._keyHandler = e => {
      const tag = e.target && e.target.tagName;
      if (tag === "INPUT" || tag === "TEXTAREA" || tag === "BUTTON") return;
      if (e.key.toLowerCase() === "f") this.camera.fit();
    };
    addEventListener("keydown", this._keyHandler);

    this._renderTabs();
  }

  /* ── public API ── */

  /** cmd: string|null, id: string|number, name: string */
  observe(cmd, id, name) {
    const changes = this.mapper.observe(cmd, id, name);
    const node = this.mapper.nodes.get(id);

    for (const c of changes) {
      if (c.type === "node") this.renderer.drawNode(c.node);
      else if (c.type === "edge") this.renderer.drawEdge(c.edge);
      else if (c.type === "cross") {
        this.renderer.drawCrossBadges(this.mapper.nodes.get(c.edge.from));
        this.renderer.drawCrossBadges(this.mapper.nodes.get(c.edge.to));
      }
    }
    this.renderer.refreshUpgraded();
    if (node.level !== this.renderer.activeLevel) this.renderer.setLevel(node.level);
    this.renderer.highlight(id);
    this.camera.follow(cx(node), cy(node));
    if (changes.some(c => c.type === "cross")) this.renderer.pulse(id);
    this._renderTabs();

    if (changes.length) this._notify(changes);
    return changes;
  }

  reset() {
    this.mapper = new Mapper();
    this.svg.innerHTML = "";
    this.renderer = new Renderer(this.svg, this.mapper,
      id => this.jumpTo(id), this.reduced);
    this.camera.fit();
    this._renderTabs();
  }

  show() { this.hidden = false; }
  hide() { this.hidden = true; }

  /**
   * Plain-JSON snapshot of the graph, suitable for JSON.stringify() and
   * later restore(). See CARTOGRAPHER.md § Persistence — the host page
   * decides when and where to store this (e.g. localStorage keyed by the
   * "story" message); Cartographer only knows how to (de)serialize itself.
   */
  serialize() {
    return {
      nodes: [...this.mapper.nodes.values()].map(n => ({
        id: n.id, name: n.name, level: n.level, x: n.x, y: n.y,
        displaced: !!n.displaced,
        crossLinks: n.crossLinks.map(c => ({ to: c.to, dir: c.dir })),
      })),
      edges: this.mapper.edges.map(e => ({
        from: e.from, to: e.to, dir: e.dir,
        oneWay: e.oneWay, dashed: e.dashed, level: e.level,
      })),
    };
  }

  /**
   * Rebuild the graph from a previous serialize() and redraw it without
   * per-element entrance animation. Call before the first observe() —
   * the server re-announces the current room (cmd:null) on load/resume,
   * so the next observe() call will highlight the right node without
   * re-placing it.
   */
  restore(data) {
    if (!data || !Array.isArray(data.nodes) || !Array.isArray(data.edges)) return;

    this.mapper = new Mapper();
    for (const n of data.nodes) {
      this.mapper.nodes.set(n.id, {
        ...n, crossLinks: (n.crossLinks || []).map(c => ({ ...c })),
      });
    }
    this.mapper.edges = data.edges.map(e => ({ ...e }));

    this.svg.innerHTML = "";
    this.renderer = new Renderer(this.svg, this.mapper,
      id => this.jumpTo(id), /* reduced, for a static bulk redraw */ true);
    for (const n of this.mapper.nodes.values()) this.renderer.drawNode(n);
    for (const e of this.mapper.edges) this.renderer.drawEdge(e);
    for (const n of this.mapper.nodes.values())
      if (n.crossLinks.length) this.renderer.drawCrossBadges(n);
    this.renderer.reduced = this.reduced;   // resume normal motion for live updates

    this.camera.fit();
    this._renderTabs();
  }

  /* ── stats, read-only, for host header/chrome ── */
  get roomCount() { return this.mapper.nodes.size; }
  get edgeCount() {
    return this.mapper.edges.length +
      [...this.mapper.nodes.values()]
        .reduce((s, n) => s + n.crossLinks.length, 0) / 2;
  }

  /* ── internal ── */

  jumpTo(id) {
    const n = this.mapper.nodes.get(id);
    if (!n) return;
    this.renderer.setLevel(n.level);
    this.camera.follow(cx(n), cy(n));
    this.renderer.pulse(id);
    this._renderTabs();
  }

  _notify(changes) {
    this.stage.dispatchEvent(new CustomEvent("cart:update", {
      detail: { changes, hidden: this.hidden,
                rooms: this.roomCount, edges: this.edgeCount },
    }));
  }

  /* Bounding box of the active level → camera target, padded and
     clamped so a lone room never fills the pane. */
  _fitTarget() {
    const nodes = [...this.mapper.nodes.values()]
      .filter(n => n.level === this.renderer.activeLevel);
    if (!nodes.length) return null;
    const PAD = 70;
    let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
    for (const n of nodes) {
      x0 = Math.min(x0, cx(n) - RW/2); x1 = Math.max(x1, cx(n) + RW/2);
      y0 = Math.min(y0, cy(n) - RH/2); y1 = Math.max(y1, cy(n) + RH/2);
    }
    const bw = x1 - x0 + PAD*2, bh = y1 - y0 + PAD*2;
    const zoom = Math.max(0.2, Math.min(
      this.stage.clientWidth / bw, this.stage.clientHeight / bh, 1.0));
    return { x: (x0 + x1)/2, y: (y0 + y1)/2, zoom };
  }

  _renderTabs() {
    this.tabs.innerHTML = "";
    for (const lv of this.mapper.levels()) {
      const b = document.createElement("button");
      b.textContent = LEVEL_NAME(lv);
      if (lv === this.renderer.activeLevel) b.classList.add("active");
      b.onclick = () => {
        this.renderer.setLevel(lv);
        const cur = this.mapper.nodes.get(this.mapper.current);
        const target = cur && cur.level === lv ? cur :
          [...this.mapper.nodes.values()].find(n => n.level === lv);
        if (target) this.camera.follow(cx(target), cy(target));
        this._renderTabs();
      };
      this.tabs.appendChild(b);
    }
  }

  _buildDom() {
    this.stage.dataset.cartFit = "true";
    this.svg = el("svg", { "aria-label": "Auto-generated game map" }, this.stage);

    this.vignette = document.createElement("div");
    this.vignette.className = "cart-vignette";
    this.stage.appendChild(this.vignette);

    this.tabs = document.createElement("nav");
    this.tabs.className = "cart-tabs";
    this.stage.appendChild(this.tabs);

    this.fitBtn = document.createElement("button");
    this.fitBtn.className = "cart-fitbtn";
    this.fitBtn.title = "Fit explored level in view (f)";
    this.fitBtn.textContent = "⌖ Fit";
    this.stage.appendChild(this.fitBtn);

    this.legend = document.createElement("div");
    this.legend.className = "cart-legend";
    this.legend.innerHTML = `
      <div class="row"><svg><line x1="0" y1="5" x2="30" y2="5" stroke="#8d8a79" stroke-width="1.5"/></svg>two-way passage</div>
      <div class="row"><svg><line x1="0" y1="5" x2="24" y2="5" stroke="#8d8a79" stroke-width="1.5"/><path d="M24 1 30 5 24 9z" fill="#8d8a79"/></svg>one-way passage</div>
      <div class="row"><svg><line x1="0" y1="5" x2="30" y2="5" stroke="#8d8a79" stroke-width="1.5" stroke-dasharray="3 4"/></svg>uncharted move</div>
      <div class="row"><svg><text x="10" y="9" fill="#f5b04c" font-size="10">▲▼</text></svg>level link</div>
    `;
    this.stage.appendChild(this.legend);
  }
}
