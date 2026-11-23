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
#define Q8_ONE 256U

typedef struct {
    uint16_t loop_gain;       /* Q15 round-trip gain */
    uint8_t damping_shift;    /* one-pole loss filter */
    uint8_t attack_ms;
    uint16_t release_ms;
    uint8_t vibrato_cents;
    uint8_t bow_pressure;
    uint8_t air_noise;
    uint8_t body_mix;
} instrument_profile_t;

typedef struct {
    int16_t delay[DELAY_SIZE];
    int16_t resonator[512];
    uint16_t length;
    uint16_t target_length;
    uint16_t resonator_length, resonator_index, resonator_gain;
    uint16_t index;
    uint32_t delay_q8;
    uint32_t target_delay_q8;
    uint32_t glide_left;
    int32_t loss_state;
    int32_t body_low;
    int32_t body_high;
    int32_t envelope;
    int32_t bore_state;
    int32_t resonator_filter;
    uint32_t age;
    uint32_t attack_samples;
    uint32_t release_samples;
    uint32_t vib_phase;
    uint32_t vib_step;
    uint32_t exciter_phase, exciter_step;
    uint16_t vib_depth;
    uint16_t tremolo_depth;
    uint8_t note, channel, velocity, family;
    uint8_t active, gate, releasing, excited;
    uint8_t articulation;
} voice_t;

typedef struct { uint8_t channel, note, velocity, type; } event_t;
typedef struct {
    uint32_t last_on, mean_ioi;
    uint8_t last_note, last_velocity, phrase_notes, initialized;
} adaptive_t;

static const instrument_profile_t profiles[] = {
    /* loop gain, loss LP, attack, release, vibrato cents, bow pressure, air, body */
    {32680U, 4U, 42U, 260U, 7U, 82U, 4U, 52U}, /* violin */
    {32650U, 4U, 48U, 320U, 6U, 88U, 4U, 58U}, /* viola */
    {32630U, 5U, 62U, 460U, 5U, 92U, 3U, 68U}, /* cello */
    {32580U, 5U, 78U, 620U, 4U, 96U, 3U, 76U}, /* bass */
    {32150U, 3U, 24U, 180U, 3U, 0U, 42U, 34U}, /* woodwind */
    {31950U, 3U, 30U, 220U, 4U, 0U, 30U, 46U}, /* brass */
    {31750U, 3U, 90U, 520U, 5U, 0U, 12U, 62U}, /* choir */
    {30800U, 2U, 2U, 260U, 0U, 0U, 70U, 84U}   /* percussion */
};

static voice_t voices[ORCH_MAX_VOICES];
static event_t events[EVENT_CAP];
static volatile uint8_t event_write, event_read;
static uint8_t sustain[ORCH_MIDI_CHANNELS];
static enum orch_family channel_family[ORCH_MIDI_CHANNELS];
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
static enum orch_family default_family;
static uint32_t rng = 0x6d2b79f5U;

