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
#include <dirent.h>
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <sys/file.h>
#include <sys/list.h>
#include <sys/rb.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>

typedef int64_t ph_lfs_time_t;

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

/* TODO: this implementation doesn't extend sign */
static inline long long ph_lfs_attrFromLE(const uint8_t attr[8], size_t attrSize)
{
	long long result = 0;
#if defined(PH_LFS_LE_NOOP)
	memcpy(&result, attr, attrSize);
#else
	while (attrSize > 0) {
		attrSize--;
		result <<= 8;
		result |= attr[attrSize];
	}
#endif
	return result;
}

static inline void ph_lfs_attrToLE(long long attr, uint8_t result[8], size_t attrSize)
{
#if defined(PH_LFS_LE_NOOP)
	memcpy(result, &attr, attrSize);
#else
	for (size_t i = 0; i < attrSize; i++) {
		result[i] = attr & 0xff;
		attr >>= 8;
	}
#endif
}

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

static inline uint16_t ph_lfs_fromLE16(uint16_t a)
{
#if defined(PH_LFS_LE_NOOP)
	return a;
#elif defined(PH_LFS_LE_INTRINSIC)
	return __builtin_bswap16(a);
#else
	return (((uint8_t *)&a)[0] << 0) | (((uint8_t *)&a)[1] << 8);
#endif
}

static inline uint16_t ph_lfs_toLE16(uint16_t a)
{
	return ph_lfs_fromLE16(a);
}

#endif /* _PH_LFS_UTIL_H_ */
