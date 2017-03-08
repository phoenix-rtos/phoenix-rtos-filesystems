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
	info->rootoff = info->fatend + (info->fatend - info->fatoff) * (info->bsbpb.BPB_NumFATs - 1); // TODO on FAT32 is is someware else
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


static int fatio_cmpname(const char *path, fat_dirent_t *d)
{
	if (path[0] == d->name[0])
		return 1;
	else
		return 0;
	return 1;
}


static void fat_dumpdirent(fat_dirent_t *d)
{
	printf("XXX name: %.8s.%.3s\n", d->name, d->ext); //TODO first byte can be special
	printf("attr:");
	if (d->attr & 0x01)
		printf(" RO");
	if (d->attr & 0x02)
		printf(" HIDDEN");
	if (d->attr & 0x04)
		printf(" SYSTEM");
	if (d->attr & 0x08)
		printf(" DVL");
	if (d->attr & 0x10)
		printf(" DIR");
	if (d->attr & 0x20)
		printf(" ARCHIVE");
	if (d->attr & 0xC0)
		printf(" ERROR");
	printf("\n");
	printf("mtime: %d\n", d->mtime);
	printf("mdate: %d\n", d->mdate);
	printf("start cluster: %d\n", d->cluster);
	printf("size: %d\n", d->size);
}


static int fatio_lookupone(fat_info_t *info, const char *path, fatfat_chain_t *c, fat_dirent_t *d)
{
	int i, plen;
	u8 buff[SIZE_SECTOR * 32];
	fat_dirent_t *tmpd;

	for (;;) {
		if (fatfat_lookup(info, c) < 0)
			return ERR_NOENT;

		for (i = 0; i < SIZE_CHAIN_AREAS; i++) {
			if (!c->areas[i].start)
				return ERR_NOENT;

// 			printf("c.areas[%d].start: %d+%d\n", i, c.areas[i].start, c.areas[i].size);

			if (fatdev_read(info, c->areas[i].start, min(c->areas[i].size, sizeof(buff) / SIZE_SECTOR), (char *)buff))
				return ERR_PROTO;

			for (tmpd = (fat_dirent_t *) buff; (u8 *) tmpd < sizeof(buff) + buff; tmpd++) {
				if (tmpd->name[0] == 0)
					return ERR_NONE;
				fat_dumpdirent(tmpd);
				printf("\n");
				if ((plen = fatio_cmpname(path, tmpd)) > 0) {
					memcpy(d, tmpd, sizeof(*d));
					return plen;
				}
			}
		}

		if (c->start == FAT_EOF)
			return ERR_NOENT;
	}
}


int fatio_lookup(fat_info_t *info, const char *path, fat_dirent_t *d)
{
	fatfat_chain_t c;
	unsigned int p, plen;

	printf("lookup path %s\n", path);

	for (p = 0; path[p] == '/'; p++);

	if (path[p] == 0) {
		d->cluster = info->rootoff;
		d->attr = 0x10;
		return ERR_NONE;
	}

// 	c.start = info->bsbpb.fat32.BPB_RootClus;
	c.start = info->rootoff;

	for (plen = 0;; p += plen) {
		if (path[p] == 0) {
			printf("found !\n");
			return ERR_NONE;
		}
		plen = fatio_lookupone(info, path + p, &c, d);
		printf("lookupone returned %d\n", plen);
		if (plen < 0)
			return ERR_NOENT;
// 		if ((!(d->attr & 0x10)) && (path[plen + p] != 0))
// 			return ERR_NOENT;
	}

	return ERR_NONE;
}