static ORCH_AUDIO int32_t clamp16(int32_t x) {
    if (x > 32767) return 32767;
    if (x < -32768) return -32768;
    return x;
}
static ORCH_AUDIO int32_t clamp32(int32_t x, int32_t lo, int32_t hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}
static ORCH_AUDIO uint32_t clamp_u32(uint32_t x, uint32_t lo, uint32_t hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}
static ORCH_AUDIO int32_t noise16(void) {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (int16_t)(rng >> 16);
}
static ORCH_AUDIO int32_t triangle_lfo(uint32_t phase) {
    uint32_t x = phase >> 16;
    if (x < 32768U) return (int32_t)(x * 2U) - 32768;
    return 98303 - (int32_t)(x * 2U);
}
static ORCH_AUDIO uint16_t note_delay(unsigned note) {
    static const uint16_t semitone[12] =
        {170U,160U,151U,143U,135U,127U,120U,113U,107U,101U,95U,90U};
    int octave = (int)(note / 12U) - 5;
    uint32_t d = semitone[note % 12U];
    if (octave > 0) d >>= (unsigned)octave;
    else if (octave < 0) {
        unsigned shift = (unsigned)(-octave);
        if (shift > 3U) shift = 3U;
        d <<= shift;
    }
    return (uint16_t)clamp_u32(d, 8U, DELAY_SIZE - 1U);
}
static ORCH_AUDIO uint16_t instrument_delay(unsigned note, enum orch_family family) {
    uint32_t d = note_delay(note);
    if (family == ORCH_FAMILY_WOODWIND || family == ORCH_FAMILY_BRASS)
        d = (d + 1U) / 2U;
    return (uint16_t)clamp_u32(d, 8U, DELAY_SIZE - 1U);
}
static ORCH_AUDIO uint16_t body_resonator_length(enum orch_family family, uint16_t pitch_delay) {
    uint32_t n;
    switch (family) {
    case ORCH_FAMILY_VIOLIN: n = 88U; break;
    case ORCH_FAMILY_VIOLA: n = 126U; break;
    case ORCH_FAMILY_CELLO: n = 200U; break;
    case ORCH_FAMILY_BASS: n = 400U; break;
    case ORCH_FAMILY_WOODWIND: n = pitch_delay / 2U; break;
    case ORCH_FAMILY_BRASS: n = pitch_delay / 3U; break;
    case ORCH_FAMILY_CHOIR: n = 70U; break;
    default: n = (pitch_delay * 3U) / 2U; break;
    }
    return (uint16_t)clamp_u32(n, 8U, 511U);
}
static ORCH_AUDIO uint16_t body_resonator_gain(enum orch_family family) {
    switch (family) {
    case ORCH_FAMILY_VIOLIN: return 31600U;
    case ORCH_FAMILY_VIOLA: return 31650U;
    case ORCH_FAMILY_CELLO: return 31700U;
    case ORCH_FAMILY_BASS: return 31750U;
    case ORCH_FAMILY_WOODWIND: return 30800U;
    case ORCH_FAMILY_BRASS: return 30600U;
    case ORCH_FAMILY_CHOIR: return 31200U;
    default: return 29200U;
    }
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
    if (ch < 16U && note < 128U && velocity < 128U) enqueue(ch, note, velocity, 0U);
}
void orch_panic(void) { enqueue(0U, 0U, 0U, 2U); }
void orch_midi_short_message(const uint8_t *m, unsigned bytes) {
    if (!m || bytes < 2U || m[0] < 0x80U || m[0] >= 0xf0U) return;
    unsigned kind = m[0] & 0xf0U, ch = m[0] & 15U;
    if ((kind == 0x80U || kind == 0x90U) && bytes >= 3U)
        enqueue(ch, m[1], (kind == 0x80U || m[2] == 0U) ? 0U : m[2], 0U);
    else if (kind == 0xb0U && bytes >= 3U && m[1] == 64U)
        enqueue(ch, 0U, m[2] >= 64U, 1U);
}
static ORCH_AUDIO int32_t read_fractional_delay(const voice_t *v, uint32_t delay_q8) {
    uint32_t whole = delay_q8 >> 8;
    uint32_t frac = delay_q8 & 255U;
    int32_t a = (int32_t)v->index - (int32_t)whole;
    while (a < 0) a += v->length;
    while (a >= v->length) a -= v->length;
    int32_t b = a - 1;
    if (b < 0) b = v->length - 1;
    int32_t sa = v->delay[(unsigned)a];
    int32_t sb = v->delay[(unsigned)b];
    return (sa * (int32_t)(256U - frac) + sb * (int32_t)frac) >> 8;
}
static ORCH_AUDIO void release_voice(unsigned ch, unsigned note) {
    for (unsigned i = 0; i < ORCH_MAX_VOICES; ++i) {
        voice_t *v = &voices[i];
        if (v->active && v->channel == ch && v->note == note) {
            v->gate = 0U;
            if (ch >= ORCH_MIDI_CHANNELS || !sustain[ch]) v->releasing = 1U;
        }
    }
}
static ORCH_AUDIO void adaptive_note(unsigned ch, unsigned note, unsigned velocity,
        uint32_t *attack_ms, uint32_t *release_ms, uint16_t *vib, uint16_t *trem) {
    adaptive_t *a = &adaptive[ch];
    uint8_t same_onset = (uint8_t)(a->initialized && a->last_on == sample_counter);
    uint32_t ioi = (a->initialized && !same_onset) ? sample_counter - a->last_on : 0U;
    if (a->initialized && !same_onset && ioi) {
        if (!a->mean_ioi) a->mean_ioi = ioi;
        else a->mean_ioi = (a->mean_ioi * 7U + ioi) / 8U;
        if (ioi > a->mean_ioi * 3U) a->phrase_notes = 0U;
    }
    if (!same_onset && a->phrase_notes < 255U) ++a->phrase_notes;
    uint32_t density = (a->mean_ioi && ioi) ? (a->mean_ioi * 100U) / ioi : 50U;
    density = clamp_u32(density, 25U, 200U);
    const instrument_profile_t *p = &profiles[channel_family[ch]];
    *attack_ms = p->attack_ms + (127U - velocity) / 4U + (density < 55U ? 8U : 0U);
    *release_ms = p->release_ms + (127U - velocity) * 2U + (density < 55U ? 50U : 0U);
    *vib = (uint16_t)(p->vibrato_cents * ((velocity < 40U) ? 1U : 2U));
    *trem = (uint16_t)((a->phrase_notes > 3U && density > 75U) ? 900U : 250U);
    if (!same_onset) a->last_on = sample_counter;
    a->last_note = (uint8_t)note;
    a->last_velocity = (uint8_t)velocity;
    a->initialized = 1U;
}
static ORCH_AUDIO void excite_pluck(voice_t *v, const instrument_profile_t *p) {
    unsigned length = v->length;
    unsigned burst = length / 3U;
    if (burst < 8U) burst = 8U;
    if (burst > 180U) burst = 180U;
    for (unsigned i = 0; i < length; ++i) {
        int32_t x = i < burst ? noise16() : 0;
        /* A short shaped noise burst approximates the string's initial displacement. */
        int32_t window = i < burst ? (int32_t)(burst - i) : 0;
        v->delay[i] = (int16_t)clamp16((x * window * p->body_mix) / (128 * (int32_t)burst));
    }
    v->excited = 1U;
}
static ORCH_AUDIO void start_voice(unsigned ch, unsigned note, unsigned velocity) {
    unsigned chosen = ORCH_MAX_VOICES, oldest_index = 0U;
    int32_t quietest = 2147483647;
    uint32_t oldest = UINT32_MAX;
    for (unsigned i = 0; i < ORCH_MAX_VOICES; ++i) {
        voice_t *v = &voices[i];
        if (!v->active) { chosen = i; break; }
        if (v->releasing && v->envelope < quietest) { quietest = v->envelope; chosen = i; }
        if (!v->releasing && v->age < oldest) { oldest = v->age; oldest_index = i; }
    }
    if (chosen == ORCH_MAX_VOICES) chosen = oldest_index;
    voice_t *v = &voices[chosen];
    enum orch_family family = ch < ORCH_MIDI_CHANNELS ? channel_family[ch] : default_family;
    const instrument_profile_t *p = &profiles[family];
    uint16_t base = instrument_delay(note, family);
    uint32_t attack_ms = p->attack_ms, release_ms = p->release_ms;
    uint16_t vib = p->vibrato_cents, trem = 0U;
    if (ch < ORCH_MIDI_CHANNELS) adaptive_note(ch, note, velocity, &attack_ms, &release_ms, &vib, &trem);
    uint8_t legato = 0U;
    uint32_t glide_start = (uint32_t)base * Q8_ONE;
    uint16_t glide_length = base;
    uint32_t newest_age = 0U;
    if (ch < ORCH_MIDI_CHANNELS) {
        for (unsigned i = 0; i < ORCH_MAX_VOICES; ++i) {
            voice_t *prior = &voices[i];
            if (prior->active && prior->channel == ch && prior->gate &&
                prior->age >= newest_age) {
                newest_age = prior->age;
                glide_start = prior->delay_q8;
                glide_length = prior->length;
                legato = 1U;
            }
        }
    }
    v->length = legato ? glide_length : base;
    v->resonator_length = body_resonator_length(family, base);
    v->resonator_index = 0U;
    v->resonator_gain = body_resonator_gain(family);
    v->resonator_filter = 0;
    v->target_length = base;
    v->delay_q8 = glide_start;
    v->target_delay_q8 = (uint32_t)base * Q8_ONE;
    v->glide_left = legato ? ORCH_SAMPLE_RATE / 10U : 0U;
    v->age = ++age_counter;
    v->loss_state = v->body_low = v->body_high = v->bore_state = 0;
    v->envelope = 0;
    v->attack_samples = (ORCH_SAMPLE_RATE * attack_ms) / 1000U;
    v->release_samples = (ORCH_SAMPLE_RATE * release_ms) / 1000U;
    v->note = (uint8_t)note; v->channel = (uint8_t)ch;
    v->velocity = (uint8_t)velocity; v->family = (uint8_t)family;
    v->active = 1U; v->gate = 1U; v->releasing = 0U;
    v->articulation = (uint8_t)(selected_model == ORCH_MODEL_PLUCKED_STRING);
    v->vib_phase = rng;
    v->vib_step = (uint32_t)(family == ORCH_FAMILY_CHOIR ? 240000U : 390000U);
    v->vib_depth = vib;
    v->tremolo_depth = trem;
    v->exciter_phase = rng ^ (note * 7919U);
    v->exciter_step = (uint32_t)((family == ORCH_FAMILY_WOODWIND || family == ORCH_FAMILY_BRASS) ?
        (0x7fffffffU / (uint32_t)base) : (0xffffffffU / (uint32_t)base));
    v->excited = 0U;
    v->index = 0U;
    for (unsigned i = 0; i < v->length; ++i) v->delay[i] = 0;
    for (unsigned i = 0; i < v->resonator_length; ++i) v->resonator[i] = 0;
    if (v->articulation || family == ORCH_FAMILY_PERCUSSION) excite_pluck(v, p);
}
static ORCH_AUDIO void drain_events(void) {
    while (event_read != event_write) {
        event_t e = events[event_read];
        event_read = (uint8_t)((event_read + 1U) & (EVENT_CAP - 1U));
        if (e.type == 2U) {
            for (unsigned i = 0; i < ORCH_MAX_VOICES; ++i) voices[i].active = 0U;
            for (unsigned c = 0; c < ORCH_MIDI_CHANNELS; ++c) sustain[c] = 0U;
        } else if (e.type == 1U) {
            sustain[e.channel] = e.velocity;
            if (!e.velocity) for (unsigned i = 0; i < ORCH_MAX_VOICES; ++i)
                if (voices[i].active && voices[i].channel == e.channel && !voices[i].gate)
                    voices[i].releasing = 1U;
        } else if (e.velocity) start_voice(e.channel, e.note, e.velocity);
        else release_voice(e.channel, e.note);
    }
}
void orch_seq_clear(void) {
    for (unsigned s = 0; s < ORCH_SEQ_STEPS; ++s) {
        pattern_count[s] = 0U;
        for (unsigned n = 0; n < ORCH_STEP_NOTES; ++n) {
            pattern[s][n].note = 60U;
            pattern[s][n].velocity = 0U;
            pattern[s][n].gate_percent = 80U;
        }
    }
}
void orch_init(void) {
    for (unsigned i = 0; i < ORCH_MAX_VOICES; ++i) voices[i].active = 0U;
    event_read = event_write = 0U;
    for (unsigned c = 0; c < ORCH_MIDI_CHANNELS; ++c) {
        sustain[c] = 0U; adaptive[c] = (adaptive_t){0};
        channel_family[c] = ORCH_FAMILY_VIOLIN;
    }
    orch_seq_clear();
    tempo = 120U; seq_enabled = seq_started = seq_stop_pending = 0U;
    current_step = 0U; sample_counter = step_start = 0U;
    divisi_enabled = 1U;
    for (unsigned n = 0; n < ORCH_STEP_NOTES; ++n) {
        seq_active[n] = 0U; seq_off_at[n] = 0U; seq_note[n] = 0U;
    }
    selected_model = ORCH_MODEL_WAVEGUIDE_STRING;
    default_family = ORCH_FAMILY_VIOLIN;
}
void orch_seq_set_tempo(uint16_t bpm) {
    if (bpm < 30U) bpm = 30U;
    if (bpm > 300U) bpm = 300U;
    tempo = bpm;
}
void orch_seq_set_enabled(unsigned enabled) {
    if (seq_enabled && !enabled) seq_stop_pending = 1U;
    seq_enabled = enabled ? 1U : 0U;
    seq_started = 0U;
}
int orch_seq_set_step(unsigned step, unsigned slot, const struct orch_step_note *n) {
    if (step >= ORCH_SEQ_STEPS || slot >= ORCH_STEP_NOTES || !n ||
        n->note > 127U || n->velocity > 127U || n->gate_percent > 100U) return -1;
    pattern[step][slot] = *n;
    if (n->velocity && pattern_count[step] <= slot) pattern_count[step] = (uint8_t)(slot + 1U);
    else if (!n->velocity && pattern_count[step] == slot + 1U)
        while (pattern_count[step] && !pattern[step][pattern_count[step] - 1U].velocity)
            --pattern_count[step];
    return 0;
}
void orch_set_model(enum orch_model model) {
    if (model <= ORCH_MODEL_PLUCKED_STRING) selected_model = model;
}
void orch_set_family(enum orch_family family) {
    if (family <= ORCH_FAMILY_PERCUSSION) {
        default_family = family;
        for (unsigned c = 0; c < ORCH_MIDI_CHANNELS; ++c) channel_family[c] = family;
    }
}
void orch_set_channel_family(unsigned channel, enum orch_family family) {
    if (channel < ORCH_MIDI_CHANNELS && family <= ORCH_FAMILY_PERCUSSION)
        channel_family[channel] = family;
}
void orch_set_divisi(unsigned enabled) { divisi_enabled = enabled ? 1U : 0U; }
void orch_plan_divisi(const uint8_t *notes, unsigned note_count, unsigned density,
                      struct orch_divisi_plan *p) {
    static const int8_t offsets[6] = {0, 0, 7, -5, 12, -12};
    if (!p) return;
    p->player_count = 0U; p->active_mask = 0U;
    p->density = (uint8_t)clamp_u32(density, 0U, 100U);
    for (unsigned i = 0; i < 6U; ++i) {
        p->semitone_offset[i] = 0; p->part_assignment[i] = 0U;
    }
    if (!notes || !note_count) return;
    unsigned count = note_count > 6U ? 6U : note_count;
    unsigned players = divisi_enabled ?
        ((density < 25U && note_count == 1U) ? 4U :
        ((density < 55U && note_count < 3U) ? 3U : count)) : count;
    if (players > 6U) players = 6U;
    p->player_count = (uint8_t)players;
    for (unsigned i = 0; i < players; ++i) {
        unsigned part = i < count ? i : (i % count);
        p->part_assignment[i] = (uint8_t)part;
        p->semitone_offset[i] = (i < count) ? 0 : offsets[i];
        p->active_mask |= (uint8_t)(1U << i);
    }
}
unsigned orch_active_voices(void) {
    unsigned n = 0U;
    for (unsigned i = 0; i < ORCH_MAX_VOICES; ++i) n += voices[i].active ? 1U : 0U;
    return n;
}
static ORCH_AUDIO void seq_tick(void) {
    uint32_t step_samples = (ORCH_SAMPLE_RATE * 60U) / ((uint32_t)tempo * 4U);
    if (!seq_enabled) {
        if (seq_stop_pending) {
            for (unsigned n = 0; n < ORCH_STEP_NOTES; ++n) if (seq_active[n]) {
                release_voice(SEQ_SOURCE, seq_note[n]); seq_active[n] = 0U;
            }
            seq_stop_pending = 0U;
        }
        return;
    }
    if (!seq_started) {
        seq_started = 1U; current_step = 15U; step_start = sample_counter - step_samples;
    }
    for (unsigned n = 0; n < ORCH_STEP_NOTES; ++n)
        if (seq_active[n] && (int32_t)(sample_counter - seq_off_at[n]) >= 0) {
            release_voice(SEQ_SOURCE, seq_note[n]); seq_active[n] = 0U;
        }
    if ((uint32_t)(sample_counter - step_start) >= step_samples) {
        step_start += step_samples; current_step = (uint8_t)((current_step + 1U) & 15U);
        for (unsigned n = 0; n < ORCH_STEP_NOTES; ++n) {
            struct orch_step_note *p = &pattern[current_step][n];
            if (p->velocity) {
                start_voice(SEQ_SOURCE, p->note, p->velocity);
                seq_note[n] = p->note; seq_active[n] = 1U;
                seq_off_at[n] = sample_counter +
                    (step_samples * (p->gate_percent > 100U ? 100U : p->gate_percent)) / 100U;
            }
        }
    }
}
/* Distinct low-cost excitation paths; all share the same bounded resonator core. */
static ORCH_AUDIO int32_t instrument_excitation(voice_t *v, const instrument_profile_t *p,
                                                  int32_t string_velocity) {
    int32_t n = noise16();
    uint32_t phase = v->exciter_phase;
    v->exciter_phase += v->exciter_step;
    switch ((enum orch_family)v->family) {
    case ORCH_FAMILY_VIOLIN:
    case ORCH_FAMILY_VIOLA:
    case ORCH_FAMILY_CELLO:
    case ORCH_FAMILY_BASS: {
        if (v->articulation) return 0;
        int32_t bow = ((int32_t)v->velocity - 64) * 180;
        int32_t slip = bow - string_velocity;
        int32_t friction = clamp32((slip * (int32_t)p->bow_pressure) >> 7, -9000, 9000);
        return (friction + ((n * p->air_noise) >> 9)) >> 2;
    }
    case ORCH_FAMILY_WOODWIND: {
        int32_t jet = triangle_lfo(phase);
        int32_t breath = ((n * p->air_noise) >> 5) + ((jet * (int32_t)v->velocity) >> 10);
        v->bore_state += (breath - v->bore_state) >> 3;
        return v->bore_state >> 2;
    }
    case ORCH_FAMILY_BRASS: {
        int32_t lip = triangle_lfo(phase);
        int32_t pressure = ((lip * (int32_t)v->velocity) >> 9) - (string_velocity >> 1);
        v->bore_state += (pressure - v->bore_state) >> 2;
        return clamp32(v->bore_state >> 2, -11000, 11000);
    }
    case ORCH_FAMILY_CHOIR: {
        int32_t fundamental = triangle_lfo(phase);
        int32_t overtone = triangle_lfo(phase * 2U);
        int32_t vowel = (fundamental * 3 + overtone) >> 2;
        v->body_low += (vowel - v->body_low) >> 4;
        v->body_high += ((vowel - v->body_low) - v->body_high) >> 2;
        return (v->body_low + (v->body_high >> 1)) >> 3;
    }
    case ORCH_FAMILY_PERCUSSION:
        return 0;
    default:
        return 0;
    }
}
static ORCH_AUDIO void render_voice(voice_t *v, int64_t *mix) {
    const instrument_profile_t *p = &profiles[v->family];
    int32_t lfo = triangle_lfo(v->vib_phase);
    v->vib_phase += v->vib_step;
    uint32_t delay_q8 = v->delay_q8;
    if (v->vib_depth && v->family != ORCH_FAMILY_PERCUSSION) {
        /* Small pitch modulation in cents, applied to fractional delay. */
        int32_t cents = (lfo * (int32_t)v->vib_depth) >> 15;
        int32_t delta = (int32_t)((v->length * (uint32_t)(cents < 0 ? -cents : cents) * 38U) / 256U);
        delay_q8 = (uint32_t)clamp32((int32_t)delay_q8 + (cents < 0 ? delta : -delta),
                                     (int32_t)(8U * Q8_ONE), (int32_t)((DELAY_SIZE - 1U) * Q8_ONE));
    }
    int32_t delayed = read_fractional_delay(v, delay_q8);
    v->loss_state += (delayed - v->loss_state) >> p->damping_shift;
    int32_t loop = (v->loss_state * (int32_t)p->loop_gain) >> 15;
    if (v->family == ORCH_FAMILY_WOODWIND || v->family == ORCH_FAMILY_BRASS) loop = -loop;
    int32_t excitation = 0;
    if (v->gate && !v->articulation && v->family != ORCH_FAMILY_PERCUSSION)
        excitation = instrument_excitation(v, p, v->loss_state);
    if (v->family == ORCH_FAMILY_PERCUSSION && v->excited && v->age == 0U)
        excitation = noise16() >> 2;
    loop = clamp16(loop + excitation);
    v->delay[v->index] = (int16_t)loop;
    ++v->index;
    int32_t body_in = v->loss_state >> 3;
    int32_t body_fb = (v->resonator_filter * (int32_t)v->resonator_gain) >> 15;
    v->resonator[v->resonator_index] = (int16_t)clamp16(body_fb + body_in);
    v->resonator_filter += ((int32_t)v->resonator[v->resonator_index] - v->resonator_filter) >> 2;
    ++v->resonator_index;
    if (v->resonator_index >= v->resonator_length) v->resonator_index = 0U;
    if (v->index >= v->length) v->index = 0U;

    if (v->glide_left) {
        int32_t diff = (int32_t)v->target_delay_q8 - (int32_t)v->delay_q8;
        v->delay_q8 = (uint32_t)((int32_t)v->delay_q8 + diff / (int32_t)v->glide_left);
        --v->glide_left;
        if (!v->glide_left) {
            v->delay_q8 = v->target_delay_q8;
            v->length = v->target_length;
            if (v->index >= v->length) v->index = 0U;
        }
    }
    if (v->gate && v->envelope < ENV_MAX) {
        uint32_t attack = clamp_u32(v->attack_samples, 1U, ORCH_SAMPLE_RATE);
        v->envelope += ENV_MAX / (int32_t)attack;
        if (v->envelope > ENV_MAX) v->envelope = ENV_MAX;
    } else if (v->releasing || v->family == ORCH_FAMILY_PERCUSSION ||
               (v->articulation && v->family <= ORCH_FAMILY_BASS)) {
        uint32_t rel = clamp_u32(v->release_samples, 1U, ORCH_SAMPLE_RATE * 4U);
        v->envelope -= v->envelope / (int32_t)rel + 1;
        if (v->envelope <= 8) { v->active = 0U; v->envelope = 0; return; }
    }
    int32_t signal = v->loss_state;
    /* Instrument-body coloration: two cheap resonant-ish smoothing stages. */
    v->body_low += (signal - v->body_low) >> 5;
    v->body_high += ((signal - v->body_low) - v->body_high) >> 3;
    signal = signal + ((v->body_low - signal) * (int32_t)p->body_mix >> 7)
                   + (v->body_high >> 2)
                   + ((v->resonator_filter * (int32_t)p->body_mix) >> 7);
    int32_t trem = 32767 - ((triangle_lfo(v->vib_phase >> 1) *
                              (int32_t)v->tremolo_depth) >> 15);
    signal = (signal * v->envelope) >> 15;
    signal = (signal * trem) >> 15;
    *mix += ((int64_t)signal * v->velocity) >> 7;
}
ORCH_AUDIO void orch_render_q23(int32_t *out, unsigned frames) {
    if (!out) return;
    drain_events();
    for (unsigned f = 0; f < frames; ++f) {
        seq_tick();
        int64_t sum = 0;
        unsigned active = 0U;
        for (unsigned i = 0; i < ORCH_MAX_VOICES; ++i) {
            voice_t *v = &voices[i];
            if (!v->active) continue;
            ++active;
            render_voice(v, &sum);
            if (!v->active) --active;
        }
        int64_t scaled = active ? (sum * 256) / (int64_t)active : 0;
        if (scaled > Q23_MAX) scaled = Q23_MAX;
        if (scaled < Q23_MIN) scaled = Q23_MIN;
        out[f * 2U] = (int32_t)scaled;
        out[f * 2U + 1U] = (int32_t)scaled;
        ++sample_counter;
    }
}
