/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * RDNA3 renamed these; the encodings are the same instruction.  The
 * shader is written in the older spelling because that is the one the
 * generation it was first built for used.
 */

#ifndef IPSEC_RDNA_INC
#define IPSEC_RDNA_INC

#if KNOD_ISA >= 11
# define DS_READ_B32              ds_load_b32
# define DS_READ_B128             ds_load_b128
# define DS_WRITE_B32             ds_store_b32
# define DS_WRITE_B128            ds_store_b128
# define GLOBAL_STORE_SHORT       global_store_b16
# define GLOBAL_STORE_DWORD       global_store_b32
# define GLOBAL_STORE_DWORDX2     global_store_b64
# define GLOBAL_STORE_DWORDX4     global_store_b128
# define GLOBAL_LOAD_USHORT       global_load_u16
# define GLOBAL_LOAD_DWORD        global_load_b32
# define GLOBAL_LOAD_DWORDX2      global_load_b64
# define GLOBAL_LOAD_DWORDX4      global_load_b128
# define GLOBAL_ATOMIC_ADD        global_atomic_add_u32
# define S_LOAD_DWORDX2           s_load_b64
# define S_LOAD_DWORDX4           s_load_b128
#else
# define DS_READ_B32              ds_read_b32
# define DS_READ_B128             ds_read_b128
# define DS_WRITE_B32             ds_write_b32
# define DS_WRITE_B128            ds_write_b128
# define GLOBAL_STORE_SHORT       global_store_short
# define GLOBAL_STORE_DWORD       global_store_dword
# define GLOBAL_STORE_DWORDX2     global_store_dwordx2
# define GLOBAL_STORE_DWORDX4     global_store_dwordx4
# define GLOBAL_LOAD_USHORT       global_load_ushort
# define GLOBAL_LOAD_DWORD        global_load_dword
# define GLOBAL_LOAD_DWORDX2      global_load_dwordx2
# define GLOBAL_LOAD_DWORDX4      global_load_dwordx4
# define GLOBAL_ATOMIC_ADD        global_atomic_add
# define S_LOAD_DWORDX2           s_load_dwordx2
# define S_LOAD_DWORDX4           s_load_dwordx4
#endif

/*
 * The ESP header sits at 34 or 54, both two past a dword boundary, so
 * these reads take the dword that encloses it and shift the wanted
 * bytes out.  RDNA2's load unit drops the low two bits of the address
 * by itself and lands on that dword; RDNA3 does not, and has to be
 * told.
 */
#if KNOD_ISA >= 11
# define ESP_BIAS	(-2)
#else
# define ESP_BIAS	0
#endif

#endif /* IPSEC_RDNA_INC */
