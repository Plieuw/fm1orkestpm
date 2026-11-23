/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "fm1.h"
#include "orch_engine.h"

#define ORCH_AUDIO FM1_AUDIO_CODE

#define DELAY_SIZE 2048U
#define EVENT_CAP 64U
#define ENV_MAX 32767
#define Q23_MAX 8388607
#define Q23_MIN (-8388608)

typedef struct {
    int16_t delay[DELAY_SIZE];
    uint16_t length, index;
    int32_t filter, envelope;
    uint32_t age;
    uint8_t note, channel, velocity, active, gate, releasing;
} voice_t;

typedef struct { uint8_t channel, note, velocity; } event_t;

static voice_t voices[ORCH_MAX_VOICES];
static event_t events[EVENT_CAP];
static volatile uint8_t event_write, event_read;
static uint8_t sustain[ORCH_MIDI_CHANNELS];
static struct orch_step_note pattern[ORCH_SEQ_STEPS][ORCH_STEP_NOTES];
static uint8_t pattern_count[ORCH_SEQ_STEPS];
static uint32_t age_counter, sample_counter, step_start;
static uint16_t tempo = 120U;
static uint8_t seq_enabled, seq_started, current_step;
static enum orch_model selected_model;
static uint32_t rng = 0x6d2b79f5U;

