/*
 * Phoenix-RTOS
 *
 * Misc. Page cache
 *
 * Device support
 *
 * Copyright 2017 Phoenix Systems
 * Author: Katarzyna Baranowska
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _MISC_PCACHE_H_
#define _MISC_PCACHE_H_


#include "pcache.h"
#include "types.h"


#define PCACHE_CNT_MAX 30
#define PCACHE_SIZE_PAGE 100
#define PCACHE_BUCKETS 1024


struct _pcache_list_t {
	struct _pcache_list_t *n;
	struct _pcache_list_t *p;
};
typedef struct _pcache_list_t pcache_list_t;


struct _pcache_t {
	mut_t m;
	pcache_list_t b[PCACHE_BUCKETS];
	pcache_list_t f;
	pcache_list_t e;
	int cnt;
	int max_cnt;
	unsigned pagesize;
	void *dev;
};
typedef struct _pcache_t pcache_t;


extern int pcache_init(pcache_t *pcache, unsigned size, void *dev, unsigned pagesize);


extern int pcache_resize(pcache_t *pcache, unsigned size, void **dev);


extern int pcache_read(pcache_t *pcache, offs_t off, unsigned int size, char *buff);


extern int pcache_devread(void *dev, offs_t off, unsigned int size, char *buff);

#endif
