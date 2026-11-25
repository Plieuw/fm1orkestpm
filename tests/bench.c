/* SPDX-License-Identifier: GPL-3.0-or-later */
/* How long the engine takes on the host, per voice-sample, for a factory
 * scene with all eight parts holding a note: tests/run_bench.sh [scene] [s].
 * Relative numbers only: the host is not the PI32v2. */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "orch_engine.h"
#include "orch_fx.h"
#include "orch_scenes.h"
static int32_t block[256];
int main(int argc, char **argv){
  unsigned scene = argc > 1 ? (unsigned)atoi(argv[1]) : 7U; unsigned secs = argc>2?(unsigned)atoi(argv[2]):10U;
  orch_init(); orch_fx_init(); orch_scene_apply(scene);
  for(unsigned p=0;p<ORCH_PARTS;++p){ orch_engine_set_player_cap(p,1); orch_note_event(p,43+5*p,100); }
  unsigned blocks = secs*44100U/128U; unsigned long long vs=0;
  clock_t t0=clock();
  for(unsigned b=0;b<blocks;++b){ memset(block,0,sizeof block); orch_render_q23(block,128); vs+=orch_active_voices()*128ULL;
    if(b%1000==999) for(unsigned p=0;p<ORCH_PARTS;++p){orch_note_event(p,43+5*p,0);orch_note_event(p,43+5*p,100);} }
  double t=(double)(clock()-t0)/CLOCKS_PER_SEC;
  printf("scene %u: %.1f ns per voice-sample (%llu voice-samples), %.2f%% of real time\n", scene, t*1e9/vs, vs, 100.0*t/secs);
  return 0;
}
