// SPDX-License-Identifier: GPL-3.0-or-later
// The wiring of web/emu/index.html: the engine loop, the seven encoders, the
// fourteen buttons, the keyboard and the four orchestra desks. The panel itself
// is drawn by app/orch_ui.c inside build/emu/orch.wasm, and the sound is the
// engine's; this file only turns hands into emu_* calls.

const BUTTONS = ["OCT-", "OCT+", "FX", "SCL", "ENV", "LFO", "EDIT",
                 "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"];
const ENCODERS = [
  { k: 0, label: "SELECT" }, { k: 1, label: "ALGORITHM" }, { k: 2, label: "PRESETS" },
  { k: 3, label: "KNOB 1" }, { k: 4, label: "KNOB 2" },
  { k: 5, label: "KNOB 3" }, { k: 6, label: "KNOB 4" },
];
const DESKS = [
  { part: 0, name: "P1 Strings" }, { part: 1, name: "P2 Woodwinds" },
  { part: 2, name: "P3 Brass" }, { part: 3, name: "P4 Mallets" },
];

/* the board's keys: 27 of them; the page lays out the first 18, F3 upward */
const FIRST_NOTE = 53;                       // F3
const KEY_LETTERS = ["a", "w", "s", "e", "d", "f", "t", "g", "y",
                     "h", "u", "j", "k", "o", "l", "p", ";", "'"];
const NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
const isWhite = (midi) => [0, 2, 4, 5, 7, 9, 11].includes(midi % 12);

let e;                       // the wasm exports
let audio = null, wport = null;
let ms = 0, buttons = 0, pot = 1023, keys = 0;

const lcd = document.getElementById("lcd");
const ctx2d = lcd.getContext("2d");
const img = ctx2d.createImageData(240, 240);
const msg = document.getElementById("msg");
const vol = document.getElementById("vol");
const deskEls = [];

const say = (t) => { msg.textContent = t; };

/* --------------------------------------------------------------- start --- */
async function start() {
  const res = await fetch("../../build/emu/orch.wasm");
  if (!res.ok) throw new Error(`orch.wasm: HTTP ${res.status} (run web/emu/build.sh)`);
  const { instance } = await WebAssembly.instantiate(await res.arrayBuffer(), { env: {} });
  e = instance.exports;
  e.emu_init();
  e.emu_knob(pot);
  buildEncoders();
  buildButtons();
  buildKeyboard();
  buildDesks();
  draw();
  frame();
  say("engine running · press a key");
}

/* -------------------------------------------------------------- drawing --- */
function draw() {
  const fb = e.emu_fb();
  const dv = new DataView(e.memory.buffer);
  const d = img.data;
  for (let i = 0; i < 240 * 240; i++) {
    const c = dv.getUint16(fb + 2 * i, true);
    d[4 * i] = ((c >> 11) & 31) * 8.23;
    d[4 * i + 1] = ((c >> 5) & 63) * 4.04;
    d[4 * i + 2] = (c & 31) * 8.23;
    d[4 * i + 3] = 255;
  }
  ctx2d.putImageData(img, 0, 0);
  for (const desk of deskEls) readDesk(desk);
}

function readDesk(d) {
  const n = e.emu_voices(d.part);
  d.bar.style.width = Math.min(n, 4) * 25 + "%";
  d.n.textContent = n ? `${n} voice${n === 1 ? "" : "s"}` : "resting";
  d.el.classList.toggle("playing", n > 0);
}

/* --------------------------------------------------------------- engine --- */
/* The control task, the engine and the screen at about 60 Hz. The engine runs
 * whether or not there is an audio device, so the panel and the desks stay
 * live; with audio on, the same blocks go to the worklet. Six blocks of 128
 * frames is 768 frames a frame at 60 Hz, about the 800 a 48 kHz device plays. */
const BLOCKS_PER_FRAME = 6;
let lastBuffer = null;

function frame() {
  ms += 16;
  e.emu_tick(ms);
  const frames = e.emu_frames();
  for (let b = 0; b < BLOCKS_PER_FRAME; b++) {
    e.emu_render();
    if (audio && wport) wport.postMessage(blockToFloat(frames), [lastBuffer]);
  }
  draw();
  requestAnimationFrame(frame);
}

