/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "orch_stage.h"

#include "orch_part.h"

/* The orchestra as the audience hears it: strings left to right, woodwinds in
 * the middle, brass right of them, percussion at the back right, choir at the
 * back centre. These are places of a section, not of one player. */
static const uint8_t SEAT[ORCH_INST_COUNT] = {
    40U,   /* violin      */
    100U,  /* viola       */
    180U,  /* cello       */
    216U,  /* double bass */
    56U,   /* harp        */
    118U,  /* flute       */
    126U,  /* piccolo     */
    92U,   /* clarinet    */
    142U,  /* oboe        */
    158U,  /* bassoon     */
    196U,  /* trumpet     */
    168U,  /* french horn */
    208U,  /* trombone    */
    232U,  /* tuba        */
    176U,  /* marimba     */
    196U,  /* xylophone   */
    240U,  /* timpani     */
    128U,  /* choir       */
    146U,  /* english horn, beside the oboe */
    98U,   /* bass clarinet, beside the clarinet */
    164U,  /* contrabassoon, beside the bassoon */
    112U,  /* alto flute, beside the flute */
    220U,  /* bass trombone */
    186U,  /* glockenspiel */
    170U,  /* vibraphone */
    228U,  /* tubular bells */
    72U,   /* celesta, by the harp */
    64U,   /* harpsichord, the continuo by the harp */
    116U,  /* women's choir, left of centre */
    140U   /* men's choir, right of centre */
};

static int listener_x;
static unsigned listener_depth;

void orch_stage_reset(void)
{
    listener_x = 0;
    listener_depth = 0U;
}

void orch_stage_set_listener(int x, int depth)
{
    if (x < -100) x = -100;
    if (x > 100) x = 100;
    if (depth < 0) depth = 0;
    if (depth > 100) depth = 100;
    listener_x = x;
    listener_depth = (unsigned)depth;
}

int orch_stage_listener_x(void) { return listener_x; }
unsigned orch_stage_listener_depth(void) { return listener_depth; }

unsigned orch_stage_seat(unsigned instrument)
{
    return instrument < ORCH_INST_COUNT ? SEAT[instrument] : 128U;
}

unsigned orch_stage_place(unsigned instrument, uint8_t section_pan)
{
    /* moving back keeps 40 % of the width, so a distant orchestra closes in
     * towards the middle without moving its sections off their axes */
    const int width = 100 - (int)(listener_depth * 60U / 100U);
    const int rel = ((int)section_pan - 128) * width / 100;
    int pan = (int)orch_stage_seat(instrument) + rel + listener_x * 64 / 100;
    if (pan < 0) pan = 0;
    if (pan > 255) pan = 255;
    return (unsigned)pan;
}
