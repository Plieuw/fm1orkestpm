/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Edit transactions against the audio interrupt, as FM1 Core names them
 * (fm1_engine_control.h there). In the HAWK/Felucca firmware they hold the
 * interrupts off for the few microseconds a panel edit takes; they nest. */
#ifndef ORCH_HAWK_FM1_ENGINE_CONTROL_H
#define ORCH_HAWK_FM1_ENGINE_CONTROL_H

#include "fm1.h"

void fm1_engine_pause(void);
void fm1_engine_resume(void);

#endif
