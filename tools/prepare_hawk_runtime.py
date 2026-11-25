#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare a pinned HAWK CHUA/Felucca checkout for the FM1 Orchestral Engine.

This is a source overlay, not a firmware builder. Since 0.5.0 it replaces
HAWK's synth, keyboard path and screen with the orchestral engine and the
orchestral panel (app/orch_ui.c), and keeps HAWK's boot, watchdog, USB/TRS
MIDI, storage, OTA update entry and UBOOT combination. Run from the orchestral repo:
  python3 tools/prepare_hawk_runtime.py /path/to/hawk-chua

The HAWK checkout must be clean and at the reviewed stable commit. The script
fails closed if expected upstream anchors have changed.
"""
from pathlib import Path
import shutil
import subprocess
import sys

EXPECTED = "e750df6a2e13490a4f01b84d3f9c553f4612163f"
# Compiled as their own units (tools/build.py patch below), not amalgamated into
# felucca.c: the panel and the engine have static names of their own (ui, text,
# card, B_FX, ...) that would collide with HAWK's in one translation unit.
RUNTIME = (
    "fm1.h", "fm1_engine_control.h", "orch_hawk_hw.h",
    "orch_hawk_bridge.c", "orch_hawk_bridge.h", "orch_hawk_panel.c",
    "orch_hawk_glue.c",          # the one file that is included into felucca.c
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

    repo = Path(__file__).resolve().parents[1]
    app = repo / "app"
    runtime = repo / "runtime" / "hawk"
    dst = fw / "orchestra"
    dst.mkdir(exist_ok=True)
    sources = sorted(p for p in app.glob("orch_*") if p.suffix in (".c", ".h", ".inc"))
    if not any(p.name == "orch_ui.c" for p in sources):
        raise SystemExit(f"missing orchestral sources in {app}")
    for src in sources:
        shutil.copy2(src, dst / src.name)
    for name in RUNTIME:
        src = runtime / name
        if not src.is_file():
            raise SystemExit(f"missing runtime source: {src}")
        shutil.copy2(src, dst / name)

    # Build: every orchestra/*.c except the glue is its own object, linked
    # beside felucca.o. They take HAWK's own flags (-Os, -fno-builtin; memset
    # and memcpy come from felucca's libc.c at link time).
    build = root / "tools" / "build.py"
    replace_once(build,
        '    (OUT / "fm1_cpu1.o").unlink(missing_ok=True)\n',
        '    orch_src = sorted(p for p in (FW / "src" / "orchestra").glob("*.c") if p.name != "orch_hawk_glue.c")\n'
        '    orch_obj = [OUT / ("orch_" + p.stem + ".o") for p in orch_src]\n'
        '    orch_cc = [("cc", *flags, "-Ifirmware/src/orchestra", "-c", p, "-o", o) for p, o in zip(orch_src, orch_obj)]\n'
        '    (OUT / "fm1_cpu1.o").unlink(missing_ok=True)\n')
    replace_once(build, "           cmain)\n", "           *orch_cc, cmain)\n")
    replace_once(build, 'OUT / "felucca.o", "-o", elf)', 'OUT / "felucca.o", *orch_obj, "-o", elf)')

    felucca = fw / "felucca.c"
    replace_once(felucca,
        '#include "keys.c"\n#include "audio.c"\n',
        '#include "keys.c"\n'
        '/* FM1 Orchestral Engine: the audio block runs the orchestra (orchestra/orch_hawk_glue.c) */\n'
        '#include "orchestra/orch_hawk_bridge.h"\n'
        'static void orch_hawk_keys_block(void);\n'
        '#include "audio.c"\n')
    replace_once(felucca,
        '#include "ui.c"\n',
        '#include "orchestra/orch_hawk_glue.c"   /* the orchestra\'s keys, MIDI, panel services */\n'
        '#include "ui.c"\n')

    audio = fw / "audio.c"
    replace_once(audio, "    keys_block();\n    synth_render(out, n);\n",
                        "    orch_hawk_keys_block();\n"
                        "    orch_hawk_render_q15(out, n, sys.master_q12);\n")

    main_c = fw / "main.c"
    replace_once(main_c, "    keys_init();\n", "    keys_init();\n    orch_hawk_init();\n")
    replace_once(main_c, "    kb_boot_hold = 0;\n    ui_force = 1;\n",
                         "    kb_boot_hold = 0;\n    ui_force = 1;\n    orch_hawk_panel_start();\n")
    replace_once(main_c,
        "        ui_input();\n"
        "        felucca_dbg.stage = 2;\n"
        "        ui_leds();\n"
        "        if (!(fm1_in.buttons & btn_bit(B_OCTDN) && fm1_in.buttons & btn_bit(B_OCTUP)))\n"
        "            ui_draw();                                  /* (the countdown has the screen while both are held) */\n",
        "        orch_hawk_main_frame();                         /* the orchestral panel (orch_hawk_glue.c) */\n"
        "        felucca_dbg.stage = 2;\n")
    replace_once(main_c, 'draw_text_box(0, 72, 240, &AF_M, "HAWK CHUA FIRMWARE", T_TEXT, 1);',
                         'draw_text_box(0, 72, 240, &AF_M, "FM1 ORCHESTRA FIRMWARE", T_TEXT, 1);')

    print("Prepared HAWK runtime overlay: orchestral engine, panel and glue.")
    print("Next: ./build.sh in that checkout (JieLi PI32v2 toolchain + AC79 SDK).")
    print("This script does not build, package, flash, or claim device validation.")

if __name__ == "__main__":
    main()
