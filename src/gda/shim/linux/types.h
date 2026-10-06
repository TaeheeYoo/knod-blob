/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The kernel types the uapi headers spell, for building them freestanding
 * for the GPU.  Nothing else from the kernel is available there.
 */
#ifndef _KNOD_BLOB_SHIM_LINUX_TYPES_H
#define _KNOD_BLOB_SHIM_LINUX_TYPES_H

typedef unsigned char		__u8;
typedef unsigned short		__u16;
typedef unsigned int		__u32;
typedef unsigned long long	__u64;
typedef int			__s32;
typedef __u16			__le16;
typedef __u32			__le32;
typedef __u64			__le64;
typedef __u16			__be16;
typedef __u32			__be32;
typedef __u64			__be64;

typedef __u8			u8;
typedef __u16			u16;
typedef __u32			u32;
typedef __u64			u64;
typedef __s32			s32;

#endif
