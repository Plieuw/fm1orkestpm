/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "fm1.h"
#include "fm1_app.h"
#include "orch_engine.h"
#include "orch_fx.h"
#include "orch_level.h"
#include "orch_ui.h"

static int engine_started;

static void start_engine(void)
{
    if (engine_started) return;
    orch_init();
    orch_fx_init();
    engine_started = 1;
}

void groove_screen_init(void) {
    start_engine();
    orch_ui_init();
}
void groove_screen_control(void) {
    orch_ui_control();
}
void groove_midi_init(void) { start_engine(); }
void groove_midi_poll(void) {}
void groove_midi_panic(void) { orch_reset_notes(); orch_fx_reset(); }
unsigned groove_midi_mode(unsigned track) {
    (void)track;
    return 0U;
}

/* Panel keys and MIDI both pass the octave transpose and the scale here, so a
 * transposed or quantised note is the same note wherever it came from. */
void groove_midi_note(unsigned track,unsigned note,unsigned velocity) {
    const int mapped = orch_ui_map_note((int)note);
    if (mapped < 0) return;
    orch_note_event(track&15U,(unsigned)mapped,velocity);
}
void groove_midi_track_reset(unsigned track) { (void)track; }
/* MIDI clock and transport drive the ARP/phrase engine (orch_performance). */
void groove_midi_transport(unsigned owner,uint8_t status) { (void)owner; orch_midi_realtime(status); }
void groove_midi_clock(unsigned owner,unsigned tempo) { (void)owner; orch_midi_clock_tempo(tempo); }
void groove_midi_clock_reset(void) { orch_midi_realtime(0xFCU); }

void fm1_app_midi_received(unsigned source,const uint8_t *message,size_t bytes) {
    (void)source;
    orch_midi_short_message(message,(unsigned)bytes);
}

/* The SDK mixer is bypassed: this renderer owns the master volume, so the
 * panel knob works without a second gain stage. Unity is never applied, which
 * keeps the 64-bit multiply off the common path. */
FM1_AUDIO_CODE int fm1_app_render(int32_t *output,unsigned frames) {
    if (!output || frames!=FM1_BLOCK_FRAMES) return -1;
    orch_render_q23(output,frames);
    orch_fx_process(output,frames);
    /* MASTER moves along a line across the block from where it was to where
     * the knob is now, so a fast turn is a fade and not a step a block */
    static int32_t gain_now = (int32_t)(1UL << 23);
    const int32_t gain_to = (int32_t)orch_ui_master_q23;
    if (gain_to != gain_now || gain_now != (int32_t)(1UL << 23)) {
        const int32_t step = (gain_to - gain_now) / (int32_t)frames;
        int32_t gain = gain_now;
        for (unsigned f = 0; f < frames; ++f) {
            gain = f + 1U == frames ? gain_to : gain + step;
            for (unsigned c = 0; c < 2U; ++c) {
                int64_t v = ((int64_t)output[f * 2U + c] * (int64_t)gain) >> 23;
                if (v > 8388607) v = 8388607;      /* over full scale: saturate */
                if (v < -8388608) v = -8388608;
                output[f * 2U + c] = (int32_t)v;
            }
        }
        gain_now = gain_to;
    }
    /* what leaves the instrument, for the light the panel draws over it */
    orch_level_take(output, 2U * frames);
    return 0;
}
