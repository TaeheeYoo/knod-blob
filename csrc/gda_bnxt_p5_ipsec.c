// SPDX-License-Identifier: GPL-2.0-or-later
/* KNOD_PERSIST_FMT_BNXT_P5's engine that decrypts the ESP packets it passes. */
#include "gda.h"
#define BNXT_GEN	5
#include "nic_bnxt.h"

#define GDA_IPSEC
#define GDA_FN(f)	cfn_##f##_bnxt_p5_ipsec
#include "gda_engine.h"
