// SPDX-License-Identifier: GPL-2.0-or-later
/* The GDA engine on ConnectX-5 on: a cyclic RQ: KNOD_PERSIST_FMT_MLX5_CX5. */
#include "gda.h"
#include "nic_mlx5.h"

#define GDA_FN(f)	cfn_##f##_mlx5_cx5
#include "gda_engine.h"
