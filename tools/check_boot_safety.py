#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail closed if the pinned FM1 OTA loader stops protecting the boot area.

This checks the upstream HAWK/Felucca loader contract used by our build.
It does not flash or modify any device.
"""
from pathlib import Path
import sys

EXPECTED = {
    "LDR_APP_LO": "#define LDR_APP_LO 0x4000u",
    "LDR_APP_HI": "#define LDR_APP_HI 0x93000u",
    "LDR_DATA_LO": "#define LDR_DATA_LO 0x97000u",
    "LDR_DATA_HI": "#define LDR_DATA_HI 0xE0000u",
    "FL_RANGE_OK": "#define FL_RANGE_OK(off, n) (FL_IN(off, n, 0x4000u, 0x97000u) || FL_IN(off, n, 0xE0000u, 0xFC000u))",
}

def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: check_boot_safety.py /path/to/pinned-hawk-chua")
    root = Path(sys.argv[1]).resolve()
    core = root / "firmware" / "loader" / "ldr_core.c"
    loader = root / "firmware" / "loader" / "loader.c"
    if not core.is_file() or not loader.is_file():
        raise SystemExit("boot-safety check: pinned HAWK loader sources are missing")
    core_text, loader_text = core.read_text(), loader.read_text()
    checks = (
        (core_text, EXPECTED["LDR_APP_LO"], "application lower boundary"),
        (core_text, EXPECTED["LDR_APP_HI"], "application upper boundary"),
        (core_text, EXPECTED["LDR_DATA_LO"], "user-data lower boundary"),
        (core_text, EXPECTED["LDR_DATA_HI"], "user-data upper boundary"),
        (loader_text, EXPECTED["FL_RANGE_OK"], "flash write/erase allow-list"),
    )
    for source, needle, label in checks:
        if source.count(needle) != 1:
            raise SystemExit(f"boot-safety check: {label} changed or is ambiguous; refusing build")
    if "writes the app area only" not in loader_text:
        raise SystemExit("boot-safety check: loader no longer declares app-only writes")
    if "never the head" not in loader_text:
        raise SystemExit("boot-safety check: loader no longer documents boot-head protection")
    print("Boot-safety check passed: application updates exclude flash head [0x0000, 0x4000).")
    print("User-data gap [0x97000, 0xE0000) remains excluded from update writes.")
    print("This validates the pinned OTA source contract, not a physical device.")

if __name__ == "__main__":
    main()
