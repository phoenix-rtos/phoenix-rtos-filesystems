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
#include "fatio.h"
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

	if (fatdev2_read(info, 0, SIZE_SECTOR, (char *)&info->bsbpb) < 0) {
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


static void fatio_initname(fat_name_t *n)
{
	n->len = 0;
}


static void fatio_makename(fat_dirent_t *d, fat_name_t *n)
{
	int s;

	if (d->attr == 0x0F)
		return; //TODO

	if (d->name[0] == 0xE5) { /* file is deleted */
		FATDEBUG("fatio_makename deleted file\n");
		n->len = 0;
		return;
	}

	for (s = 2; (s > 0) && (d->ext[s] == ' '); s--);
	if (d->ext[s] != ' ') {
		FATDEBUG("fatio_makename extension found\n");
		n->len += s + 1;
		memcpy(n->name + sizeof(n->name) - n->len, d->ext, s + 1);
		n->len++;
		n->name[sizeof(n->name) - n->len] = '.';
	}

	for (s = 7; (s > 0) && (d->name[s] == ' '); s--);
	if (d->name[s] != ' ') {
		n->len += s + 1;
		memcpy(n->name + sizeof(n->name) - n->len, d->name, s + 1);
	}
	if (n->name[sizeof(n->name) - n->len] == 0x05)
		n->name[sizeof(n->name) - n->len] = 0xE5;

	FATDEBUG("fatio_makename is %.*s\n", n->len, n->name + sizeof(n->name) - n->len);
}


static int fatio_cmpname(const char *path, fat_name_t *n)
{
	if (n->len == 0)
		return 0;

	FATDEBUG("fatio_cmpname %s %.*s len %d\n", path, n->len, n->name + sizeof(n->name) - n->len, n->len);
	if (strncmp(path, n->name + sizeof(n->name) - n->len, n->len) == 0) {
		if ((path[n->len] == 0) || (path[n->len] == '/'))
			return n->len;
		else
			return 0;
	}
	return 0;
}


static int fatio_lookupone(fat_info_t *info, const char *path, fat_dirent_t *d)
{
	fatfat_chain_t c;
	int plen;
	char buff[SIZE_SECTOR * 32];
	fat_dirent_t *tmpd;
	fat_name_t name;
	unsigned int r, ret;

	FATDEBUG("fatio_lookupone path %s\n", path);
	c.start = d->cluster;
	c.soff = 0;
	c.scnt = 0;
	fatio_initname(&name);

	for (r = 0;;r += ret) {
		ret = fatio_read(info, d, &c, r, sizeof(buff), buff);
		FATDEBUG("fatio_lookupone readret is %d\n", ret);
		if (ret < 0)
			return ret;

		for (tmpd = (fat_dirent_t *) buff; (char *) tmpd < ret + buff; tmpd++) {
			FATDEBUG("fatio_lookupone loop\n");
			if (tmpd->attr == 0x0F) { /* long file name (LFN) data */
				fatio_makename(tmpd, &name);
				continue;
			}
			if (tmpd->name[0] == 0x00) {
				FATDEBUG("fatio_lookupone end of dir entries\n");
				return ERR_NOENT;
			}
			fatio_makename(tmpd, &name);
			if ((plen = fatio_cmpname(path, &name)) > 0) {
				memcpy(d, tmpd, sizeof(*d));
				FATDEBUG("fatio_lookupone found on cluster %d\n", d->cluster);
				return plen;
			}
			fatio_initname(&name);
		}

		if (ret < sizeof(buff))
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


int fatio_read(fat_info_t *info, fat_dirent_t *d, fatfat_chain_t *c, unsigned int offset, unsigned int size, char * buff)
{
	int i;
	unsigned int r = 0;
	unsigned int secoff, insecoff, o, tr;

	FATDEBUG("fatio_read begin chain %u+%u\n", c->soff, c->scnt);
	FATDEBUG("fatio_read cstart is %d\n", c->start);
	

	insecoff = offset % info->bsbpb.BPB_BytesPerSec;
	secoff = offset / info->bsbpb.BPB_BytesPerSec;

	if (c->soff > secoff) {
		c->start = d->cluster;
		c->soff = 0;
		c->scnt = 0;
		FATDEBUG("fatio_read rereading chain\n");
	}
	FATDEBUG("fatio_read chain checkskip %d+%d ? %d insecoff %d\n", c->soff, c->scnt, secoff, insecoff);
	if (c->soff + c->scnt <= secoff) {
		FATDEBUG("fatio_read chain skip %d\n", secoff - c->soff - c->scnt);
		if (c->start == FAT_EOF) {
			FATDEBUG("fatio_read no more data to read\n");
			return 0;
		}
		if (fatfat_lookup(info, c, secoff - c->soff - c->scnt) < 0)
			return ERR_NOENT;
	}

	secoff -= c->soff;
	for (;;) {
		FATDEBUG("fatio_read cstart is %d\n", c->start);
		for (i = 0; i < SIZE_CHAIN_AREAS; i++) {
		FATDEBUG("fatio_read cstart is %d\n", c->start);
			FATDEBUG("fatio_read chain is [%d, %d] start[%d] %d+%d, secoffs %d read %d\n",
				 c->areas[i].start * info->bsbpb.BPB_BytesPerSec, (c->areas[i].start + c->areas[i].size) * info->bsbpb.BPB_BytesPerSec,
				 i, c->areas[i].start, c->areas[i].size,
				 secoff, r
				);
			if (!c->areas[i].start)
				return r;

			if (c->areas[i].size <= secoff) {
				secoff -= c->areas[i].size;
				FATDEBUG("fatio_read skipping chain %d secoff %d->%d\n", i, secoff + c->areas[i].size, secoff);
				continue;
			}
		FATDEBUG("fatio_read cstart is %d\n", c->start);
			
			o = (c->areas[i].start + secoff) * info->bsbpb.BPB_BytesPerSec + insecoff;
			tr = min((c->areas[i].size - secoff) * info->bsbpb.BPB_BytesPerSec - insecoff, size - r);
			FATDEBUG("size -r is %d\n", size - r);
			if (fatdev2_read(info, o, tr, (char *)buff)) {
				FATDEBUG("fatio_read ERR_PROTO\n");
				return ERR_PROTO;
			}
			FATDEBUG("tr is %d\n", tr);
			insecoff = 0;
			secoff = 0;
			r += tr;
			buff += tr;
			if (r == size)
				return size;
		FATDEBUG("fatio_read cstart is %d\n", c->start);
		}

		FATDEBUG("fatio_read cstart is %d\n", c->start);
		if (c->start == FAT_EOF)
			return r;

		FATDEBUG("fatio_read doing lookup\n");
		if (fatfat_lookup(info, c, 0) < 0)
			return ERR_NOENT;
	}
}

