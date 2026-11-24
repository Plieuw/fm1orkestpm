/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * HAWK/Felucca audio and event bridge for the FM1 Orchestral Engine.
 *
 * HAWK renders CTL=32 frames at a time in Q15. The orchestral engine renders
 * any positive frame count in interleaved signed Q23. This adapter converts
 * formats without allocation and can also handle a larger block by slicing
 * it into bounded 32-frame chunks.
 *
 * Include this file only in the native HAWK/Felucca target build, after the
 * orchestral engine sources are linked. The FM1 Core app adapter is separate.
 */
#include <stdint.h>
#include "orch_engine.h"
#include "orch_fx.h"
#include "orch_hawk_bridge.h"

#define ORCH_HAWK_CHUNK_FRAMES 32U
static int32_t q23_scratch[ORCH_HAWK_CHUNK_FRAMES * 2U];

static int32_t q23_to_q15(int32_t sample)
{
    int32_t value = sample >> 8;
    if (value > 32767) value = 32767;
    if (value < -32767) value = -32767;
    return value;
}

void orch_hawk_init(void)
{
    orch_init();
    orch_fx_init();
}

/* HAWK synth_render() replacement: output is interleaved L/R signed Q15. */
void orch_hawk_render_q15(int32_t *output, uint32_t frames)
{
    uint32_t offset = 0U;
    if (!output) return;

    while (offset < frames) {
        uint32_t count = frames - offset;
        if (count > ORCH_HAWK_CHUNK_FRAMES)
            count = ORCH_HAWK_CHUNK_FRAMES;

        orch_render_q23(q23_scratch, count);
        orch_fx_process(q23_scratch, count);
        for (uint32_t i = 0U; i < count * 2U; ++i)
            output[offset * 2U + i] = q23_to_q15(q23_scratch[i]);
        offset += count;
    }
}

/* Call from HAWK's already-routed MIDI/key event path, preserving channel. */
void orch_hawk_note_event(uint8_t channel, uint8_t note, uint8_t velocity)
{
    const uint8_t message[3] = {
        (uint8_t)((velocity == 0U ? 0x80U : 0x90U) | (channel & 15U)),
        (uint8_t)(note & 127U),
        (uint8_t)(velocity & 127U)
    };
    orch_midi_short_message(message, 3U);
}

void orch_hawk_control_change(uint8_t channel, uint8_t controller, uint8_t value)
{
    const uint8_t message[3] = {
        (uint8_t)(0xB0U | (channel & 15U)),
        (uint8_t)(controller & 127U),
        (uint8_t)(value & 127U)
    };
    orch_midi_short_message(message, 3U);
}

void orch_hawk_midi_realtime(uint8_t status)
{
    orch_midi_realtime((unsigned)status);
}

void orch_hawk_panic(void)
{
    orch_panic();
    orch_fx_reset();
}
