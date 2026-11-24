# FM1 boot and recovery protection

## Decision

The Orchestral Engine does **not** replace or bundle a custom bootloader. Keep the FM1's independently installed bootloader intact and treat the application updater as a separate layer.

For the FM1, the open-source `m-bootloader` project provides the recovery features associated with M-Flasher:
- REC held at power-on enters the chip's USB download mode.
- PLAY held at power-on opens the recovery menu.
- A firmware checksum failure or failed startup returns to recovery instead of blindly launching a damaged app.
- Its MIDI recovery mode can communicate with a host even when the main firmware does not run.

M-Flasher is the host-side backup/restore/flash application; `m-bootloader` is the device-side bootloader. They work together, but they are not the same component.

## Non-negotiable update rules

1. **Never write the boot head during an application update.** The first 0x4000 bytes are outside the app update range.
2. **Never bundle an instruction that flashes the bootloader as part of the Orchestral firmware install.** Bootloader installation/replacement is a separate, explicitly reviewed maintenance operation.
3. **Keep the existing device bootloader.** M-Flasher 0.12.0 and later separates firmware flashing from bootloader flashing and preserves the unit's bootloader during a firmware install.
4. **Verify every changed sector.** The pinned HAWK/Felucca loader reads back each changed 4 KiB sector and compares it before proceeding.
5. **Fail closed if the upstream loader changes.** CI runs `tools/check_boot_safety.py` against the pinned runtime and rejects unexpected changes to the protected ranges.
6. **Keep user data untouched.** The update loader excludes the user-data interval [0x97000, 0xE0000).
7. **Do not claim brick-proof.** Power loss, flash-chip incompatibility, hardware faults, or a damaged bootloader can still require external recovery. Recovery reduces risk; it cannot make hardware writes infallible.

## Dual flash-chip compatibility

The FM-1 has two known 1 MiB production flash variants:
- Puya P25Q80: JEDEC ID `0x856014`.
- UC25HQ80: JEDEC ID `0xB36014`.

The Orchestral package remains a single firmware build for both variants. The pinned HAWK flash HAL uses the same documented SPI-NOR commands for these two parts. The build overlay now checks the live JEDEC ID before every physical erase and program operation and permits only these two IDs. Unknown physical IDs fail closed; the browser emulator is exempt because it has no physical JEDEC chip. No per-chip firmware images or bootloader replacement are needed for these two supported variants.

CI checks that both IDs remain in the allow-list and that both destructive operations retain the guard. This is source-level compatibility validation, not a substitute for testing both hardware revisions.

## What the current pinned OTA loader protects

The HAWK/Felucca loader used by the Orchestral build has two explicit protections:
- `ldr_core.c` limits application sectors to `0x4000..0x93000`, and treats the user-data gap `0x97000..0xE0000` separately.
- `loader.c` has a flash-operation allow-list that excludes the boot head and the user-data gap. The loader source states that it writes the app area only and never the head.

The `.fwsc` package contains a package image with an SDK SPL in its header. That does **not** mean the normal OTA loader writes that header to the device: the loader's write range is independently constrained. Do not bypass that range check or change it to include address zero.

The CI guard is deliberately tied to the exact pinned HAWK source revision. If we move to another runtime revision, review its flash map and write path before updating the guard.

## Bootloader version policy

Do not automatically install a bootloader from an Orchestral update. The upstream `m-bootloader` 3.0 release is a Rust rewrite; its README describes testing on an FM-1 and an SMK-37 Pro, but notes that several exact-build paths were not re-tested. Use the version and compatibility guidance shown by the current M-Flasher release rather than silently embedding a bootloader binary in this project.

FM-1 production batches have more than one flash-chip ID. Before any separate bootloader maintenance, identify the chip in M-Flasher and confirm that the exact bootloader release supports it. This project does not guess the chip ID and does not force-install a bootloader.

## Recovery UX for the Orchestral installer

The future web installer should:
- identify the connected FM-1 before writing;
- state clearly that it updates the application only and preserves the installed bootloader;
- validate package identity, size and checksums before transfer;
- stop if the device or package identity is unexpected;
- show transfer and device-confirmed completion as separate states;
- never offer a bootloader-write checkbox in the ordinary firmware-install flow;
- link to the recovery guide if the device fails to restart.

## Validation boundary

The CI guard proves that the pinned source still declares the protected flash ranges. It does not prove that a physical FM-1 can recover from every failure, nor does it validate a bootloader install. Before a release is called recovery-tested, test normal update, interrupted app update, checksum failure, REC download mode and PLAY recovery menu on a physical unit with a known-good backup.
