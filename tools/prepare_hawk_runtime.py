#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare a pinned HAWK CHUA/Felucca checkout for the FM1 Orchestral Engine.

This is a source overlay, not a firmware builder. Run from the orchestral repo:
  python3 tools/prepare_hawk_runtime.py /path/to/hawk-chua

The HAWK checkout must be clean and at the reviewed stable commit. The script
fails closed if expected upstream anchors have changed.
"""
from pathlib import Path
import shutil
import subprocess
import sys

EXPECTED = "e750df6a2e13490a4f01b84d3f9c553f4612163f"
FILES = (
    "orch_adaptive.c", "orch_adaptive.h",
    "orch_divisi.c", "orch_divisi.h",
    "orch_engine.c", "orch_engine.h",
    "orch_part.c", "orch_part.h",
    "orch_performance.c", "orch_performance.h",
    "orch_fx.c", "orch_fx.h",
)

def replace_once(path: Path, old: str, new: str) -> None:
    data = path.read_text()
    count = data.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one patch anchor, found {count}: {old[:70]!r}")
    path.write_text(data.replace(old, new, 1))

def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: prepare_hawk_runtime.py /path/to/hawk-chua")
    root = Path(sys.argv[1]).resolve()
    fw = root / "firmware" / "src"
    if not (fw / "felucca.c").is_file():
        raise SystemExit("not a HAWK/Felucca source tree: firmware/src/felucca.c missing")
    if not (root / ".git").exists():
        raise SystemExit("expected a Git checkout; refusing an unverified source tree")
    actual = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "HEAD"], text=True
    ).strip()
    if actual != EXPECTED:
        raise SystemExit(f"wrong HAWK revision: {actual}; expected {EXPECTED}")
    dirty = subprocess.check_output(
        ["git", "-C", str(root), "status", "--porcelain"], text=True
    ).strip()
    if dirty:
        raise SystemExit("HAWK checkout is not clean; use a disposable checkout")

    # Guard destructive SPI-NOR operations for both production FM-1 flash IDs.
    # The browser emulator has no physical JEDEC device and is explicitly exempt.
    flash_hal = root / "firmware" / "hal" / "fm1_flash.h"
    replace_once(flash_hal,
        "    uint32_t f = irq_save();\n    int rc = FL_FAR(fl_erase4k_ram)(off, took_us);\n",
        "    if (!fl_emulated() && !fl_id_ok(FL_FAR(fl_jedec_ram)())) return -5; /* refuse unsupported physical flash */\n"
        "    uint32_t f = irq_save();\n    int rc = FL_FAR(fl_erase4k_ram)(off, took_us);\n")
    replace_once(flash_hal,
        "    uint32_t took;\n    while (n) {\n",
        "    uint32_t took;\n"
        "    if (!fl_emulated() && !fl_id_ok(FL_FAR(fl_jedec_ram)())) return -5; /* refuse unsupported physical flash */\n"
        "    while (n) {\n")

    app = Path(__file__).resolve().parents[1] / "app"
    dst = fw / "orchestra"
    dst.mkdir(exist_ok=True)
    for name in FILES:
        src = app / name
        if not src.is_file():
            raise SystemExit(f"missing orchestral source: {src}")
        shutil.copy2(src, dst / name)

    (dst / "fm1.h").write_text(
        "/* SPDX-License-Identifier: GPL-3.0-or-later */\n"
        "#ifndef ORCH_HAWK_FM1_COMPAT_H\n"
        "#define ORCH_HAWK_FM1_COMPAT_H\n"
        "/* The engine only uses this placement macro in its HAWK port. */\n"
        "#define FM1_AUDIO_CODE\n"
        "#define FM1_AUDIO_DATA __attribute__((section(\".pool\")))\n"
        "#endif\n"
    )
    shutil.copy2(Path(__file__).resolve().parents[1] /
                 "runtime" / "hawk" / "orch_hawk_bridge.c", dst / "orch_hawk_bridge.c")
    shutil.copy2(Path(__file__).resolve().parents[1] /
                 "runtime" / "hawk" / "orch_hawk_bridge.h", dst / "orch_hawk_bridge.h")

    felucca = fw / "felucca.c"
    replace_once(felucca,
        '#include "synth.c"\n#include "keys.c"\n#include "audio.c"\n',
        '#include "synth.c"\n'
        '#include "orchestra/orch_adaptive.c"\n'
        '#include "orchestra/orch_divisi.c"\n'
        '#include "orchestra/orch_part.c"\n'
        '/* Felucca is an amalgamated translation unit. Isolate private names. */\n'
        '#define parts orch_perf_parts\n'
        '#include "orchestra/orch_performance.c"\n'
        '#undef parts\n'
        '#define voice_t orch_voice_t\n'
        '#define rng orch_rng_state\n'
        '#include "orchestra/orch_engine.c"\n'
        '#undef rng\n'
        '#undef voice_t\n'
        '#include "orchestra/orch_fx.c"\n'
        '#include "orchestra/orch_hawk_bridge.c"\n'
        '#include "keys.c"\n#include "audio.c"\n')

    audio = fw / "audio.c"
    replace_once(audio, "    synth_render(out, n);\n",
                        "    orch_hawk_render_q15(out, n);\n")

    main_c = fw / "main.c"
    replace_once(main_c, "    keys_init();\n", "    keys_init();\n    orch_hawk_init();\n")

    keys = fw / "keys.c"
    replace_once(keys, "#define KB_SILENT 0xFFu\n",
                        '#include "orchestra/orch_hawk_bridge.h"\n'
                        "#define KB_SILENT 0xFFu\n")
    replace_once(keys, "    synth_all_off();\n    arp_reset();\n    karma_reset();\n",
                        "    synth_all_off();\n    orch_hawk_reset_notes();\n"
                        "    arp_reset();\n    karma_reset();\n")
    replace_once(keys,
        "            kb_note[k] = (uint8_t)n;\n"
        "            note_start((uint32_t)n, KEY_VEL);\n"
        "            kb_ch[k] = (uint8_t)(sys.midi_tx & 15u);\n",
        "            kb_note[k] = (uint8_t)n;\n"
        "            kb_ch[k] = (uint8_t)(sys.midi_tx & 15u);\n"
        "            orch_hawk_note_event(kb_ch[k], (uint8_t)n, KEY_VEL);\n"
        "            note_start((uint32_t)n, KEY_VEL);\n")
    replace_once(keys,
        "            note_end(kb_note[k]);\n"
        "            midi_out_event(0x08u | (0x80u | kb_ch[k]) << 8 | (uint32_t)kb_note[k] << 16);\n",
        "            note_end(kb_note[k]);\n"
        "            orch_hawk_note_event(kb_ch[k], kb_note[k], 0u);\n"
        "            midi_out_event(0x08u | (0x80u | kb_ch[k]) << 8 | (uint32_t)kb_note[k] << 16);\n")

    replace_once(keys,
        "            if (st == 0x90u && d2)\n"
        "                synth_note_on_b(d1, d2);\n"
        "            else if (st == 0x80u || st == 0x90u)\n"
        "                synth_note_off_b(d1);\n",
        "            if (st == 0x90u && d2)\n"
        "                orch_hawk_note_event((uint8_t)((pkt >> 8) & 15u), (uint8_t)d1, (uint8_t)d2);\n"
        "            else if (st == 0x80u || st == 0x90u)\n"
        "                orch_hawk_note_event((uint8_t)((pkt >> 8) & 15u), (uint8_t)d1, 0u);\n")
    replace_once(keys,
        "            else if (st == 0xB0u && (d1 == 123u || d1 == 120u))\n"
        "                for (uint32_t k2 = 0; k2 < 128u; k2++)\n"
        "                    synth_note_off_b(k2);\n",
        "            else if (st == 0xB0u && d1 == 64u)\n"
        "                orch_hawk_control_change((uint8_t)((pkt >> 8) & 15u), (uint8_t)d1, (uint8_t)d2);\n"
        "            else if (st == 0xB0u && (d1 == 123u || d1 == 120u)) {\n"
        "                orch_hawk_control_change((uint8_t)((pkt >> 8) & 15u), (uint8_t)d1, (uint8_t)d2);\n"
        "                for (uint32_t k2 = 0; k2 < 128u; k2++) synth_note_off_b(k2);\n"
        "            }\n")
    replace_once(keys,
        "        if (st == 0x90u && d2)\n"
        "            note_start(d1, d2);\n"
        "        else if (st == 0x80u || st == 0x90u)     /* (note on with velocity 0 is a note off) */\n"
        "            note_end(d1);\n"
        "        else if (st == 0xB0u)\n"
        "            midi_cc(d1, d2);\n",
        "        if (st == 0x90u && d2)\n"
        "            orch_hawk_note_event((uint8_t)((pkt >> 8) & 15u), (uint8_t)d1, (uint8_t)d2);\n"
        "        else if (st == 0x80u || st == 0x90u)\n"
        "            orch_hawk_note_event((uint8_t)((pkt >> 8) & 15u), (uint8_t)d1, 0u);\n"
        "        else if (st == 0xB0u) {\n"
        "            if (d1 == 64u || d1 == 120u || d1 == 123u)\n"
        "                orch_hawk_control_change((uint8_t)((pkt >> 8) & 15u), (uint8_t)d1, (uint8_t)d2);\n"
        "            midi_cc(d1, d2);\n"
        "        }\n")
    replace_once(keys,
        "            arp_midi_rt(rt);\n",
        "            orch_hawk_midi_realtime((uint8_t)rt);\n"
        "            arp_midi_rt(rt);\n")

    print("Prepared HAWK runtime overlay.")
    print("Next: run the pinned JieLi PI32v2 build and inspect link/RAM/audio timing.")
    print("This script does not build, package, flash, or claim device validation.")

if __name__ == "__main__":
    main()
