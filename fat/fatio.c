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


#include "fatio.h"
#include "fatdev.h"
#include "fatfat.h"


#define min(a, b) ({__typeof__(a) _a = (a); \
	__typeof__(b) _b = (b); \
	_a < _b ? _a : _b;})


typedef struct _fat_name_t {
	char name[12];
	u8 len;
} __attribute__((packed)) fat_name_t;


int fatio_readsuper(void *opt, fat_info_t **out)
{
	fat_info_t *info;

	if ((info = (fat_info_t *)malloc(sizeof(fat_info_t))) == NULL) {
		free(info);
		return ERR_MEM;
	}

	info->off = ((fat_opt_t *)opt)->off;
	info->dev = ((fat_opt_t *)opt)->dev;

	if (fatdev_read(info, 0, SIZE_SECTOR, (char *)&info->bsbpb) < 0) {
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
	if ((strncmp((const char *) info->bsbpb.fat32.BS_FilSysType, "FAT", 3) == 0)
		&& ((info->bsbpb.fat32.BS_BootSig == 0x28) || (info->bsbpb.fat32.BS_BootSig == 0x29))) {
		if (strncmp((char *) info->bsbpb.fat32.BS_FilSysType + 3, "32", 2) == 0)
			info->type = FAT32;
		else if (strncmp((char *) info->bsbpb.fat32.BS_FilSysType + 3, "16", 2) == 0) /* TODO not tested */
			info->type = FAT16;
		else if (strncmp((char *) info->bsbpb.fat32.BS_FilSysType + 3, "12", 2) == 0) /* TODO not tested */
			info->type = FAT12;
		else
			return ERR_PROTO;
	} else if ((strncmp((const char *) info->bsbpb.fat.BS_FilSysType, "FAT", 3) == 0)
		&& ((info->bsbpb.fat.BS_BootSig == 0x28) || (info->bsbpb.fat.BS_BootSig == 0x29))) {
		if (strncmp((char *) info->bsbpb.fat.BS_FilSysType + 3, "32", 2) == 0) /* TODO not tested */
			info->type = FAT32;
		else if (strncmp((char *) info->bsbpb.fat.BS_FilSysType + 3, "16", 2) == 0)
			info->type = FAT16;
		else if (strncmp((char *) info->bsbpb.fat.BS_FilSysType + 3, "12", 2) == 0)
			info->type = FAT12;
		else
			return ERR_PROTO;
	}

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
		n->len = 0;
		return;
	}

	for (s = 2; (s > 0) && (d->ext[s] == ' '); s--);
	if (d->ext[s] != ' ') {
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
}


static int fatio_cmpname(const char *path, fat_name_t *n)
{
	if (n->len == 0)
		return 0;

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

	c.start = d->cluster;
	c.soff = 0;
	c.scnt = 0;
	fatio_initname(&name);

	for (r = 0;;r += ret) {
		ret = fatio_read(info, d, &c, r, sizeof(buff), buff);
		if (ret < 0)
			return ret;

		for (tmpd = (fat_dirent_t *) buff; (char *) tmpd < ret + buff; tmpd++) {
			if (tmpd->attr == 0x0F) { /* long file name (LFN) data */
				fatio_makename(tmpd, &name);
				continue;
			}
			if (tmpd->name[0] == 0x00)
				return ERR_NOENT;
			fatio_makename(tmpd, &name);
			if ((plen = fatio_cmpname(path, &name)) > 0) {
				memcpy(d, tmpd, sizeof(*d));
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

	for (p = 0; path[p] == '/'; p++);

	d->cluster = 0;
	if (path[p] == 0) {
		d->attr = 0x10;
		return ERR_NONE;
	}

	for (plen = 0;; p += plen) {
		for (; path[p] == '/'; p++);
		if (path[p] == 0) {
			return ERR_NONE;
		}
		plen = fatio_lookupone(info, path + p, d);
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

	insecoff = offset % info->bsbpb.BPB_BytesPerSec;
	secoff = offset / info->bsbpb.BPB_BytesPerSec;

	if (c->soff > secoff) {
		c->start = d->cluster;
		c->soff = 0;
		c->scnt = 0;
	}
	if (c->soff + c->scnt <= secoff) {
		if (c->start == FAT_EOF)
			return 0;
		if (fatfat_lookup(info, c, secoff - c->soff - c->scnt) < 0)
			return ERR_NOENT;
	}

	secoff -= c->soff;
	for (;;) {
		for (i = 0; i < SIZE_CHAIN_AREAS; i++) {
			if (!c->areas[i].start)
				return r;

			if (c->areas[i].size <= secoff) {
				secoff -= c->areas[i].size;
				continue;
			}
			
			o = (c->areas[i].start + secoff) * info->bsbpb.BPB_BytesPerSec + insecoff;
			tr = min((c->areas[i].size - secoff) * info->bsbpb.BPB_BytesPerSec - insecoff, size - r);
			if (fatdev_read(info, o, tr, (char *)buff))
				return ERR_PROTO;
			insecoff = 0;
			secoff = 0;
			r += tr;
			buff += tr;
			if (r == size)
				return size;
		}

		if (c->start == FAT_EOF)
			return r;

		if (fatfat_lookup(info, c, 0) < 0)
			return ERR_NOENT;
	}
}

