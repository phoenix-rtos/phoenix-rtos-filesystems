/*
 * Phoenix-RTOS
 *
 * FAT filesystem driver
 *
 * FAT cluster chain reading and parsing
 *
 * Copyright 2012, 2023 Phoenix Systems
 * Author: Pawel Pisarczyk, Jacek Maksymowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */


#ifndef _FATCHAIN_H_
#define _FATCHAIN_H_

#include "fatio.h"


static inline void fatchain_initCache(fatchain_cache_t *c, uint32_t cluster)
{
	c->chainStart = cluster;
	c->nextAfterAreas = cluster;
	c->areasOffset = 0;
	c->areasLength = 0;
}


extern fat_cluster_t fatchain_scanFreeSpace(fat_info_t *info);


extern int fatchain_getOne(fat_info_t *info, fat_cluster_t cluster, fat_cluster_t *next);


extern int fatchain_parseNext(fat_info_t *info, fatchain_cache_t *c, fat_sector_t skip);


#endif /* _FATCHAIN_H_ */