static ORCH_AUDIO int32_t clamp16(int32_t x) {
    if (x > 32767) return 32767;
    if (x < -32768) return -32768;
    return x;
}
static ORCH_AUDIO int32_t noise16(void) {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (int16_t)(rng >> 16);
}
static ORCH_AUDIO uint16_t note_delay(unsigned note) {
    static const uint16_t base[12] = {
        170,160,151,143,135,127,120,113,106,100,95,90
    };
    int octave = (int)(note / 12U) - 5;
    uint32_t n = base[note % 12U];
    if (octave > 0) n >>= (unsigned)octave;
    else if (octave < 0) {
        unsigned shift = (unsigned)(-octave);
        if (shift > 3U) shift = 3U;
        n <<= shift;
    }
    if (n < 8U) n = 8U;
    if (n > DELAY_SIZE) n = DELAY_SIZE;
    return (uint16_t)n;
}
static ORCH_AUDIO void enqueue(unsigned ch, unsigned note, unsigned velocity) {
    uint8_t next = (uint8_t)((event_write + 1U) & (EVENT_CAP - 1U));
    if (next == event_read) return;
    events[event_write].channel = (uint8_t)(ch & 15U);
    events[event_write].note = (uint8_t)(note & 127U);
    events[event_write].velocity = (uint8_t)(velocity & 127U);
    event_write = next;
}
void orch_note_event(unsigned ch, unsigned note, unsigned velocity) {
    if (ch < 16U && note < 128U && velocity < 128U) enqueue(ch,note,velocity);
}
void orch_midi_short_message(const uint8_t *m, unsigned bytes) {
    if (!m || bytes < 2U || m[0] < 0x80U || m[0] >= 0xf0U) return;
    unsigned kind = m[0] & 0xf0U, ch = m[0] & 15U;
    if ((kind == 0x80U || kind == 0x90U) && bytes >= 3U)
        enqueue(ch,m[1],(kind == 0x80U || m[2] == 0U) ? 0U : m[2]);
    else if (kind == 0xb0U && bytes >= 3U && m[1] == 64U) {
        sustain[ch] = m[2] >= 64U;
        if (!sustain[ch]) for (unsigned i=0;i<ORCH_MAX_VOICES;i++)
            if (voices[i].active && voices[i].channel==ch && !voices[i].gate)
                voices[i].releasing=1U;
    }
}
static ORCH_AUDIO void start_voice(unsigned ch, unsigned note, unsigned velocity) {
    unsigned chosen=ORCH_MAX_VOICES, oldest_index=0U;
    int32_t quietest=2147483647;
    uint32_t oldest=0U;
    for (unsigned i=0;i<ORCH_MAX_VOICES;i++) {
        voice_t *v=&voices[i];
        if (!v->active) { chosen=i; break; }
        if (v->releasing && v->envelope<quietest) {
            quietest=v->envelope; chosen=i;
        }
        if (!v->releasing && v->age>=oldest) { oldest=v->age; oldest_index=i; }
    }
    if (chosen==ORCH_MAX_VOICES) chosen=oldest_index;
    voice_t *v=&voices[chosen];
    v->length=note_delay(note); v->index=0U; v->filter=0; v->envelope=0;
    v->age=++age_counter; v->note=(uint8_t)note; v->channel=(uint8_t)ch;
    v->velocity=(uint8_t)velocity; v->active=1U; v->gate=1U; v->releasing=0U;
    for (unsigned n=0;n<v->length;n++)
        v->delay[n]=(int16_t)((noise16()*(int32_t)velocity)/512);
}
static ORCH_AUDIO void release_voice(unsigned ch, unsigned note) {
    for (unsigned i=0;i<ORCH_MAX_VOICES;i++) {
        voice_t *v=&voices[i];
        if (v->active && v->channel==ch && v->note==note) {
            v->gate=0U;
            if (!sustain[ch]) v->releasing=1U;
        }
    }
}
static ORCH_AUDIO void drain_events(void) {
    while (event_read!=event_write) {
        event_t e=events[event_read];
        event_read=(uint8_t)((event_read+1U)&(EVENT_CAP-1U));
        if (e.velocity) start_voice(e.channel,e.note,e.velocity);
        else release_voice(e.channel,e.note);
    }
}
void orch_seq_clear(void) {
    for (unsigned s=0;s<ORCH_SEQ_STEPS;s++) {
        pattern_count[s]=0U;
        for (unsigned n=0;n<ORCH_STEP_NOTES;n++) {
            pattern[s][n].note=60U; pattern[s][n].velocity=0U;
            pattern[s][n].gate_percent=80U;
        }
    }
}
void orch_init(void) {
    for (unsigned i=0;i<ORCH_MAX_VOICES;i++) voices[i].active=0U;
    event_read=event_write=0U;
    for (unsigned c=0;c<16U;c++) sustain[c]=0U;
    orch_seq_clear(); tempo=120U; seq_enabled=seq_started=0U;
    current_step=0U; sample_counter=step_start=0U;
    selected_model=ORCH_MODEL_WAVEGUIDE_STRING;
}
void orch_seq_set_tempo(uint16_t bpm) {
    if (bpm<30U) bpm=30U; if (bpm>300U) bpm=300U; tempo=bpm;
}
void orch_seq_set_enabled(unsigned enabled) {
    seq_enabled=enabled?1U:0U; seq_started=0U;
}
int orch_seq_set_step(unsigned step,unsigned slot,const struct orch_step_note *n) {
    if (step>=16U || slot>=4U || !n || n->note>127U ||
        n->velocity>127U || n->gate_percent>100U) return -1;
    pattern[step][slot]=*n;
    if (n->velocity && pattern_count[step]<=slot) pattern_count[step]=(uint8_t)(slot+1U);
    else if (!n->velocity && pattern_count[step]==slot+1U)
        while (pattern_count[step] && !pattern[step][pattern_count[step]-1U].velocity)
            --pattern_count[step];
    return 0;
}
void orch_set_model(enum orch_model model) {
    if (model<=ORCH_MODEL_PLUCKED_STRING) selected_model=model;
}
unsigned orch_active_voices(void) {
    unsigned n=0; for (unsigned i=0;i<ORCH_MAX_VOICES;i++) n+=voices[i].active?1U:0U;
    return n;
}
static ORCH_AUDIO void seq_tick(void) {
    uint32_t step_samples=(ORCH_SAMPLE_RATE*60U)/((uint32_t)tempo*4U);
    if (!seq_enabled) return;
    if (!seq_started) { seq_started=1U; current_step=15U; step_start=sample_counter-step_samples; }
    if ((uint32_t)(sample_counter-step_start)>=step_samples) {
        step_start+=step_samples; current_step=(uint8_t)((current_step+1U)&15U);
        for (unsigned n=0;n<pattern_count[current_step];n++) {
            struct orch_step_note *p=&pattern[current_step][n];
            if (p->velocity) start_voice(16U,p->note,p->velocity);
        }
    }
}
ORCH_AUDIO void orch_render_q23(int32_t *out,unsigned frames) {
    if (!out) return;
    drain_events();
    for (unsigned f=0;f<frames;f++) {
        seq_tick();
        int64_t sum=0;
        for (unsigned i=0;i<ORCH_MAX_VOICES;i++) {
            voice_t *v=&voices[i]; if (!v->active) continue;\n            ++active;
            int32_t delayed=v->delay[v->index];
            v->filter+=(delayed-v->filter)>>3;
            int32_t loop=v->filter-(v->filter>>15);
            if (v->gate) {
                int32_t excitation=(noise16()*(int32_t)v->velocity)>>9;
                loop += selected_model==ORCH_MODEL_WAVEGUIDE_STRING ?
                    (excitation>>3) : (excitation>>1);
            }
            v->delay[v->index]=(int16_t)clamp16(loop);
            v->index=(uint16_t)((v->index+1U)%v->length);
            if (v->gate && v->envelope<ENV_MAX)
                v->envelope+=(ENV_MAX-v->envelope)>>5;
            else if (v->releasing) {
                v->envelope-=(v->envelope>>10)+1;
                if (v->envelope<=8) { v->active=0U; v->envelope=0; }
            }
            sum+=(v->filter*v->envelope>>15)*v->velocity>>7;
        }
        int64_t scaled=active ? (sum*256)/(int64_t)active : 0;
        if (scaled>Q23_MAX) scaled=Q23_MAX;
        if (scaled<Q23_MIN) scaled=Q23_MIN;
        out[f*2U]=(int32_t)scaled; out[f*2U+1U]=(int32_t)scaled;
        ++sample_counter;
    }
}
