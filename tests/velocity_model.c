/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The models' own level per velocity, by family: what tools/gen_cc_tables.py
 * needs to put every family on the even velocity line. Prints the content of
 * tests/velocity_model.txt (tests/run_velocity_model.sh writes it).
 *
 * For each instrument at C3 and G4, every velocity 1..127: the output power
 * over 200 blocks (0.58 s) in dB, less the output share the engine applied
 * (orch_cc_velocity_q12), so the table that is measured with does not end up
 * in the measurement. Averaged in dB over the family's instruments. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "orch_engine.h"
#include "orch_cc.h"

static int32_t block[256];

int main(void)
{
    static double acc[ORCH_FAM_COUNT][128];
    static unsigned count[ORCH_FAM_COUNT];
    const unsigned notes[2] = { 48U, 67U };
    for (unsigned inst = 0; inst < ORCH_INST_COUNT; ++inst) {
        const unsigned fam = orch_instrument_family(inst);
        for (unsigned k = 0; k < 2U; ++k) {
            ++count[fam];
            for (unsigned v = 1; v < 128U; ++v) {
                orch_init();
                orch_engine_set_instrument(0U, inst);
                orch_engine_set_player_cap(0U, 1U);
                orch_note_event(0U, notes[k], v);
                double power = 0.0;
                long n = 0;
                for (unsigned b = 0; b < 200U; ++b) {
                    memset(block, 0, sizeof block);
                    orch_render_q23(block, 128U);
                    for (unsigned i = 0; i < 256U; i += 2U) {
                        power += (double)block[i] * block[i];
                        ++n;
                    }
                }
                acc[fam][v] += 10.0 * log10(power / n + 1e-9) -
                               20.0 * log10(orch_cc_velocity_q12(fam, v) / 4096.0);
            }
        }
    }
    printf("# the models' own level per velocity, by family (strings woodwinds brass mallets choir):\n");
    printf("# mean output power in dB over the family's instruments at C3 and G4, less the\n");
    printf("# output share applied when measured; tests/run_velocity_model.sh, tools/gen_cc_tables.py\n");
    for (unsigned v = 1; v < 128U; ++v) {
        printf("%u", v);
        for (unsigned f = 0; f < ORCH_FAM_COUNT; ++f) printf(" %.3f", acc[f][v] / count[f]);
        printf("\n");
    }
    return 0;
}
