/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "fm1.h"
#include "orch_engine.h"

#define ORCH_AUDIO FM1_AUDIO_CODE
#define DELAY_SIZE 2048U
#define EVENT_CAP 64U
#define ENV_MAX 32767
#define Q23_MAX 8388607
#define Q23_MIN (-8388608)
#define SEQ_SOURCE 16U

typedef struct {
    int16_t delay[DELAY_SIZE];
    uint16_t length, target_length, index;
    int32_t filter, envelope;
    uint32_t age, age_samples, attack_samples, release_samples, glide_samples;
    uint32_t vib_phase, vib_step;
    uint16_t vib_depth;
    uint8_t note, channel, velocity, active, gate, releasing;
    uint8_t attack_scale, release_scale, player;
    uint16_t tremolo_depth;
} voice_t;

typedef struct { uint8_t channel, note, velocity, type; } event_t;
typedef struct {
    uint32_t last_on;
    uint32_t mean_ioi;
    uint32_t last_off;
    uint8_t last_note, last_velocity, phrase_notes, initialized;
} adaptive_t;

static voice_t voices[ORCH_MAX_VOICES];
static event_t events[EVENT_CAP];
static volatile uint8_t event_write, event_read;
static uint8_t sustain[ORCH_MIDI_CHANNELS];
static adaptive_t adaptive[ORCH_MIDI_CHANNELS];
static struct orch_step_note pattern[ORCH_SEQ_STEPS][ORCH_STEP_NOTES];
static uint8_t pattern_count[ORCH_SEQ_STEPS];
static uint8_t seq_note[ORCH_STEP_NOTES], seq_active[ORCH_STEP_NOTES];
static uint32_t seq_off_at[ORCH_STEP_NOTES];
static uint32_t age_counter, sample_counter, step_start;
static volatile uint16_t tempo = 120U;
static volatile uint8_t seq_enabled, seq_stop_pending;
static uint8_t seq_started, current_step, divisi_enabled;
static enum orch_model selected_model;
static enum orch_family selected_family;
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
    static const uint16_t base[12] = {170,160,151,143,135,127,120,113,106,100,95,90};
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
static ORCH_AUDIO uint32_t clamp_u32(uint32_t x, uint32_t lo, uint32_t hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}
static ORCH_AUDIO void enqueue(unsigned ch, unsigned note, unsigned velocity, unsigned type) {
    uint8_t next = (uint8_t)((event_write + 1U) & (EVENT_CAP - 1U));
    if (next == event_read) return;
    events[event_write].channel = (uint8_t)(ch & 15U);
    events[event_write].note = (uint8_t)(note & 127U);
    events[event_write].velocity = (uint8_t)(velocity & 127U);
    events[event_write].type = (uint8_t)type;
    event_write = next;
}
void orch_note_event(unsigned ch, unsigned note, unsigned velocity) {
    if (ch < 16U && note < 128U && velocity < 128U) enqueue(ch,note,velocity,0U);
}
void orch_panic(void) { enqueue(0U,0U,0U,2U); }
void orch_midi_short_message(const uint8_t *m, unsigned bytes) {
    if (!m || bytes < 2U || m[0] < 0x80U || m[0] >= 0xf0U) return;
    unsigned kind = m[0] & 0xf0U, ch = m[0] & 15U;
    if ((kind == 0x80U || kind == 0x90U) && bytes >= 3U)
        enqueue(ch,m[1],(kind == 0x80U || m[2] == 0U) ? 0U : m[2],0U);
    else if (kind == 0xb0U && bytes >= 3U && m[1] == 64U)
        enqueue(ch,0U,m[2]>=64U,1U);
}
static ORCH_AUDIO void release_voice(unsigned ch, unsigned note) {
    for (unsigned i=0;i<ORCH_MAX_VOICES;i++) {
        voice_t *v=&voices[i];
        if (v->active && v->channel==ch && v->note==note) {
            v->gate=0U;
            if (ch >= ORCH_MIDI_CHANNELS || !sustain[ch]) v->releasing=1U;
        }
    }
}
static ORCH_AUDIO void adaptive_note(unsigned ch, unsigned note, unsigned velocity,
                                      uint32_t *attack_ms, uint32_t *release_ms,
                                      uint16_t *vib, uint16_t *trem) {
    adaptive_t *a=&adaptive[ch];
    uint8_t same_onset=(uint8_t)(a->initialized && a->last_on==sample_counter);
    uint32_t ioi= (a->initialized && !same_onset) ? sample_counter-a->last_on : 0U;
    if (a->initialized && !same_onset && ioi>0U) {
        if (!a->mean_ioi) a->mean_ioi=ioi;
        else a->mean_ioi=(a->mean_ioi*7U+ioi)/8U;
        if (ioi > a->mean_ioi*3U) a->phrase_notes=0U;
    }
    if (!same_onset && a->phrase_notes<255U) ++a->phrase_notes;
    uint32_t density=(a->mean_ioi && ioi) ? (a->mean_ioi*100U)/ioi : 50U;
    density=clamp_u32(density,25U,200U);
    uint32_t energy=velocity;
    /* Soft, velocity-sensitive onset; longer phrase peaks get slightly more presence. */
    *attack_ms= selected_family==ORCH_FAMILY_PERCUSSION ? 3U :
        22U + (127U-energy)/3U + (density<60U ? 8U : 0U);
    if (a->phrase_notes>4U && velocity>a->last_velocity) *attack_ms=(*attack_ms*9U)/10U;
    *release_ms= selected_family==ORCH_FAMILY_PERCUSSION ? 180U :
        95U + (127U-velocity) + (density<55U ? 65U : 0U);
    if (selected_family==ORCH_FAMILY_VIOLIN || selected_family==ORCH_FAMILY_VIOLA ||
        selected_family==ORCH_FAMILY_CELLO || selected_family==ORCH_FAMILY_BASS) {
        *vib=(uint16_t)(ioi>ORCH_SAMPLE_RATE/3U ? 4U : 2U);
        if (velocity<45U) *vib=(uint16_t)(*vib/2U);
        *trem=(uint16_t)(a->phrase_notes>3U && density>75U ? 1400U : 450U);
    } else {
        *vib=(uint16_t)(selected_family==ORCH_FAMILY_CHOIR ? 2U : 1U);
        *trem=(uint16_t)(a->phrase_notes>5U ? 600U : 0U);
    }
    if (!same_onset) a->last_on=sample_counter;
    a->last_note=(uint8_t)note;
    a->last_velocity=(uint8_t)velocity; a->initialized=1U;
}
static ORCH_AUDIO void start_voice(unsigned ch, unsigned note, unsigned velocity) {
    unsigned chosen=ORCH_MAX_VOICES, oldest_index=0U;
    int32_t quietest=2147483647;
    uint32_t oldest=0U;
    for (unsigned i=0;i<ORCH_MAX_VOICES;i++) {
        voice_t *v=&voices[i];
        if (!v->active) { chosen=i; break; }
        if (v->releasing && v->envelope<quietest) { quietest=v->envelope; chosen=i; }
        if (!v->releasing && v->age>=oldest) { oldest=v->age; oldest_index=i; }
    }
    if (chosen==ORCH_MAX_VOICES) chosen=oldest_index;
    voice_t *v=&voices[chosen];
    uint16_t target=note_delay(note);
    uint32_t attack_ms=20U, release_ms=140U;
    uint16_t vib=0U, trem=0U;
    if (ch<ORCH_MIDI_CHANNELS) adaptive_note(ch,note,velocity,&attack_ms,&release_ms,&vib,&trem);
    else {
        attack_ms=selected_model==ORCH_MODEL_PLUCKED_STRING?5U:24U;
        release_ms=selected_model==ORCH_MODEL_PLUCKED_STRING?520U:180U;
    }
    uint8_t legato=0U;
    if (ch<ORCH_MIDI_CHANNELS) for (unsigned i=0;i<ORCH_MAX_VOICES;i++)
        if (voices[i].active && voices[i].channel==ch && voices[i].gate) { legato=1U; break; }
    v->target_length=target;
    v->length=(legato && v->length) ? v->length : target;
    v->glide_samples=legato ? ORCH_SAMPLE_RATE/14U : 0U;
    v->attack_samples=(ORCH_SAMPLE_RATE*attack_ms)/1000U;
    v->release_samples=(ORCH_SAMPLE_RATE*release_ms)/1000U;
    v->age=++age_counter; v->age_samples=0U;
    v->filter=0; v->envelope=0; v->index=0U;
    v->note=(uint8_t)note; v->channel=(uint8_t)ch; v->velocity=(uint8_t)velocity;
    v->active=1U; v->gate=1U; v->releasing=0U;
    v->vib_phase=rng; v->vib_step=(uint32_t)(selected_family==ORCH_FAMILY_CHOIR?330000U:450000U);
    v->vib_depth=vib; v->tremolo_depth=trem; v->attack_scale=1U; v->release_scale=1U;
    for (unsigned n=0;n<v->length;n++)
        v->delay[n]=(int16_t)((noise16()*(int32_t)velocity)/2048);
}
static ORCH_AUDIO void drain_events(void) {
    while (event_read!=event_write) {
        event_t e=events[event_read];
        event_read=(uint8_t)((event_read+1U)&(EVENT_CAP-1U));
        if (e.type==2U) {
            for (unsigned i=0;i<ORCH_MAX_VOICES;i++) voices[i].active=0U;
            for (unsigned ch=0;ch<ORCH_MIDI_CHANNELS;ch++) sustain[ch]=0U;
        } else if (e.type==1U) {
            sustain[e.channel]=e.velocity;
            if (!e.velocity) for (unsigned i=0;i<ORCH_MAX_VOICES;i++)
                if (voices[i].active && voices[i].channel==e.channel && !voices[i].gate)
                    voices[i].releasing=1U;
        } else if (e.velocity) start_voice(e.channel,e.note,e.velocity);
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
    for (unsigned c=0;c<16U;c++) { sustain[c]=0U; adaptive[c]=(adaptive_t){0}; }
    orch_seq_clear(); tempo=120U; seq_enabled=seq_started=seq_stop_pending=0U;
    current_step=0U; sample_counter=step_start=0U; divisi_enabled=1U;
    for (unsigned n=0;n<ORCH_STEP_NOTES;n++) { seq_active[n]=0U; seq_off_at[n]=0U; seq_note[n]=0U; }
    selected_model=ORCH_MODEL_WAVEGUIDE_STRING; selected_family=ORCH_FAMILY_VIOLIN;
}
void orch_seq_set_tempo(uint16_t bpm) {
    if (bpm<30U) bpm=30U;
    if (bpm>300U) bpm=300U;
    tempo=bpm;
}
void orch_seq_set_enabled(unsigned enabled) {
    if (seq_enabled && !enabled) seq_stop_pending=1U;
    seq_enabled=enabled?1U:0U; seq_started=0U;
}
int orch_seq_set_step(unsigned step,unsigned slot,const struct orch_step_note *n) {
    if (step>=16U || slot>=4U || !n || n->note>127U || n->velocity>127U || n->gate_percent>100U) return -1;
    pattern[step][slot]=*n;
    if (n->velocity && pattern_count[step]<=slot) pattern_count[step]=(uint8_t)(slot+1U);
    else if (!n->velocity && pattern_count[step]==slot+1U)
        while (pattern_count[step] && !pattern[step][pattern_count[step]-1U].velocity) --pattern_count[step];
    return 0;
}
void orch_set_model(enum orch_model model) { if (model<=ORCH_MODEL_PLUCKED_STRING) selected_model=model; }
void orch_set_family(enum orch_family family) { if (family<=ORCH_FAMILY_PERCUSSION) selected_family=family; }
void orch_set_divisi(unsigned enabled) { divisi_enabled=enabled?1U:0U; }
void orch_plan_divisi(const uint8_t *notes,unsigned note_count,unsigned density,struct orch_divisi_plan *p) {
    static const int8_t offsets[6]={0,0,7,-5,12,-12};
    if (!p) return;
    p->player_count=0U; p->active_mask=0U; p->density=(uint8_t)clamp_u32(density,0U,100U);
    for (unsigned i=0;i<6U;i++) { p->semitone_offset[i]=0; p->part_assignment[i]=0U; }
    if (!notes || !note_count) return;
    unsigned count=note_count>6U?6U:note_count;
    unsigned players=divisi_enabled ? ((density<25U && note_count==1U)?4U:(density<55U && note_count<3U)?3U:count) : count;
    if (players>6U) players=6U;
    p->player_count=(uint8_t)players;
    for (unsigned i=0;i<players;i++) {
        unsigned part=i<count?i:(i%count);
        p->part_assignment[i]=(uint8_t)part;
        p->semitone_offset[i]=(i<count)?0:offsets[i];
        p->active_mask|=(uint8_t)(1U<<i);
    }
}
unsigned orch_active_voices(void) {
    unsigned n=0; for (unsigned i=0;i<ORCH_MAX_VOICES;i++) n+=voices[i].active?1U:0U;
    return n;
}
static ORCH_AUDIO void seq_tick(void) {
    uint32_t step_samples=(ORCH_SAMPLE_RATE*60U)/((uint32_t)tempo*4U);
    if (!seq_enabled) {
        if (seq_stop_pending) {
            for (unsigned n=0;n<ORCH_STEP_NOTES;n++) if (seq_active[n]) {
                release_voice(SEQ_SOURCE,seq_note[n]); seq_active[n]=0U;
            }
            seq_stop_pending=0U;
        }
        return;
    }
    if (!seq_started) { seq_started=1U; current_step=15U; step_start=sample_counter-step_samples; }
    for (unsigned n=0;n<ORCH_STEP_NOTES;n++) if (seq_active[n] && (int32_t)(sample_counter-seq_off_at[n])>=0) {
        release_voice(SEQ_SOURCE,seq_note[n]); seq_active[n]=0U;
    }
    if ((uint32_t)(sample_counter-step_start)>=step_samples) {
        step_start+=step_samples; current_step=(uint8_t)((current_step+1U)&15U);
        for (unsigned n=0;n<ORCH_STEP_NOTES;n++) {
            struct orch_step_note *p=&pattern[current_step][n];
            if (p->velocity) {
                start_voice(SEQ_SOURCE,p->note,p->velocity); seq_note[n]=p->note; seq_active[n]=1U;
                seq_off_at[n]=sample_counter+(step_samples*(p->gate_percent>100U?100U:p->gate_percent))/100U;
            }
        }
    }
}
static ORCH_AUDIO int32_t triangle_lfo(uint32_t phase) {
    uint32_t x=phase>>16;
    if (x<32768U) return (int32_t)(x*2U)-32768;
    return 98303-(int32_t)(x*2U);
}
ORCH_AUDIO void orch_render_q23(int32_t *out,unsigned frames) {
    if (!out) return;
    drain_events();
    for (unsigned f=0;f<frames;f++) {
        seq_tick();
        int64_t sum=0; unsigned active=0U;
        for (unsigned i=0;i<ORCH_MAX_VOICES;i++) {
            voice_t *v=&voices[i]; if (!v->active) continue;
            ++active; ++v->age_samples;
            if (v->glide_samples && v->length!=v->target_length) {
                int32_t diff=(int32_t)v->target_length-(int32_t)v->length;
                v->length=(uint16_t)((int32_t)v->length+diff/(int32_t)(v->glide_samples>0U?v->glide_samples:1U));
                --v->glide_samples; if (!v->glide_samples) v->length=v->target_length;
                if (v->length<8U) v->length=8U;
            }
            int32_t vib=triangle_lfo(v->vib_phase);
            v->vib_phase+=v->vib_step;
            int32_t vib_offset=(vib*(int32_t)v->vib_depth)>>15;
            int32_t read_index=(int32_t)v->index+vib_offset;
            while (read_index<0) read_index+=v->length;
            read_index%=(int32_t)v->length;
            int32_t delayed=v->delay[(unsigned)read_index];
            v->filter+=(delayed-v->filter)>>3;
            int32_t loop=v->filter-(v->filter>>15);
            if (v->gate) {
                int32_t excitation=(noise16()*(int32_t)v->velocity)>>11;
                loop += selected_model==ORCH_MODEL_WAVEGUIDE_STRING ? (excitation>>3) : (excitation>>1);
            }
            v->delay[v->index]=(int16_t)clamp16(loop);
            v->index=(uint16_t)((v->index+1U)%v->length);
            if (v->gate && v->envelope<ENV_MAX) {
                uint32_t attack=v->attack_samples?v->attack_samples:1U;
                v->envelope+=(int32_t)(ENV_MAX/clamp_u32(attack,1U,ORCH_SAMPLE_RATE));
                if (v->envelope>ENV_MAX) v->envelope=ENV_MAX;
            } else if (v->releasing) {
                uint32_t rel=v->release_samples?v->release_samples:1U;
                v->envelope-=(int32_t)(v->envelope/(int32_t)clamp_u32(rel,1U,ORCH_SAMPLE_RATE)+1);
                if (v->envelope<=8) { v->active=0U; v->envelope=0; continue; }
            }
            uint32_t phase=v->vib_phase;
            int32_t trem=32767-((triangle_lfo(phase>>1)*(int32_t)v->tremolo_depth)>>15);
            int32_t level=(v->filter*v->envelope)>>15;
            level=(level*trem)>>15;
            sum+=(int64_t)level*v->velocity>>7;
        }
        int64_t scaled=active ? (sum*256)/(int64_t)active : 0;
        if (scaled>Q23_MAX) scaled=Q23_MAX;
        if (scaled<Q23_MIN) scaled=Q23_MIN;
        out[f*2U]=(int32_t)scaled; out[f*2U+1U]=(int32_t)scaled;
        ++sample_counter;
    }
}
