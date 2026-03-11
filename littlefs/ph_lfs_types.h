/*
 * Phoenix-RTOS
 *
 * LittleFS implementation for Phoenix-RTOS - definitions of attribute types
 * and corresponding utility functions.
 *
 * Copyright 2024 Phoenix Systems
 * Author: Jacek Maksymowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _PH_LFS_TYPES_H_
#define _PH_LFS_TYPES_H_

#include <byteswap.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>


/* Note: in the code below we assume compiler intrinsics will be available. */
#if (defined(BYTE_ORDER) && defined(ORDER_LITTLE_ENDIAN) && BYTE_ORDER == ORDER_LITTLE_ENDIAN) || \
		(defined(__BYTE_ORDER) && defined(__ORDER_LITTLE_ENDIAN) && __BYTE_ORDER == __ORDER_LITTLE_ENDIAN) || \
		(defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#define PH_LFS_LE_NOOP 1
#else
#define PH_LFS_LE_NOOP 0
#endif

/* Phoenix-RTOS-specific attribute types */

/* Phoenix ID - unique IDs given to each file or directory to implement an inode-like API */
#define LFS_TYPE_PHID_START 0xfc /* Note: this must be bitmask leading with 1 bits and trailing with 0 bits */
#define LFS_TYPE_PHID_MASK  (0x700 | LFS_TYPE_PHID_START)
#define LFS_TYPE_PHID_ANY   (LFS_TYPE_USERATTR + LFS_TYPE_PHID_START)
#define LFS_TYPE_PHID_REG   (LFS_TYPE_USERATTR + LFS_TYPE_PHID_START + 0) /* File ID for a regular file */
#define LFS_TYPE_PHID_DIR   (LFS_TYPE_USERATTR + LFS_TYPE_PHID_START + 1) /* File ID for a directory */
/* The remaining 2 attribute types are reserved for future use */

#define LFS_INVALID_PHID 0            /* Value to indicate Phoenix ID is invalid */
#define LFS_ROOT_PHID    1            /* Phoenix ID representing root dir */
#define ID_SIZE          sizeof(id_t) /* Size of Phoenix IDs stored on disk */

/* POSIX-like attributes. Attribute IDs are allocated in descending order below LFS_TYPE_PHID_START. */
#define LFS_TYPE_PH_ATTR_LAST   (LFS_TYPE_USERATTR + LFS_TYPE_PHID_START - 1)
#define LFS_TYPE_PH_ATTR_NUM(x) (LFS_TYPE_PH_ATTR_LAST - x)
#define LFS_TYPE_PH_ATTR_ATIME  LFS_TYPE_PH_ATTR_NUM(0) /* Accessed time */
#define LFS_TYPE_PH_ATTR_CTIME  LFS_TYPE_PH_ATTR_NUM(1) /* Created time */
#define LFS_TYPE_PH_ATTR_MTIME  LFS_TYPE_PH_ATTR_NUM(2) /* Modified time */
#define LFS_TYPE_PH_ATTR_UID    LFS_TYPE_PH_ATTR_NUM(3) /* User ID */
#define LFS_TYPE_PH_ATTR_GID    LFS_TYPE_PH_ATTR_NUM(4) /* Group ID */
#define LFS_TYPE_PH_ATTR_MODE   LFS_TYPE_PH_ATTR_NUM(5) /* File mode */

typedef int64_t ph_lfs_time_t;


static inline id_t ph_lfs_idFromLE(id_t x)
{
#if PH_LFS_LE_NOOP == 1
	return x;
#else
	if (sizeof(x) == 8) {
		return __builtin_bswap64(x);
	}
	else if (sizeof(x) == 4) {
		return __builtin_bswap32(x);
	}
	else if (sizeof(x) == 2) {
		return __builtin_bswap16(x);
	}

	return x;
#endif
}


static inline id_t ph_lfs_idToLE(id_t x)
{
	return ph_lfs_idFromLE(x);
}


/* TODO: this implementation doesn't extend sign */
static inline long long ph_lfs_attrFromLE(const uint8_t attr[8], unsigned int attrSize)
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


static inline void ph_lfs_attrToLE(long long attr, uint8_t result[8], unsigned int attrSize)
{
#if defined(PH_LFS_LE_NOOP)
	memcpy(result, &attr, attrSize);
#else
	for (unsigned int i = 0; i < attrSize; i++) {
		result[i] = attr & 0xff;
		attr >>= 8;
	}
#endif
}


#endif /* _PH_LFS_UTIL_H_ */
