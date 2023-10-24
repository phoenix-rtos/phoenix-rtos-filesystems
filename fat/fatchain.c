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


#include "fatchain.h"

#include <string.h>

#include "fatdev.h"

#define RSVD_ENTRIES 2

int fatchain_getOne(fat_info_t *info, fat_cluster_t cluster, fat_cluster_t *next)
{
	int ret;
	size_t byteOff;
	fat_cluster_t readNext;

	if (cluster >= info->clusters) {
		return -EINVAL;
	}

	if (info->type == FAT32) {
		byteOff = cluster * 4;
	}
	else if (info->type == FAT16) {
		byteOff = cluster * 2;
	}
	else { /* FAT12 */
		byteOff = (cluster * 3) / 2;
	}

	ret = fatdev_read(info, info->fatoffBytes + byteOff, sizeof(readNext), &readNext);
	if (ret < 0) {
		return ret;
	}

	if (info->type == FAT32) {
		readNext &= 0xfffffff;
		if (readNext >= 0xffffff8) {
			readNext = FAT_EOF;
		}
	}
	else if (info->type == FAT16) {
		readNext &= 0xffff;
		if (readNext >= 0xfff8) {
			readNext = FAT_EOF;
		}
	}
	else { /* FAT12 */
		readNext >>= ((cluster % 2) == 1) ? 4 : 0;
		readNext &= 0xfff;
		if (readNext >= 0xff8) {
			readNext = FAT_EOF;
		}
	}

	*next = readNext;
	return EOK;
}


fat_cluster_t fatchain_scanFreeSpace(fat_info_t *info)
{
	fat_cluster_t freeClusters = 0;
	if (info->type == FAT12) {
		/* Not very efficient but for FAT12 it should be good enough (only 4085 clusters max) */
		for (fat_cluster_t i = 0; i < info->dataClusters + RSVD_ENTRIES; i++) {
			fat_cluster_t result;
			if (fatchain_getOne(info, i, &result) < 0) {
				return 0;
			}

			freeClusters += (result == 0) ? 1 : 0;
		}

		return freeClusters;
	}

	uint32_t buff[16];
	/* The first two entries in FAT are reserved, but they are always non-zero */
	offs_t byteOff = info->fatoffBytes;
	offs_t byteEnd = byteOff + (info->dataClusters + RSVD_ENTRIES) * ((info->type == FAT32) ? 4 : 2);

	while (byteOff < byteEnd) {
		size_t toRead = min(sizeof(buff), byteEnd - byteOff);
		if (toRead != sizeof(buff)) {
			memset(buff, 0xff, sizeof(buff));
		}

		if (fatdev_read(info, byteOff, toRead, buff) < 0) {
			return 0;
		}

		for (int i = 0; i < (sizeof(buff) / sizeof(buff[0])); i++) {
			if (buff[i] == 0) {
				freeClusters += (info->type == FAT32) ? 1 : 2;
			}
			else if (info->type == FAT16) {
				freeClusters += ((buff[i] & 0xffff) == 0) ? 1 : 0;
				freeClusters += ((buff[i] >> 16) == 0) ? 1 : 0;
			}
		}

		byteOff += toRead;
	}

	return freeClusters;
}


static void setNext(fat_info_t *info, fatchain_cache_t *c, size_t i)
{
	c->areas[i].start = info->dataoff + (c->nextAfterAreas - 2) * info->bsbpb.BPB_SecPerClus;
	c->areas[i].size = info->bsbpb.BPB_SecPerClus;

	if (i < (FAT_CHAIN_AREAS - 1)) {
		c->areas[i + 1].start = 0;
	}
}


int fatchain_parseNext(fat_info_t *info, fatchain_cache_t *c, fat_sector_t skip)
{
	c->areas[0].start = 0;

	if (c->nextAfterAreas >= info->clusters) {
		/* Either invalid input or reached end of chain */
		return -EINVAL;
	}

	if (c->nextAfterAreas == ROOT_DIR_CLUSTER) {
		/* Trying to read root directory cluster - special treatment needed */
		if (info->type == FAT32) {
			c->nextAfterAreas = info->bsbpb.fat32.BPB_RootClus;
		}
		else {
			/* On FAT12 or FAT16 root directory is always contiguous and fixed size */
			c->nextAfterAreas = FAT_EOF;
			fat_sector_t rootDirSize = info->dataoff - info->rootoff;
			if (skip >= rootDirSize) {
				c->areasOffset = 0;
				c->areasLength = 0;
			}
			else {
				c->areas[0].start = info->rootoff + skip;
				c->areas[0].size = rootDirSize - skip;
				c->areas[1].start = 0;
				c->areasOffset = skip;
				c->areasLength = c->areas[0].size;
			}

			return EOK;
		}
	}

	c->areasOffset += c->areasLength + skip;
	c->areasLength = 0;
	setNext(info, c, 0);
	unsigned int i = 0;
	for (;;) {
		unsigned int next;
		if (fatchain_getOne(info, c->nextAfterAreas, &next) < 0) {
			return -EINVAL;
		}

		bool mergeIntoCurrent = (next == (c->nextAfterAreas + 1));
		c->nextAfterAreas = next;
		if (mergeIntoCurrent) {
			c->areas[i].size += info->bsbpb.BPB_SecPerClus;
		}
		else {
			if (skip > 0) {
				if (skip < c->areas[i].size) {
					c->areas[i].size -= skip;
					c->areas[i].start += skip;
					skip = 0;
					c->areasLength += c->areas[i].size;
					i++;
				}
				else {
					skip -= c->areas[i].size;
				}
			}
			else {
				c->areasLength += c->areas[i].size;
				i++;
			}


			if (i == FAT_CHAIN_AREAS) {
				break;
			}
			else if (next == FAT_EOF) {
				c->areas[i].start = 0;
				break;
			}
			else if (next < RSVD_ENTRIES) {
				/* This may indicate FAT is corrupted */
				return -EINVAL;
			}
			else {
				setNext(info, c, i);
			}
		}
	}

	return EOK;
}
