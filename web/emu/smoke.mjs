// SPDX-License-Identifier: GPL-3.0-or-later
// Headless check of build/emu/orch.wasm: the engine and the panel run, the LCD
// gets pixels, a note makes sound and SAVE writes the record. No browser.
//
//   FM1_CORE=../fm1-core sh web/emu/build.sh && node web/emu/smoke.mjs
import { readFileSync } from "node:fs";

const file = process.argv[2] || "build/emu/orch.wasm";
const { instance } = await WebAssembly.instantiate(readFileSync(file), { env: {} });
const e = instance.exports;
const u32 = (ptr, i) => new DataView(e.memory.buffer).getUint32(ptr + 4 * i, true);
const i32 = (ptr, i) => new DataView(e.memory.buffer).getInt32(ptr + 4 * i, true);

if (!e.memory) {
  console.error("fail: the module exports no memory");
  process.exit(1);
}

let ms = 0;
const tick = (steps = 1) => {
  for (let i = 0; i < steps; i++) {
    ms += 16;
    e.emu_tick(ms);
    e.emu_render();
  }
};

const peak = () => {
  const p = e.emu_audio();
  let m = 0;
  for (let i = 0; i < 2 * e.emu_frames(); i++) {
    const v = Math.abs(i32(p, i));
    if (v > m) m = v;
  }
  return m;
};

e.emu_init();
tick(6);

// 1. the LCD is drawn
const fb = e.emu_fb();
let lit = 0;
for (let i = 0; i < 240 * 240; i++) if (u32(fb, i) !== 0) lit++;
const litOk = lit > 2000;

// 2. a note sounds
const before = peak();
e.emu_note(0, 60, 100);
tick(4);
const after = peak();
const soundOk = after > 1000 && after > before;

// 3. SAVE writes a record with the magic 0x3035524F ("OR50", the 0.5.0 record)
e.emu_button(1 << 9);            // SAVE
tick(2);
e.emu_button(0);
tick(1);
const magic = u32(e.emu_flash(), 0);
const saveOk = magic === 0x3035524f;

console.log(`lit pixels      ${lit} ${litOk ? "ok" : "FAIL"}`);
console.log(`peak audio      ${after} (before ${before}) ${soundOk ? "ok" : "FAIL"}`);
console.log(`flash magic     0x${magic.toString(16)} ${saveOk ? "ok" : "FAIL"}`);

process.exit(litOk && soundOk && saveOk ? 0 : 1);
