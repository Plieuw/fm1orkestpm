/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "fm1.h"
#include "fm1_app.h"
#include "orch_engine.h"
#include "orch_fx.h"
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
void groove_midi_panic(void) { orch_panic(); orch_fx_reset(); }
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
    const uint32_t gain = orch_ui_master_q23;
    if (gain != (1UL << 23)) {
        for (unsigned i = 0; i < 2U * frames; ++i)
            output[i] = (int32_t)(((int64_t)output[i] * (int32_t)gain) >> 23);
    }
    return 0;
}
