// SPDX-License-Identifier: GPL-2.0-or-later
/* The GDA engine on BCM5750x (Thor): KNOD_PERSIST_FMT_BNXT_P5. */
#include "gda.h"
#include "nic_bnxt.h"

#define GDA_FN(f)	cfn_##f##_bnxt_p5
#include "gda_engine.h"
