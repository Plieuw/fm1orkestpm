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
#include "orch_level.h"
#include "orch_ui.h"

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

/* HAWK synth_render() replacement: output is interleaved L/R signed Q15.
 * master_q12 is HAWK's MASTER pot (0..4088, its square law, main.c). Since
 * 0.6.1 it spans up to +12 dB: the orchestra shares full scale evenly (1/n)
 * and leaves headroom, so the pot's middle (about 1022) is unity and fully
 * open is x4. Over full scale the output saturates; that is the player's
 * choice at the top of the pot, there is no limiter. The panel's level light
 * reads what leaves after it. No 64-bit divide: a multiply and a shift. */
void orch_hawk_render_q15(int32_t *output, uint32_t frames, uint32_t master_q12)
{
    uint32_t offset = 0U;
    if (!output) return;
    if (master_q12 > 4096U) master_q12 = 4096U;
    /* 0 .. x4, Q12. The pot is read once a callback; the gain moves to it
     * along a line across the callback, so a turn (or the pot's own
     * jitter) is a fade, not a step every few milliseconds. */
    static int32_t gain_now = 4096 * 4 / 4;               /* unity until told */
    const int32_t gain_to = (int32_t)master_q12 * 4;
    const int32_t gain_step = frames ? (gain_to - gain_now) / (int32_t)frames : 0;
    uint32_t done = 0U;

    while (offset < frames) {
        uint32_t count = frames - offset;
        if (count > ORCH_HAWK_CHUNK_FRAMES)
            count = ORCH_HAWK_CHUNK_FRAMES;

        orch_render_q23(q23_scratch, count);
        orch_fx_process(q23_scratch, count);
        for (uint32_t i = 0U; i < count * 2U; ++i) {
            if (!(i & 1U)) {
                ++done;
                gain_now = done == frames ? gain_to : gain_now + gain_step;
            }
            /* Q23 x Q12 in 64 bits, then back to Q23: a multiply, no divide */
            int64_t v = ((int64_t)q23_scratch[i] * gain_now) >> 12;
            if (v > 8388607) v = 8388607;
            if (v < -8388608) v = -8388608;
            q23_scratch[i] = (int32_t)v;
            output[offset * 2U + i] = q23_to_q15(q23_scratch[i]);
        }
        orch_level_take(q23_scratch, count * 2U);
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

/* Any MIDI 1.0 channel or realtime message HAWK's rings hand over. */
void orch_hawk_midi_message(uint8_t status, uint8_t d1, uint8_t d2)
{
    if (status >= 0xF8U) {
        orch_midi_realtime((unsigned)status);
        return;
    }
    const uint8_t message[3] = { status, (uint8_t)(d1 & 127U), (uint8_t)(d2 & 127U) };
    const unsigned kind = status & 0xF0U;
    orch_midi_short_message(message, (kind == 0xC0U || kind == 0xD0U) ? 2U : 3U);
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

void orch_hawk_reset_notes(void)
{
    orch_reset_notes();
    orch_fx_reset();
}
