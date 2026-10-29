/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/**
 * zterm.js - a small VT100 screen for version 4 stories in the webterm target.
 *
 * A V4 story draws its own screen: an upper window, cursor addressing, text
 * styles. The server runs the ANSI screen layer (src/screen/zvibe_screen.c) and
 * sends its bytes in {"type":"screen"} frames; this file draws them. It
 * understands exactly what that layer emits: cursor positioning (CSI H), erase
 * (CSI J, CSI K), scroll regions (CSI r), SGR 0/1/4/7, auto-wrap switching
 * (CSI ?7h/l), CR, LF and BS. Everything else is ignored.
 *
 * ZTermEngine is pure logic (no DOM) and is tested in Node against the C
 * emulator the console tests use. ZTerm adds the DOM and the keyboard:
 *
 *   const term = new ZTerm(document.getElementById("screen"), send);
 *   term.reset(rows, cols);          // on {"type":"screen","reset":true,...}
 *   term.write(msg.data);            // on every screen frame
 *   term.setWait("line" | "key" | null);
 *
 * send() receives {"type":"input","text":...} and {"type":"key","code":...}.
 * While a line is awaited, typed characters are echoed here (the server never
 * echoes them, so the two sides agree on the cursor) and Return sends the line;
 * the server then moves to the next line itself.
 */
"use strict";

const ATTR_REVERSE = 1, ATTR_BOLD = 2, ATTR_UNDERLINE = 4;

class ZTermEngine {
  constructor(rows, cols) {
    this.rows = rows;
    this.cols = cols;
    this.reset();
  }

  reset() {
    const n = this.rows * this.cols;
    this.ch = new Array(n).fill(" ");
    this.at = new Uint8Array(n);
    this.r = 0; this.c = 0;
    this.top = 0; this.bot = this.rows - 1;
    this.autowrap = true;
    this.attr = 0;
    this.state = 0;            // 0 ground, 1 after ESC, 2 CSI, 3 ESC + intermediate
    this.params = [0]; this.priv = false;
    this.dirty = true;
  }

  _blank(i0, i1) {
    for (let i = i0; i < i1; i++) { this.ch[i] = " "; this.at[i] = this.attr; }
  }

  _scroll() {
    const w = this.cols;
    for (let r = this.top; r < this.bot; r++) {
      for (let c = 0; c < w; c++) {
        this.ch[r * w + c] = this.ch[(r + 1) * w + c];
        this.at[r * w + c] = this.at[(r + 1) * w + c];
      }
    }
    this._blank(this.bot * w, (this.bot + 1) * w);   // blanks take the current rendition
  }

  _linefeed() {
    if (this.r === this.bot) this._scroll();
    else if (this.r < this.rows - 1) this.r++;
  }

  _csi(f) {
    const p = this.params, p0 = p[0] || 0, p1 = p[1] || 0, w = this.cols;
    if (this.priv) {
      if (p0 === 7) this.autowrap = (f === "h");
      return;
    }
    switch (f) {
      case "H": case "f":
        this.r = Math.min((p0 || 1) - 1, this.rows - 1);
        this.c = Math.min((p1 || 1) - 1, w - 1);
        break;
      case "r":
        if (p.length === 1 && p[0] === 0) { this.top = 0; this.bot = this.rows - 1; }
        else if (p0 >= 1 && p1 > p0 && p1 <= this.rows) { this.top = p0 - 1; this.bot = p1 - 1; }
        else break;                                  // terminals ignore an empty region
        this.r = 0; this.c = 0;                      // DECSTBM homes the cursor
        break;
      case "K":
        if (p0 === 0) this._blank(this.r * w + this.c, (this.r + 1) * w);
        else if (p0 === 1) this._blank(this.r * w, this.r * w + this.c + 1);
        else this._blank(this.r * w, (this.r + 1) * w);
        break;
      case "J":
        if (p0 === 2) this._blank(0, this.rows * w);
        break;
      case "m":
        if (p.length === 1 && p[0] === 0) { this.attr = 0; break; }
        for (const v of p) {
          if (v === 0) this.attr = 0;
          else if (v === 1) this.attr |= ATTR_BOLD;
          else if (v === 4) this.attr |= ATTR_UNDERLINE;
          else if (v === 7) this.attr |= ATTR_REVERSE;
        }
        break;
    }
  }

