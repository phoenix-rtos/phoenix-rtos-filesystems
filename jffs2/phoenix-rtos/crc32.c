/*
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * Jffs2 FileSystem - system specific information.
 *
 * Copyright 2018 Phoenix Systems
 * Author: Kamil Amanowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include "../phoenix-rtos.h"
#include "crc32.h"

/* taken from Linux kernel */
uint32_t crc32(uint32_t crc, void *p, size_t len)
{
	int i;
	const uint8_t *buf = p;
	while (len--) {
		crc = (crc ^ (*buf++));
		for (i = 0; i < 8; i++) {
			crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
		}
	}

	return crc;
}
