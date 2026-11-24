// SPDX-License-Identifier: GPL-3.0-or-later
// The browser's audio output, as HAWK CHUA's web/emu does it: the page renders
// blocks ahead into this queue and the worklet plays them. Float32, interleaved
// stereo, one message per block of FM1_BLOCK_FRAMES.
class OrchOut extends AudioWorkletProcessor {
  constructor() {
    super();
    this.q = [];
    this.off = 0;      // frames into q[0]
    this.played = 0;
    this.n = 0;
    this.port.onmessage = (e) => {
      if (e.data === "stop") {
        this.q.length = 0;
        this.off = 0;
      } else {
        this.q.push(e.data);
      }
    };
  }

  process(_in, outs) {
    const L = outs[0][0];
    const R = outs[0][1] || outs[0][0];
    for (let i = 0; i < L.length; i++) {
      const c = this.q[0];
      if (!c) {
        L[i] = R[i] = 0;
        continue;
      }
      L[i] = c[2 * this.off];
      R[i] = c[2 * this.off + 1];
      if (++this.off * 2 >= c.length) {
        this.q.shift();
        this.off = 0;
      }
      this.played++;
    }
    if (++this.n % 120 === 0) this.port.postMessage(this.played);
    return true;
  }
}

registerProcessor("orch-out", OrchOut);
