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

#include <string.h>


#include "fatio.h"
#include "fatdev.h"
#include "fatfat.h"
#include "types.h"


int fatio_readsuper(void *opt, fat_info_t *info)
{
	char *bits;

	info->off = ((fat_opt_t *)opt)->off;

	if (fatdev_read(info, 0, SIZE_SECTOR, (char *)&info->bsbpb) < 0)
		return -EPROTO;

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
		&& ((info->bsbpb.fat32.BS_BootSig == 0x28) || (info->bsbpb.fat32.BS_BootSig == 0x29)))
		bits = (char *) info->bsbpb.fat32.BS_FilSysType + 3;
	else if ((strncmp((const char *) info->bsbpb.fat.BS_FilSysType, "FAT", 3) == 0)
		&& ((info->bsbpb.fat.BS_BootSig == 0x28) || (info->bsbpb.fat.BS_BootSig == 0x29)))
		bits = (char *) info->bsbpb.fat.BS_FilSysType + 3;
	else
		return -EPROTO;
	if (strncmp(bits, "32 ", 3) == 0)
		info->type = FAT32;
	else if (strncmp(bits, "16 ", 3) == 0)
		info->type = FAT16;
	else if (strncmp(bits, "12 ", 3) == 0)
		info->type = FAT12;
	else
		return -EPROTO;

	return EOK;
}


void fatio_makename(fat_dirent_t *d, fat_name_t *n)
{
	int i, l;
	u16 *np;

	if (d->attr == 0x0F) {
		if (d->no == 0xE5) /* file is deleted */
			return;
		np = (*n) + ((d->no & 0x1F) - 1) * 13;
		memcpy(np, d->lfn1, sizeof(d->lfn1));
		np += sizeof(d->lfn1) / sizeof(d->lfn1[0]);
		memcpy(np, d->lfn2, sizeof(d->lfn2));
		np += sizeof(d->lfn2) / sizeof(d->lfn1[0]);
		memcpy(np, d->lfn3, sizeof(d->lfn3));
		np += sizeof(d->lfn3) / sizeof(d->lfn1[0]);
		if (d->no & 0x40) /* first LNF input */
			*np = 0;
		return;
	}

	if (d->name[0] == 0xE5) { /* file is deleted */
		(*n)[0] = 0;
		return;
	}

	if ((*n)[0] != 0)
		return;

	for (i = 7; (i > 0) && (d->name[i] == ' '); i--);
	if (d->name[i] == ' ') {
		(*n)[0] = 0;
		return;
	}
	l = i + 1;
	for (i = 0; i < l; i++)
		(*n)[i] = (d->cs & 0x8) ? tolower(d->name[i]) : d->name[i];

	for (i = 2; (i > 0) && (d->ext[i] == ' '); i--);
	if (d->ext[i] != ' ') {
		(*n)[l++] = '.';
		(*n)[i + l + 1] = 0;
		for (; i > 0; i--)
			(*n)[i + l] = (d->cs & 0x10) ? tolower(d->ext[i]) : d->ext[i];
		(*n)[i + l] = (d->cs & 0x10) ? tolower(d->ext[i]) : d->ext[i];
	} else {
		(*n)[i + l] = 0;
	}

	if ((*n)[0] == 0x0005)
		(*n)[0] = 0x00E5;
}


static u32 UTF8toUnicode(const char **s)
{
	u32 u = **s;
	int ones;

	for (ones = 0; u & 0x80; ones++)
		u <<= 1;

	u &= 0xFF;
	u >>= ones--;
	(*s)++;
	for (; ones > 0; ones--) {
		if ((**s & 0xC0) != 0x80)
			return -EPROTO;
		u <<= 6;
		u += **s & 0x3F;
		(*s)++;
	}
	return u;
}


s32 UTF16toUnicode(const u16 **s)
{
	s32 u = *(*s)++;

	if ((u & 0xFC00) == 0xD800)
		u = (u & 0x3FF) << 10;
	else if ((u & 0xFC00) == 0xDC00)
		u = (u & 0x3FF);
	else
		return u;
	if ((**s & 0xDC00) == 0xD800)
		u += (**s & 0x3FF) << 10;
	else if ((**s & 0xDC00) == 0xDC00)
		u += (**s & 0x3FF);
	else
		return -EPROTO;
	(*s)++;
	u += 0x10000;
	return u;
}


static int fatio_cmpname(const char *path, fat_name_t *name)
{
	const char *p = path;
	const u16 *n = *name;
	s32 up, un;

	if ((*name)[0] == 0)
		return 0;

	for (un = 1; un != 0;) {
		up = UTF8toUnicode(&p);
		un = UTF16toUnicode(&n);
		if (up < 0) {
			fatprint_err("Unrecognizable character in path detected\n");
			return 0;
		}
		if (up != un)
			break;
	}

	if (((up == '/') || (up == 0)) && (un == 0))
		return p - path - 1;
	return 0;
}


int fatio_lookupone(fat_info_t *info, unsigned int cluster, const char *path, fat_dirent_t *d, unsigned *doff)
{
	fatfat_chain_t c;
	int plen;
	char buff[SIZE_SECTOR];
	fat_dirent_t *tmpd;
	fat_name_t name;
	unsigned int r, ret;

	c.start = cluster;
	c.soff = 0;
	c.scnt = 0;
	name[0] = 0;

	for (r = 0;;r += ret) {
		ret = fatio_read(info, cluster, &c, r, sizeof(buff), buff);
		if (ret < 0)
			return ret;

		for (tmpd = (fat_dirent_t *) buff; (char *) tmpd < ret + buff; tmpd++) {
			if (tmpd->attr == 0x0F) { /* long file name (LFN) data */
				fatio_makename(tmpd, &name);
				continue;
			}
			if (tmpd->name[0] == 0x00)
				return -ENOENT;
			fatio_makename(tmpd, &name);
			if ((plen = fatio_cmpname(path, &name)) > 0) {
				memcpy(d, tmpd, sizeof(*d));
				if (doff != NULL)
					*doff = r / sizeof(*tmpd);
				return plen;
			}
			name[0] = 0;
		}

		if (ret < sizeof(buff))
			return -ENOENT;
	}
}


int fatio_lookup(fat_info_t *info, const char *path, fat_dirent_t *d, unsigned *doff)
{
	int plen;

	d->clusterH = 0;
	d->clusterL = 0;
	d->attr = 0x10;
	for (;;) {
		for (; *path == '/'; path++);
		if (*path == 0)
			return EOK;
		plen = fatio_lookupone(info, ((int) d->clusterL) | (((int) d->clusterH) << 16), path, d, doff);
		if (plen < 0)
			return plen;
		path += plen;
		if ((!(d->attr & 0x10)) && (*path != 0))
			return -ENOENT;
	}

	return EOK;
}


int fatio_read(fat_info_t *info, unsigned int cluster, fatfat_chain_t *c, unsigned int offset, unsigned int size, char * buff)
{
	int i;
	unsigned int r = 0;
	unsigned int secoff, insecoff, o, tr;

	insecoff = offset % info->bsbpb.BPB_BytesPerSec;
	secoff = offset / info->bsbpb.BPB_BytesPerSec;

	if (c->soff > secoff) {
		c->start = cluster;
		c->soff = 0;
		c->scnt = 0;
	}
	if (c->soff + c->scnt <= secoff) {
		if (c->start == FAT_EOF)
			return 0;
		if (c->scnt == 0)
			c->start = cluster;
		if (fatfat_lookup(info, c, secoff - c->soff - c->scnt) < 0)
			return -ENOENT;
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
				return -EPROTO;
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
			return -ENOENT;
	}
}

