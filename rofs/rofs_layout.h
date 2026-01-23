/*
 * Phoenix-RTOS
 *
 * ROFS - Read Only File System
 *
 * Filesystem layout
 *
 * Copyright 2026 Phoenix Systems
 * Author: Adam Greloch
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef ROFS_FORMAT_H
#define ROFS_FORMAT_H

#include <stdint.h>

#define ROFS_SIGNATURE      "ROFS"
#define ROFS_HDR_SIGNATURE  0
#define ROFS_HDR_CHECKSUM   4
#define ROFS_HDR_IMAGESIZE  8
#define ROFS_HDR_INDEXOFFS  16
#define ROFS_HDR_NODECOUNT  24
#define ROFS_HDR_ENCRYPTION 32
#define ROFS_HDR_CRYPT_SIG  34 /* let CRYPT_SIG to be at least 64-byte long to allow for future signatures schemes (e.g. ed25519) */
#define ROFS_HEADER_SIZE    128

_Static_assert(AES_BLOCKLEN <= ROFS_HEADER_SIZE - ROFS_HDR_CRYPT_SIG, "AES_MAC does not fit into the rofs header");

/* clang-format off */
enum { rofs_encryption_none, rofs_encryption_aes };
/* clang-format on */

struct rofs_node {
	uint64_t timestamp;
	uint32_t parentId;
	uint32_t id;
	uint32_t mode;
	uint32_t reserved0;
	int32_t uid;
	int32_t gid;
	uint32_t offset;
	uint32_t reserved1;
	uint32_t size;
	uint32_t reserved2;
	char name[207];
	uint8_t zero;
} __attribute__((packed)); /* 256 bytes */

#endif