function blockToFloat(frames) {
  const p = e.emu_audio();
  const dv = new DataView(e.memory.buffer);
  const f = new Float32Array(2 * frames);
  for (let s = 0; s < 2 * frames; s++) f[s] = dv.getInt32(p + 4 * s, true) / 8388608;
  lastBuffer = f.buffer;
  return f;
}

/* ---------------------------------------------------------------- audio --- */
async function audioOn() {
  if (audio) return;
  try {
    audio = new AudioContext({ sampleRate: 48000 });
    await audio.audioWorklet.addModule("orch-worklet.js");
    const node = new AudioWorkletNode(audio, "orch-out", { outputChannelCount: [2] });
    wport = node.port;
    node.connect(audio.destination);
    await audio.resume();
    say(`audio on · ${audio.sampleRate} Hz · ${audio.state}`);
  } catch (err) {
    audio = null; wport = null;
    say(`audio failed: ${err && err.message ? err.message : err}`);
  }
}

document.getElementById("go").onclick = async () => {
  await audioOn();
  document.getElementById("start").classList.add("gone");
};

/* --------------------------------------------------------------- notes --- */
function noteOn(midi) {
  keys |= 1 << (midi - FIRST_NOTE);
  e.emu_key(keys);
  e.emu_note(0, midi, 100);
}
function noteOff(midi) {
  keys &= ~(1 << (midi - FIRST_NOTE));
  e.emu_key(keys);
  e.emu_note(0, midi, 0);
}

const letterFor = (midi) => KEY_LETTERS[midi - FIRST_NOTE];
const midiForLetter = {};
KEY_LETTERS.forEach((l, i) => { midiForLetter[l] = FIRST_NOTE + i; });

addEventListener("keydown", (ev) => {
  if (ev.repeat || ev.metaKey || ev.ctrlKey) return;
  const midi = midiForLetter[ev.key];
  if (midi === undefined) return;
  noteOn(midi);
  paintKey(midi, true);
  ev.preventDefault();
});
addEventListener("keyup", (ev) => {
  const midi = midiForLetter[ev.key];
  if (midi === undefined) return;
  noteOff(midi);
  paintKey(midi, false);
  ev.preventDefault();
});

/* ------------------------------------------------------------- keyboard --- */
const keyEls = new Map();

function paintKey(midi, down) {
  const el = keyEls.get(midi);
  if (el) el.classList.toggle("down", down);
}

function buildKeyboard() {
  const kb = document.getElementById("kb");
  const notes = [];
  for (let i = 0; i < KEY_LETTERS.length; i++) notes.push(FIRST_NOTE + i);
  const whites = notes.filter(isWhite);
  const w = 100 / whites.length;

  for (const midi of notes) {
    if (!isWhite(midi)) continue;
    const idx = whites.indexOf(midi);
    const el = document.createElement("div");
    el.className = "w";
    el.style.left = idx * w + "%";
    el.style.width = w + "%";
    el.innerHTML = `<em>${NOTE_NAMES[midi % 12]}<u>${letterFor(midi).toUpperCase()}</u></em>`;
    el.addEventListener("pointerdown", (ev) => {
      el.setPointerCapture(ev.pointerId); noteOn(midi); el.classList.add("down");
    });
    const up = () => { noteOff(midi); el.classList.remove("down"); };
    el.addEventListener("pointerup", up);
    el.addEventListener("pointercancel", up);
    kb.append(el);
    keyEls.set(midi, el);
  }

  for (const midi of notes) {
    if (isWhite(midi)) continue;
    const before = whites.filter((m) => m < midi).length;   // whites to its left
    const el = document.createElement("div");
    el.className = "k";
    el.style.left = `calc(${before * w}% - ${w * 0.31}%)`;
    el.style.width = w * 0.62 + "%";
    el.addEventListener("pointerdown", (ev) => {
      el.setPointerCapture(ev.pointerId); noteOn(midi); el.classList.add("down");
    });
    const up = () => { noteOff(midi); el.classList.remove("down"); };
    el.addEventListener("pointerup", up);
    el.addEventListener("pointercancel", up);
    kb.append(el);
    keyEls.set(midi, el);
  }
}

