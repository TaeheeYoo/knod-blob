// SPDX-License-Identifier: GPL-2.0-or-later
/* The GDA engine on BCM5760x (Thor2): KNOD_PERSIST_FMT_BNXT_P7. */
#include "gda.h"
#define BNXT_GEN	7
#include "nic_bnxt.h"

#define GDA_FN(f)	cfn_##f##_bnxt_p7
#include "gda_engine.h"
