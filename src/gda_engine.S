// SPDX-License-Identifier: GPL-2.0-or-later
//
// The GDA engines, one per NIC format: knod_gda_engine_f<KNOD_PERSIST_FMT_*>,
// and those that decrypt what they pass: knod_gda_engine_ipsec_f<...>.

#include "gda_engine.inc"

#define MLX5_CX5	GDA_CAT(knod_gda_engine_f, KNOD_PERSIST_FMT_MLX5_CX5)
#define BNXT_P5		GDA_CAT(knod_gda_engine_f, KNOD_PERSIST_FMT_BNXT_P5)
#define BNXT_P7		GDA_CAT(knod_gda_engine_f, KNOD_PERSIST_FMT_BNXT_P7)
#define IPSEC_MLX5_CX5	GDA_CAT(knod_gda_engine_ipsec_f, KNOD_PERSIST_FMT_MLX5_CX5)
#define IPSEC_BNXT_P5	GDA_CAT(knod_gda_engine_ipsec_f, KNOD_PERSIST_FMT_BNXT_P5)
#define IPSEC_BNXT_P7	GDA_CAT(knod_gda_engine_ipsec_f, KNOD_PERSIST_FMT_BNXT_P7)

	GDA_ENGINE MLX5_CX5, mlx5_cx5, 0
	CFN_BODY_gda_init_mlx5_cx5
	CFN_BODY_gda_round_begin_mlx5_cx5
	CFN_BODY_gda_round_end_mlx5_cx5
	CFN_BODY_gda_host_tx_mlx5_cx5
	GDA_ENGINE_END MLX5_CX5

	GDA_ENGINE BNXT_P5, bnxt_p5, 0
	CFN_BODY_gda_init_bnxt_p5
	CFN_BODY_gda_round_begin_bnxt_p5
	CFN_BODY_gda_round_end_bnxt_p5
	CFN_BODY_gda_host_tx_bnxt_p5
	GDA_ENGINE_END BNXT_P5

	GDA_ENGINE BNXT_P7, bnxt_p7, 0
	CFN_BODY_gda_init_bnxt_p7
	CFN_BODY_gda_round_begin_bnxt_p7
	CFN_BODY_gda_round_end_bnxt_p7
	CFN_BODY_gda_host_tx_bnxt_p7
	GDA_ENGINE_END BNXT_P7

	GDA_ENGINE IPSEC_MLX5_CX5, mlx5_cx5_ipsec, 0, 1
	CFN_BODY_gda_init_mlx5_cx5_ipsec
	CFN_BODY_gda_round_begin_mlx5_cx5_ipsec
	CFN_BODY_gda_ipsec_mlx5_cx5_ipsec
	CFN_BODY_gda_round_end_mlx5_cx5_ipsec
	CFN_BODY_gda_host_gcm_mlx5_cx5_ipsec
	CFN_BODY_gda_host_tx_mlx5_cx5_ipsec
	GDA_ENGINE_END IPSEC_MLX5_CX5

	GDA_ENGINE IPSEC_BNXT_P5, bnxt_p5_ipsec, 0, 1
	CFN_BODY_gda_init_bnxt_p5_ipsec
	CFN_BODY_gda_round_begin_bnxt_p5_ipsec
	CFN_BODY_gda_ipsec_bnxt_p5_ipsec
	CFN_BODY_gda_round_end_bnxt_p5_ipsec
	CFN_BODY_gda_host_gcm_bnxt_p5_ipsec
	CFN_BODY_gda_host_tx_bnxt_p5_ipsec
	GDA_ENGINE_END IPSEC_BNXT_P5

	GDA_ENGINE IPSEC_BNXT_P7, bnxt_p7_ipsec, 0, 1
	CFN_BODY_gda_init_bnxt_p7_ipsec
	CFN_BODY_gda_round_begin_bnxt_p7_ipsec
	CFN_BODY_gda_ipsec_bnxt_p7_ipsec
	CFN_BODY_gda_round_end_bnxt_p7_ipsec
	CFN_BODY_gda_host_gcm_bnxt_p7_ipsec
	CFN_BODY_gda_host_tx_bnxt_p7_ipsec
	GDA_ENGINE_END IPSEC_BNXT_P7
