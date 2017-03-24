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


#define PCACHE_CNT_MAX 30
#define PCACHE_SIZE_PAGE 100
#define PCACHE_BUCKETS 1024


extern void pcache_init(int dev);


extern int pcache_read(unsigned long off, unsigned int size, char *buff);


#endif
