/*
 * Phoenix-RTOS
 *
 * Misc. utilities
 *
 * FAT filesystem implementation
 *
 * Copyright 2012 Phoenix Systems
 * Author: Pawel Pisarczyk
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "fat.h"
#include "fatdev.h"
#include "fatfat.h"


#define min(a, b) ({__typeof__(a) _a = (a); \
	__typeof__(b) _b = (b); \
	_a < _b ? _a : _b;})


int fatio_readsuper(void *opt, fat_info_t **out)
{
	fat_info_t *info;

	if ((info = (fat_info_t *)malloc(sizeof(fat_info_t))) == NULL) {
		free(info);
		return ERR_MEM;
	}

	info->off = ((fat_opt_t *)opt)->off;
	info->dev = ((fat_opt_t *)opt)->dev;

	if (fatdev_read(info, 0, 1, (char *)&info->bsbpb) < 0) {
		free(info);
		return ERR_PROTO;
	}

	info->fatoff = info->bsbpb.BPB_RsvdSecCnt;
	info->fatend = info->fatoff;
	info->fatend += info->bsbpb.BPB_FATSz16 ? info->bsbpb.BPB_FATSz16 : info->bsbpb.fat32.BPB_FATSz32;
	info->rootoff = info->fatend + (info->fatend - info->fatoff) * (info->bsbpb.BPB_NumFATs - 1);
	info->dataoff = info->rootoff + (info->bsbpb.BPB_RootEntCnt << 5) / info->bsbpb.BPB_BytesPerSec;
	info->dataend = info->dataoff;
	info->dataend += info->bsbpb.BPB_TotSecS ? info->bsbpb.BPB_TotSecS : info->bsbpb.BPB_TotSecL;

	info->end = info->off + info->dataend;

	info->clusters = (info->dataend - info->dataoff) / info->bsbpb.BPB_SecPerClus;

	/* Check FAT type */
	info->type = FAT16; // TODO determine FAT width from https://jdebp.eu/FGA/determining-fat-widths.html
// 	if (info->clusters < 4085)
// 		info->type = FAT12;
// 	else if (info->clusters < 65525)
// 		info->type = FAT16;

	/* Read FAT32 FSInfo */
	info->fsinfo = NULL;
	if (info->type == FAT32) {
		if ((info->fsinfo = malloc(sizeof(fat_fsinfo_t))) == NULL) {
			free(info);
			return ERR_MEM;
		}
	}

	*out = (void *)info;	
	return ERR_NONE;
}


// static int fatio_read(fat_info_t *info, fatfat_chain_t *c, unsigned long offset, unsigned int size, char *buff)
// {
// 	unsigned long areaoff;
// 	int i;
// 
// 	for (i = 0; (i < SIZE_CHAIN_AREAS) && (c->area[i]->size <= cnt); i++)
// 		cnt -= c->area[i]->size;
// 
// 	if (i == SIZE_CHAIN_AREAS)
// }


static int fatio_cmpname(const char *path, fat_dirent_t *d)
{
	int n, e, s;
	
	for (s = 7; (s > 0) && (d->name[s] == ' '); s--);
	// TODO what if d->name[s] == ' ' now
	for (n = 0; n <= s; n++) {
		if (path[n] != d->name[n])
			return 0;
	}
	for (s = 2; (s > 0) && (d->ext[s] == ' '); s--);
	e = 0;
	if (d->ext[s] != ' ') {
		if (path[n] != '.')
			return 0;
		n++;
		for (e = 0; e <= s; e++) {
			if (path[e + n] != d->ext[e])
				return 0;
		}
	}
	if (path[n + e] == '/')
		return n + e;
	if (path[n + e] == 0)
		return n + e;
	return 0;
}


static int fatio_lookupone(fat_info_t *info, const char *path, fat_dirent_t *d)
{
	fatfat_chain_t c;
	int i, j, plen;
	u8 buff[SIZE_SECTOR * 32];
	fat_dirent_t *tmpd;

	FATDEBUG("fatio_lookupone path %s\n", path);
	c.start = d->cluster;

	for (;;) {
		if (fatfat_lookup(info, &c) < 0)
			return ERR_NOENT;

		for (i = 0; i < SIZE_CHAIN_AREAS; i++) {
			if (!c.areas[i].start)
				return ERR_NOENT;

			FATDEBUG("fatio_lookupone reading area %d: start %d size %d\n", i, c.areas[i].start, c.areas[i].size);

			for (j = 0; j < c.areas[i].size; j += sizeof(buff) / SIZE_SECTOR) {
				if (fatdev_read(info, c.areas[i].start + j, min(c.areas[i].size - j, sizeof(buff) / SIZE_SECTOR), (char *)buff))
					return ERR_PROTO;

				for (tmpd = (fat_dirent_t *) buff; (u8 *) tmpd < min(sizeof(buff), (c.areas[i].size - j) * SIZE_SECTOR) + buff; tmpd++) {
					if (tmpd->attr == 0x0F) /* long file name (LFN) data */
						continue;
					if (tmpd->name[0] == 0x00)
						return ERR_NOENT;
					if (tmpd->name[0] == 0xE5) /* file is deleted */
						continue;
					if (tmpd->name[0] == 0x05)
						tmpd->name[0] = 0xE5;
					if ((plen = fatio_cmpname(path, tmpd)) > 0) {
						memcpy(d, tmpd, sizeof(*d));
						FATDEBUG("fatio_lookupone found on cluster %d\n", d->cluster);
						return plen;
					}
				}
			}
		}

		if (c.start == FAT_EOF)
			return ERR_NOENT;
	}
}


int fatio_lookup(fat_info_t *info, const char *path, fat_dirent_t *d)
{
	int p, plen;

	FATDEBUG("fatio_lookup path %s\n", path);

	for (p = 0; path[p] == '/'; p++);

	d->cluster = 0;
	if (path[p] == 0) {
		d->attr = 0x10;
		return ERR_NONE;
	}

	for (plen = 0;; p += plen) {
		for (; path[p] == '/'; p++);
		if (path[p] == 0) {
			FATDEBUG("fatio_lookup found on cluster %d\n", d->cluster);
			return ERR_NONE;
		}
		plen = fatio_lookupone(info, path + p, d);
		FATDEBUG("fatio_lookupone returned %d\n", plen);
		if (plen < 0)
			return plen;
		if ((!(d->attr & 0x10)) && (path[plen + p] != 0))
			return ERR_NOENT;
	}

	return ERR_NONE;
}
