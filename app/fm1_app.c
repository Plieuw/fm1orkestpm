/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "fm1.h"
#include "fm1_app.h"
#include "orch_engine.h"

void groove_screen_init(void) {
    fm1_controls_init();
    fm1_screen_init();
}
void groove_screen_control(void) {
    /* Panel UI follows after the audio engine is validated. */
}
void groove_midi_init(void) { orch_init(); }
void groove_midi_poll(void) {}
void groove_midi_panic(void) { orch_panic(); }
unsigned groove_midi_mode(unsigned track) {
    (void)track;
    return 0U;
}
void groove_midi_note(unsigned track,unsigned note,unsigned velocity) {
    orch_note_event(track&15U,note,velocity);
}
void groove_midi_track_reset(unsigned track) { (void)track; }

void fm1_app_midi_received(unsigned source,const uint8_t *message,size_t bytes) {
    (void)source;
    orch_midi_short_message(message,(unsigned)bytes);
}

FM1_AUDIO_CODE int fm1_app_render(int32_t *output,unsigned frames) {
    if (!output || frames!=FM1_BLOCK_FRAMES) return -1;
    orch_render_q23(output,frames);
    return 0;
}
