// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * BPF_DIV and BPF_MOD, every one of them: the GPU has no divide, so clang
 * writes the long way for each, and the JIT calls it.
 *
 * As the BPF instruction set has them: a division by zero is zero and a
 * remainder by zero is the dividend; a signed division by -1 is the
 * dividend negated and the remainder zero, whatever the dividend; the 32-bit
 * forms work on the low halves and zero the high half of the result.
 */
#include <stdint.h>

uint64_t cfn_div64(uint64_t a, uint64_t b)
{
	return b ? a / b : 0;
}

uint64_t cfn_mod64(uint64_t a, uint64_t b)
{
	return b ? a % b : a;
}

uint64_t cfn_div32(uint64_t a, uint64_t b)
{
	return (uint32_t)b ? (uint32_t)a / (uint32_t)b : 0;
}

uint64_t cfn_mod32(uint64_t a, uint64_t b)
{
	return (uint32_t)b ? (uint32_t)a % (uint32_t)b : (uint32_t)a;
}

uint64_t cfn_sdiv64(uint64_t a, uint64_t b)
{
	if (!b)
		return 0;
	if ((int64_t)b == -1)
		return -a;
	return (int64_t)a / (int64_t)b;
}

uint64_t cfn_smod64(uint64_t a, uint64_t b)
{
	if (!b)
		return a;
	if ((int64_t)b == -1)
		return 0;
	return (int64_t)a % (int64_t)b;
}

uint64_t cfn_sdiv32(uint64_t a, uint64_t b)
{
	if (!(uint32_t)b)
		return 0;
	if ((int32_t)b == -1)
		return (uint32_t)-(uint32_t)a;
	return (uint32_t)((int32_t)a / (int32_t)b);
}

uint64_t cfn_smod32(uint64_t a, uint64_t b)
{
	if (!(uint32_t)b)
		return (uint32_t)a;
	if ((int32_t)b == -1)
		return 0;
	return (uint32_t)((int32_t)a % (int32_t)b);
}