/* ------------------------------------------------------------- encoders --- */
function buildEncoders() {
  const rows = [document.getElementById("enc3"), document.getElementById("enc4")];
  ENCODERS.forEach((cfg, i) => {
    const box = document.createElement("div");
    box.className = "enc";
    const knob = document.createElement("div");
    knob.className = "knob";
    knob.tabIndex = 0;
    knob.setAttribute("role", "slider");
    knob.setAttribute("aria-label", cfg.label);
    const ring = document.createElement("div");
    ring.className = "r";
    knob.append(ring);
    const arrows = document.createElement("div");
    arrows.className = "arr";
    const down = document.createElement("button");
    down.textContent = "‹";
    const up = document.createElement("button");
    up.textContent = "›";
    arrows.append(down, up);
    const label = document.createElement("span");
    label.textContent = cfg.label;
    box.append(knob, label, arrows);
    rows[i < 3 ? 0 : 1].append(box);

    let angle = 0;
    const turn = (detents) => {
      if (!detents) return;
      angle = Math.max(-150, Math.min(150, angle + detents * 13));
      ring.style.setProperty("--a", angle + "deg");
      e.emu_turn(cfg.k, detents);
    };
    down.onclick = () => turn(-1);
    up.onclick = () => turn(1);
    knob.addEventListener("wheel", (ev) => { turn(ev.deltaY < 0 ? 1 : -1); ev.preventDefault(); }, { passive: false });
    knob.addEventListener("keydown", (ev) => {
      if (ev.key === "ArrowUp" || ev.key === "ArrowRight") { turn(1); ev.preventDefault(); }
      if (ev.key === "ArrowDown" || ev.key === "ArrowLeft") { turn(-1); ev.preventDefault(); }
    });

    let drag = null;
    knob.addEventListener("pointerdown", (ev) => {
      knob.setPointerCapture(ev.pointerId);
      drag = { y: ev.clientY, sent: 0 };
      knob.focus();
    });
    knob.addEventListener("pointermove", (ev) => {
      if (!drag) return;
      const steps = Math.round((drag.y - ev.clientY) / 12);
      if (steps !== drag.sent) { turn(steps - drag.sent); drag.sent = steps; }
    });
    const end = () => { drag = null; };
    knob.addEventListener("pointerup", end);
    knob.addEventListener("pointercancel", end);
  });
}

/* -------------------------------------------------------------- buttons --- */
function buildButtons() {
  const box = document.getElementById("btns");
  BUTTONS.forEach((label, i) => {
    const b = document.createElement("button");
    b.className = "b";
    b.innerHTML = `<i></i><b>${label}</b>`;
    const press = (on) => {
      buttons = on ? buttons | (1 << i) : buttons & ~(1 << i);
      e.emu_button(buttons);
      b.classList.toggle("down", on);
      b.classList.toggle("on", on);
    };
    b.addEventListener("pointerdown", (ev) => { b.setPointerCapture(ev.pointerId); press(true); });
    b.addEventListener("pointerup", () => press(false));
    b.addEventListener("pointercancel", () => press(false));
    b.addEventListener("keydown", (ev) => {
      if (ev.key === " " || ev.key === "Enter") { press(true); ev.preventDefault(); }
    });
    b.addEventListener("keyup", () => press(false));
    box.append(b);
  });
}

/* --------------------------------------------------------------- desks --- */
function buildDesks() {
  const box = document.getElementById("desks");
  for (const cfg of DESKS) {
    const el = document.createElement("div");
    el.className = "desk";
    el.innerHTML = `<div class="name"></div><div class="meter"><i></i></div><div class="n">resting</div>`;
    el.querySelector(".name").textContent = cfg.name;
    box.append(el);
    deskEls.push({ part: cfg.part, el, bar: el.querySelector(".meter i"), n: el.querySelector(".n") });
  }
}

/* ------------------------------------------------------------------ pot --- */
vol.addEventListener("input", () => {
  pot = Number(vol.value);
  e.emu_knob(pot);
});

document.getElementById("panic").onclick = () => {
  keys = 0;
  e.emu_key(0);
  for (const midi of keyEls.keys()) paintKey(midi, false);
  e.emu_note(0, 0, 0);
};

start().catch((err) => say(`failed: ${err.message}`));
