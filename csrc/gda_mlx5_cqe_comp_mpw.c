// SPDX-License-Identifier: GPL-2.0-or-later
/* The GDA engine on ConnectX-5 on, its RX CQ compressed and XDP_TX in multi-packet WQEs:
 * KNOD_PERSIST_FMT_MLX5_CQE_COMP_MPW.
 */
#include "gda.h"
#define MLX5_CQE_COMP	1
#define MLX5_TX_MPW	1
#include "nic_mlx5.h"

#define GDA_FN(f)	cfn_##f##_mlx5_cqe_comp_mpw
#include "gda_engine.h"
