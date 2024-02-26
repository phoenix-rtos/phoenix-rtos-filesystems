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

#ifndef METERFS_CRYPT_H_
#define METERFS_CRYPT_H_

#include <stdint.h>
#include <stddef.h>

#include "files.h"


void meterfs_encrypt(void *buff, size_t bufflen, const uint8_t *key, const file_t *f, const entry_t *e);


#endif /* METERFS_CRYPT_H_ */
