// SPDX-License-Identifier: GPL-2.0-or-later
/* The GDA engine on Intel E810: KNOD_PERSIST_FMT_ICE_E810. */
#include "gda.h"
#include "nic_ice.h"

#define GDA_FN(f)	cfn_##f##_ice_e810
#include "gda_engine.h"
