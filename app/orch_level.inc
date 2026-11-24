/* --- the output, as the panel hears it ------------------------------------ */

/* One byte, written in the audio callback and read by the control task: the
 * light in front of the boards on the stage page follows this. Nothing else in
 * the panel is allowed to read the output — the lamps follow which parts sound,
 * which is a different thing and comes from the engine's voices. */
#define LEVEL_EIGHT 8U               /* a full orchestra: four parts, two each */
#define LEVEL_TOP 2100000L           /* the peak of that orchestra, measured */

static volatile uint8_t output_level;
static volatile uint8_t level_size;  /* the orchestra's size, kept for the tail */

FM1_AUDIO_CODE void orch_level_take(const int32_t *pcm, unsigned samples)
{
    if (!pcm) return;
    uint32_t peak = 0U;
    for (unsigned i = 0; i < samples; ++i) {
        const int32_t s = pcm[i];
        const uint32_t a = (uint32_t)(s < 0 ? -s : s);
        if (a > peak) peak = a;
    }

    /* Two things make the light, and the scale is the orchestra's:
     *
     * the size — how many voices are sounding, eight of them (the four parts at
     * two each) being the top of 24, three steps a voice;
     *
     * the sound — the peak of the block against the peak of that full orchestra,
     * so an attack widens the cone and a note left to ring narrows it again
     * while its voice is still counted. The size stays while the tail rings,
     * which is why a release is not a blackout.
     *
     * The renderer keeps its mix at one level whatever the polyphony — it
     * divides each voice by how many are sounding, so eight voices peak no
     * higher than one (measured on the host: 4.5 M for one voice, 3.7 M for
     * eight, of Q23's 8.39 M). The output alone can therefore never say how big
     * the orchestra is: the count says that, and the output says how loud. A
     * single held note is two players in this engine, so it reads six. */
    uint32_t voices = 0U;
    for (unsigned p = 0; p < ORCH_PARTS; ++p)
        voices += orch_voices_of_part(p);
    if (voices > LEVEL_EIGHT) voices = LEVEL_EIGHT;
    if (voices)
        level_size = (uint8_t)(voices * ORCH_LEVEL_MAX / LEVEL_EIGHT);

    uint32_t env = (peak * ORCH_LEVEL_MAX) / (uint32_t)LEVEL_TOP;
    if (env > ORCH_LEVEL_MAX) env = ORCH_LEVEL_MAX;

    /* rises at once, because an attack is what the eye is looking for, and falls
     * by a sixty-fourth of the gap a block, so the light follows the music and
     * not the frame rate of the panel */
    const uint32_t target = (uint32_t)level_size * env / ORCH_LEVEL_MAX;
    if (target >= output_level)
        output_level = (uint8_t)target;
    else
        output_level = (uint8_t)(output_level - ((output_level - target + 63U) >> 6));
}

unsigned orch_output_level(void) { return output_level; }
