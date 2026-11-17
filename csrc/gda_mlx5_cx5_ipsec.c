// SPDX-License-Identifier: GPL-2.0-or-later
/* KNOD_PERSIST_FMT_MLX5_CX5's crypto pool: its queues' IPsec jobs. */
#include "gda.h"
#include "nic_mlx5.h"

#define GDA_IPSEC
#define GDA_FN(f)	cfn_##f##_mlx5_cx5_ipsec
#include "gda_engine.h"
