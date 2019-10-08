/*
 * Phoenix-RTOS
 *
 * ext2
 *
 * file.h
 *
 * Copyright 2017 Phoenix Systems
 * Author: Kamil Amanowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _FILE_H_
#define _FILE_H_ /* file.h */

#include <stdint.h>

extern int ext2_read(oid_t *oid, offs_t offs, char *data, unsigned int len);


extern int ext2_read_locked(oid_t *oid, offs_t offs, char *data, uint32_t len);


extern int ext2_write(oid_t *oid, offs_t offs, char *data, unsigned int len);


extern int ext2_write_locked(oid_t *oid, offs_t offs, char *data, uint32_t len);


extern int ext2_truncate(oid_t *oid, unsigned int size);


#endif /* file.h */
