// SPDX-License-Identifier: GPL-2.0-or-later
//
// The GDA engines, one per NIC format: knod_gda_engine_f<KNOD_PERSIST_FMT_*>.

#include "gda_engine.inc"

#define MLX5_CX5	GDA_CAT(knod_gda_engine_f, KNOD_PERSIST_FMT_MLX5_CX5)

	GDA_ENGINE MLX5_CX5, mlx5_cx5, 0
	CFN_BODY_gda_init_mlx5_cx5
	CFN_BODY_gda_round_begin_mlx5_cx5
	CFN_BODY_gda_round_end_mlx5_cx5
	GDA_ENGINE_END MLX5_CX5
