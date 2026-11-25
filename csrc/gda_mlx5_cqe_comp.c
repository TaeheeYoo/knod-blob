// SPDX-License-Identifier: GPL-2.0-or-later
/* The GDA engine on ConnectX-6 Dx on, its RX CQ compressed:
 * KNOD_PERSIST_FMT_MLX5_CQE_COMP.
 */
#include "gda.h"
#define MLX5_CQE_COMP	1
#include "nic_mlx5.h"

#define GDA_FN(f)	cfn_##f##_mlx5_cqe_comp
#include "gda_engine.h"
