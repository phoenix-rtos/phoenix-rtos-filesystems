/*
 * Phoenix-RTOS
 *
 * Implementation of Phoenix RTOS filesystem API for littlefs.
 *
 * Copyright 2024 Phoenix Systems
 * Author: Jacek Maksymowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _PH_LFS_UTIL_H_
#define _PH_LFS_UTIL_H_

#include <byteswap.h>

#if (defined(BYTE_ORDER) && defined(ORDER_LITTLE_ENDIAN) && BYTE_ORDER == ORDER_LITTLE_ENDIAN) || \
	(defined(__BYTE_ORDER) && defined(__ORDER_LITTLE_ENDIAN) && __BYTE_ORDER == __ORDER_LITTLE_ENDIAN) || \
	(defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#define PH_LFS_LE_NOOP
#elif !defined(LFS_NO_INTRINSICS) && \
	((defined(BYTE_ORDER) && defined(ORDER_BIG_ENDIAN) && BYTE_ORDER == ORDER_BIG_ENDIAN) || \
		(defined(__BYTE_ORDER) && defined(__ORDER_BIG_ENDIAN) && __BYTE_ORDER == __ORDER_BIG_ENDIAN) || \
		(defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__))
#define PH_LFS_LE_INTRINSIC
#endif

static inline uint64_t ph_lfs_fromLE64(uint64_t a)
{
#if defined(PH_LFS_LE_NOOP)
	return a;
#elif defined(PH_LFS_LE_INTRINSIC)
	return __builtin_bswap64(a);
#else
	return ((uint64_t)((uint8_t *)&a)[0] << 0) |
		((uint64_t)((uint8_t *)&a)[1] << 8) |
		((uint64_t)((uint8_t *)&a)[2] << 16) |
		((uint64_t)((uint8_t *)&a)[3] << 24) |
		((uint64_t)((uint8_t *)&a)[4] << 32) |
		((uint64_t)((uint8_t *)&a)[5] << 40) |
		((uint64_t)((uint8_t *)&a)[6] << 48) |
		((uint64_t)((uint8_t *)&a)[7] << 56);
#endif
}

static inline uint64_t ph_lfs_toLE64(uint64_t a)
{
	return ph_lfs_fromLE64(a);
}

#endif /* _PH_LFS_UTIL_H_ */
