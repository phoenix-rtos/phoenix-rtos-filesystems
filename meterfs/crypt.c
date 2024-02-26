/*
 * Phoenix-RTOS
 *
 * Meterfs cryptography
 *
 * Copyright 2024 Phoenix Systems
 * Author: Aleksander Kaminski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <tinyaes/aes.h>
#include "crypt.h"


static uint8_t *meterfs_ivSerializeU32(uint8_t *buff, uint32_t d)
{
	buff[0] = (d >> 0) & 0xff;
	buff[1] = (d >> 8) & 0xff;
	buff[2] = (d >> 16) & 0xff;
	buff[3] = (d >> 24) & 0xff;

	return &buff[4];
}


static inline void meterfs_constructIV(uint8_t *iv, const file_t *f, const entry_t *e)
{
	uint8_t *tbuff = meterfs_ivSerializeU32(iv, e->id.no);
	tbuff = meterfs_ivSerializeU32(tbuff, f->header.sector);
	tbuff = meterfs_ivSerializeU32(tbuff, f->header.uid);
	uint32_t t = 0;
	meterfs_ivSerializeU32(tbuff, t);
}


void meterfs_encrypt(void *buff, size_t bufflen, const uint8_t *key, const file_t *f, const entry_t *e)
{
	struct AES_ctx ctx;
	uint8_t iv[AES_BLOCKLEN];

	meterfs_constructIV(iv, f, e);
	AES_init_ctx_iv(&ctx, key, iv);
	AES_CTR_xcrypt_buffer(&ctx, buff, bufflen);
}
