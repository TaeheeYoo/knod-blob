// SPDX-License-Identifier: GPL-2.0-or-later
/* The GDA engine on ConnectX-5 on, XDP_TX in multi-packet WQEs:
 * KNOD_PERSIST_FMT_MLX5_MPW.
 */
#include "gda.h"
#define MLX5_TX_MPW	1
#include "nic_mlx5.h"

#define GDA_FN(f)	cfn_##f##_mlx5_mpw
#include "gda_engine.h"
