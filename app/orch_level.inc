/* --- the output, as the panel hears it ------------------------------------ */

/* One byte, written in the audio callback and read by the control task: the
 * light in front of the boards on the stage page follows this. Nothing else in
 * the panel is allowed to read the output — the lamps follow which parts sound,
 * which is a different thing and comes from the engine's voices. */
#define LEVEL_FULL 8388607L          /* Q23 full scale, as the engine renders */

static volatile uint8_t output_level;

FM1_AUDIO_CODE void orch_level_take(const int32_t *pcm, unsigned samples)
{
    if (!pcm) return;
    uint32_t peak = 0U;
    for (unsigned i = 0; i < samples; ++i) {
        const int32_t s = pcm[i];
        const uint32_t a = (uint32_t)(s < 0 ? -s : s);
        if (a > peak) peak = a;
    }

    /* A quarter of full scale is the top of the light, and the level falls by a
     * sixty-fourth of the gap a block, so the light follows the music and not
     * the frame rate of the panel; it rises at once, because an attack is what
     * the eye is looking for. */
    uint32_t target = (peak * 255U) / (uint32_t)(LEVEL_FULL / 4);
    if (target > 255U) target = 255U;
    if (target >= output_level)
        output_level = (uint8_t)target;
    else
        output_level = (uint8_t)(output_level - ((output_level - target + 63U) >> 6));
}

unsigned orch_output_level(void) { return output_level; }
