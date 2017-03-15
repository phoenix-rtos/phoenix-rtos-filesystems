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


int fatio_readsuper(void *opt, fat_info_t **out)
{
	fat_info_t *info;

	if ((info = (fat_info_t *)malloc(sizeof(fat_info_t))) == NULL)
		return ERR_MEM;

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

	*out = (void *)info;
	return ERR_NONE;
}


void fatio_initname(fat_name_t *n)
{
	n->name[0] = 0;
}


void fatio_makename(fat_dirent_t *d, fat_name_t *n)
{
	int i, l;

	if (d->attr == 0x0F) {
		if (d->no == 0xE5) /* file is deleted */
			return;
		if (d->no & 0x40) /* first LNF input */
			*(n->name + ((d->no & 0x1F)) * 13) = 0;
		memcpy(n->name + ((d->no & 0x1F) - 1) * 13,
		       d->lfn1, sizeof(d->lfn1));
		memcpy(n->name + ((d->no & 0x1F) - 1) * 13 + sizeof(d->lfn1) / sizeof(d->lfn1[0]),
		       d->lfn2, sizeof(d->lfn2));
		memcpy(n->name + ((d->no & 0x1F) - 1) * 13 + (sizeof(d->lfn1) + sizeof(d->lfn2)) / sizeof(d->lfn1[0]),
		       d->lfn3, sizeof(d->lfn3));
		return;
	}

	if (d->name[0] == 0xE5) { /* file is deleted */
		n->name[0] = 0;
		return;
	}

	if (n->name[0] != 0)
		return;

	for (i = 7; (i > 0) && (d->name[i] == ' '); i--);
	if (d->name[i] == ' ') {
		n->name[0] = 0;
		return;
	}
	l = i + 1;
	for (i = 0; i < l; i++)
		n->name[i] = d->name[i];

	for (i = 2; (i > 0) && (d->ext[i] == ' '); i--);
	if (d->ext[i] != ' ') {
		n->name[l++] = '.';
		n->name[i + l + 1] = 0;
		for (; i > 0; i--)
			n->name[i + l] = d->ext[i];
		n->name[i + l] = d->ext[i];
	} else {
		n->name[i + l] = 0;
	}

	if (n->name[0] == 0x0005)
		n->name[0] = 0x00E5;
}


static int fatio_cmpname(const char *path, fat_name_t *n)
{
	int i;
	if (n->name[0] == 0)
		return 0;

	for (i = 0; i < sizeof(n->name) / 2; i++) {
		if (n->name[i] == 0) {
			if ((path[i] == 0) || (path[i] == '/'))
				return i;
			else
				return 0;
		}
		if ((n->name[i] & ~0x007F) == 0) {
			if (tolower(path[i]) != tolower(n->name[i] & 0x007F))
				return 0;
		} else {
			printf("Usupported character in path detected\n");
			return 0;
		}
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

	c.start = ((int) d->clusterL) | (((int) d->clusterH) << 16);
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

	d->clusterH = 0;
	d->clusterL = 0;
	d->attr = 0x10;
	if (path[p] == 0)
		return ERR_NONE;

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
		c->start = ((int) d->clusterL) | (((int) d->clusterH) << 16);
		c->soff = 0;
		c->scnt = 0;
	}
	if (c->soff + c->scnt <= secoff) {
		if (c->start == FAT_EOF)
			return 0;
		if (c->scnt == 0)
			c->start = ((int) d->clusterL) | (((int) d->clusterH) << 16);
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

