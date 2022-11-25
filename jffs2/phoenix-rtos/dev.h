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


#ifndef _OS_PHOENIX_DEV_H_
#define _OS_PHOENIX_DEV_H_

#include <sys/stat.h>
#include <sys/rb.h>

typedef struct _jffs2_dev_t {
	rbnode_t linkage_oid;
	rbnode_t linkage_ino;
	unsigned long ino;
	oid_t dev;
} jffs2_dev_t;


int old_valid_dev(dev_t dev);

int old_encode_dev(dev_t dev);

int new_encode_dev(dev_t dev);

dev_t old_decode_dev(uint16_t val);

dev_t new_decode_dev(uint32_t dev);


extern jffs2_dev_t *dev_find_oid(void *ptr, oid_t *oid, unsigned long ino, int create);


extern jffs2_dev_t *dev_find_ino(void *ptr, unsigned long ino);


extern void dev_destroy(void *ptr, jffs2_dev_t *dev);


extern void dev_done(void *ptr);


extern void dev_init(void **ptr);


#endif /* _OS_PHOENIX_DEV_H_ */