  /** Feed a string of terminal output (one byte per character). */
  write(s) {
    const w = this.cols;
    for (let i = 0; i < s.length; i++) {
      const code = s.charCodeAt(i);
      const b = s[i];
      if (this.state === 1) {
        if (b === "[") { this.state = 2; this.params = [0]; this.priv = false; }
        else if (b === "(" || b === ")" || b === "#") this.state = 3;   // charset etc.: one more byte
        else this.state = 0;
        continue;
      }
      if (this.state === 3) { this.state = 0; continue; }
      if (this.state === 2) {
        if (b === "?") this.priv = true;
        else if (b >= "0" && b <= "9") {
          const k = this.params.length - 1;
          this.params[k] = this.params[k] * 10 + (code - 48);
        } else if (b === ";") { if (this.params.length < 8) this.params.push(0); }
        else if (code >= 0x40 && code <= 0x7e) { this._csi(b); this.state = 0; }
        // other intermediate bytes: keep collecting
        continue;
      }
      if (code === 0x1b) { this.state = 1; continue; }
      if (b === "\r") { this.c = 0; continue; }
      if (b === "\b") { if (this.c > 0) this.c--; continue; }
      if (b === "\n") { this._linefeed(); continue; }
      if (code < 0x20 || code === 0x7f) continue;
      if (this.c >= w) {                             // only reachable with auto-wrap on
        this.c = 0;
        this._linefeed();
      }
      const idx = this.r * w + this.c;
      this.ch[idx] = b;
      this.at[idx] = this.attr;
      if (this.c < w - 1 || this.autowrap) this.c++;
    }
    this.dirty = true;
  }

  /** The screen as rows of text (for tests and copying). */
  lines() {
    const out = [];
    for (let r = 0; r < this.rows; r++) out.push(this.ch.slice(r * this.cols, (r + 1) * this.cols).join(""));
    return out;
  }
}

class ZTerm {
  constructor(host, send) {
    this.host = host;
    this.send = send;
    this.wait = null;
    this.typed = "";
    this.startCol = 0;
    this.engine = null;
    this.rowEls = [];
    this.raf = 0;
    this._onKey = e => this._key(e);
    document.addEventListener("keydown", this._onKey);
  }

  reset(rows, cols) {
    this.engine = new ZTermEngine(rows, cols);
    this.host.textContent = "";
    this.host.style.setProperty("--zcols", cols);
    this.rowEls = [];
    for (let r = 0; r < rows; r++) {
      const el = document.createElement("div");
      el.className = "zrow";
      this.host.appendChild(el);
      this.rowEls.push(el);
    }
    this.wait = null;
    this.typed = "";
    this._schedule();
  }

  write(data) {
    if (!this.engine) return;
    this.engine.write(data);
    this._schedule();
  }

  /** "line" while the game waits for a command, "key" for read_char, else null. */
  setWait(kind) {
    this.wait = kind;
    this.typed = "";
    if (kind === "line" && this.engine) this.startCol = this.engine.c;
    this._schedule();
  }

  _schedule() {
    if (this.raf) return;
    this.raf = requestAnimationFrame(() => { this.raf = 0; this._render(); });
  }

  _render() {
    const e = this.engine;
    if (!e) return;
    for (let r = 0; r < e.rows; r++) {
      const el = this.rowEls[r];
      el.textContent = "";
      let c = 0;
      while (c < e.cols) {
        const a = e.at[r * e.cols + c], cursor = (r === e.r && c === e.c && this.wait);
        let end = c + 1;
        while (!cursor && end < e.cols && e.at[r * e.cols + end] === a &&
               !(r === e.r && end === e.c && this.wait)) end++;
        const text = e.ch.slice(r * e.cols + c, r * e.cols + end).join("");
        if (a || cursor) {
          const span = document.createElement("span");
          span.textContent = text;
          span.className = ((a & ATTR_REVERSE) ? "zrev " : "") + ((a & ATTR_BOLD) ? "zbold " : "") +
                           ((a & ATTR_UNDERLINE) ? "zul " : "") + (cursor ? "zcur" : "");
          el.appendChild(span);
        } else {
          el.appendChild(document.createTextNode(text));
        }
        c = end;
      }
    }
  }

  _key(e) {
    if (!this.wait || !this.engine || e.ctrlKey || e.metaKey || e.altKey) return;
    const named = { Enter: 13, Backspace: 8, Delete: 8, Escape: 27,
                    ArrowUp: 129, ArrowDown: 130, ArrowLeft: 131, ArrowRight: 132 };
    if (this.wait === "key") {
      let code = named[e.key];
      if (code === undefined && e.key.length === 1) {
        code = e.key.charCodeAt(0);
        if (code < 32 || code > 126) return;
      }
      if (code === undefined) return;
      e.preventDefault();
      this.wait = null;
      this.send({ type: "key", code });
      return;
    }
    // line: the echo happens here; the server echoes only the final newline
    const eng = this.engine;
    if (e.key === "Enter") {
      e.preventDefault();
      const text = this.typed;
      this.wait = null; this.typed = "";
      this.send({ type: "input", text });
    } else if (e.key === "Backspace") {
      e.preventDefault();
      if (this.typed.length) {
        this.typed = this.typed.slice(0, -1);
        // explicit positions, like the screen layer: BS is unreliable at the pinned last column
        const at = `\x1b[${eng.r + 1};${this.startCol + this.typed.length + 1}H`;
        eng.write("\x1b[0m" + at + " " + at);
      }
    } else if (e.key.length === 1 && e.key >= " " && e.key <= "~") {
      e.preventDefault();
      if (this.typed.length < eng.cols - this.startCol) {      // the screen layer's limit
        this.typed += e.key;
        eng.write("\x1b[0m" + e.key);
      }
    }
    this._schedule();
  }
}

if (typeof module !== "undefined") module.exports = { ZTermEngine, ZTerm };
