// SPDX-License-Identifier: GPL-2.0-or-later
/* KNOD_PERSIST_FMT_MLX5_CQE_COMP_MPW's crypto pool: its queues' IPsec jobs. */
#include "gda.h"
#define MLX5_CQE_COMP	1
#define MLX5_TX_MPW	1
#include "nic_mlx5.h"

#define GDA_IPSEC
#define GDA_FN(f)	cfn_##f##_mlx5_cqe_comp_mpw_ipsec
#include "gda_engine.h"
