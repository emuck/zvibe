// Copyright (c) 2025 Martin R. Raumann
// SPDX-License-Identifier: BSD-3-Clause
//
// Checks zterm.js's screen engine against the C reference emulator: for the same
// terminal bytes (from the real ANSI screen layer) both must show the same text,
// attributes and cursor. Usage: node test_zterm.js <ansi_dump binary> <v4screen.z4>
"use strict";
const { execFileSync } = require("child_process");
const fs = require("fs");
const os = require("os");
const path = require("path");
const { ZTermEngine } = require("../zterm.js");

const [bin, story] = process.argv.slice(2);
const dir = fs.mkdtempSync(path.join(os.tmpdir(), "zterm-"));
let checks = 0, failed = 0;

function compare(name, prefix) {
  const bytes = fs.readFileSync(prefix + ".ansi").toString("latin1");
  const ref = fs.readFileSync(prefix + ".grid", "latin1").split("\n");
  const eng = new ZTermEngine(24, 80);
  eng.write(bytes);
  const lines = eng.lines();
  let bad = 0;
  for (let r = 0; r < 24 && bad < 3; r++) {
    checks++;
    if (lines[r] !== ref[r]) { failed++; bad++; console.log(`FAIL ${name} text row ${r + 1}:\n  js  '${lines[r]}'\n  ref '${ref[r]}'`); }
    checks++;
    const at = Array.from(eng.at.slice(r * 80, r * 80 + 80)).join("");
    if (at !== ref[24 + r]) { failed++; bad++; console.log(`FAIL ${name} attr row ${r + 1}:\n  js  ${at}\n  ref ${ref[24 + r]}`); }
  }
  checks++;
  const cur = `cursor ${eng.r} ${eng.c}`;
  if (cur !== ref[48]) { failed++; console.log(`FAIL ${name} ${cur} vs ${ref[48]}`); }
}

function dump(mode, arg, name) {
  const prefix = path.join(dir, name);
  execFileSync(bin, [mode, arg, prefix]);
  compare(name, prefix);
}

dump("story", story, "story");
for (let seed = 1; seed <= 200; seed++) dump("fuzz", String(seed), "fuzz" + seed);

// the engine on its own: an escape sequence split across writes, and unknown sequences
{
  const e = new ZTermEngine(24, 80);
  e.write("\x1b[1"); e.write(";5H"); e.write("X");
  checks++; if (e.lines()[0][4] !== "X") { failed++; console.log("FAIL split CSI across writes"); }
  e.write("\x1b(B\x1b[?25h\x1b[4lZ");
  checks++; if (e.lines()[0][5] !== "Z") { failed++; console.log("FAIL ignored sequences must not print"); }
}

// ZTerm (keys and echo) with a stand-in for the DOM
{
  const listeners = {};
  global.document = {
    addEventListener: (t, f) => { listeners[t] = f; },
    createElement: () => ({ appendChild() {}, set textContent(v) {}, style: {}, className: "" }),
    createTextNode: () => ({}),
  };
  global.requestAnimationFrame = f => 1;
  const { ZTerm: Z } = require("../zterm.js");
  const host = { textContent: "", appendChild() {}, style: { setProperty() {} } };
  const sent = [];
  const t = new Z(host, m => sent.push(m));
  const key = k => { let prevented = false; listeners.keydown({ key: k, preventDefault() { prevented = true; } }); return prevented; };
  const ok = (cond, what) => { checks++; if (!cond) { failed++; console.log("FAIL zterm " + what); } };

  t.reset(24, 80);
  t.write("\x1b[24;1Hprompt> ");
  t.setWait("line");
  key("h"); key("i"); key("x"); key("Backspace");
  ok(t.engine.lines()[23].startsWith("prompt> hi "), "typed text is echoed and Backspace erases");
  ok(t.engine.c === 10, "cursor follows the typing");
  key("Enter");
  ok(JSON.stringify(sent[0]) === JSON.stringify({ type: "input", text: "hi" }), "Return sends the line");
  ok(t.wait === null, "no more typing after Return");
  ok(!key("z") && sent.length === 1, "keys are ignored when nothing is awaited");

  // the line is limited to the room left on the screen's row, like the screen layer
  t.write("\r\n\x1b[24;1H> ");
  t.setWait("line");
  for (let i = 0; i < 100; i++) key("a");
  key("Enter");
  ok(sent[1].text.length === 78, "a line stops at the right edge (78 of 80 columns after a 2-column prompt)");

  // a prompt that already fills the row: there is no room, as the screen layer sees it
  t.write("\x1b[?7l\r\n\x1b[24;1H" + "p".repeat(80));        // auto-wrap off, as the screen layer sets it
  t.setWait("line");
  key("x"); key("y");
  key("Enter");
  ok(sent[2].type === "input" && sent[2].text === "", "nothing is accepted after a prompt that fills the row");
  ok(t.engine.lines()[23] === "p".repeat(80), "and the prompt's last column was not overwritten");

  // the story's text buffer: the server says how many characters it takes
  t.write("\r\n\x1b[24;1H> ");
  t.setWait("line", 5);
  for (let i = 0; i < 12; i++) key("a");
  key("Enter");
  ok(sent[3].text === "aaaaa", "a line stops at the story's input limit (5), not the width of the screen");

  // read_char
  t.setWait("key");
  ok(key("ArrowUp") && sent[4].code === 129, "arrow up is ZSCII 129");
  t.setWait("key"); key("Escape"); ok(sent[5].code === 27, "Escape is 27");
  t.setWait("key"); key("Enter"); ok(sent[6].code === 13, "Return is 13");
  t.setWait("key"); key("Backspace"); ok(sent[7].code === 8, "Backspace is 8");
  t.setWait("key"); key("Q"); ok(sent[8].code === 81 && sent[8].type === "key", "a letter is its code");
  t.setWait("key"); key("Shift");  ok(t.wait === "key", "a modifier key alone does not answer read_char");
}

fs.rmSync(dir, { recursive: true, force: true });
console.log(`${checks - failed}/${checks} checks passed`);
process.exit(failed ? 1 : 0);
