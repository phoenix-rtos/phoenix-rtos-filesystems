/*
 * Phoenix-RTOS
 *
 * FAT filesystem driver
 *
 * Filesystem operations
 *
 * Copyright 2012, 2023 Phoenix Systems
 * Author: Pawel Pisarczyk, Jacek Maksymowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <string.h>
#include <ctype.h>

#include "fatio.h"
#include "fatdev.h"
#include "fatchain.h"

#define LOG_TAG "fatio"
/* clang-format off */
#define LOG_ERROR(str, ...) do { fprintf(stderr, LOG_TAG " error: " str "\n", ##__VA_ARGS__); } while (0)
#define TRACE(str, ...)     do { if (FATFS_DEBUG) fprintf(stderr, LOG_TAG " trace: " str "\n", ##__VA_ARGS__); } while (0)
/* clang-format on */

#define DEBUG_DISABLE_LFN 0

static void fat_unpackBSBPB(fat_bsbpbUnpacked_t *out, fat_bsbpb_t *bsbpb, fat_type_t type);


int fat_readFilesystemInfo(fat_info_t *info)
{
	int ret;
	size_t size = sizeof(fat_bsbpb_t);
	fat_bsbpb_t *bsbpb = malloc(size);
	if (bsbpb == NULL) {
		return -ENOMEM;
	}

	ret = fatdev_read(info, 0, size, bsbpb);
	if (ret < 0) {
		free(bsbpb);
		return ret;
	}

	if ((bsbpb->BPB_BytesPerSec == 0) || (bsbpb->BPB_SecPerClus == 0)) {
		free(bsbpb);
		return -EINVAL;
	}

	info->fatoffBytes = bsbpb->BPB_RsvdSecCnt * bsbpb->BPB_BytesPerSec;
	uint32_t fatSectors = (bsbpb->BPB_FATSz16 != 0) ? bsbpb->BPB_FATSz16 : bsbpb->fat32.BPB_FATSz32;
	uint32_t totalSectors = (bsbpb->BPB_TotSecS != 0) ? bsbpb->BPB_TotSecS : bsbpb->BPB_TotSecL;
	info->rootoff = bsbpb->BPB_RsvdSecCnt + fatSectors * (bsbpb->BPB_NumFATs);
	size_t rootDirEntries = (bsbpb->BPB_RootEntCnt * sizeof(fat_dirent_t));
	if ((rootDirEntries % bsbpb->BPB_BytesPerSec) != 0) {
		free(bsbpb);
		return -EINVAL;
	}

	info->dataoff = info->rootoff + rootDirEntries / bsbpb->BPB_BytesPerSec;
	info->clusters = totalSectors / bsbpb->BPB_SecPerClus;
	info->dataClusters = info->clusters - (info->dataoff / bsbpb->BPB_SecPerClus);

	/* Check FAT type */
	if ((bsbpb->BPB_FATSz16 == 0) && (bsbpb->BPB_TotSecS == 0) &&
		((bsbpb->fat32.BS_BootSig == 0x28) || (bsbpb->fat32.BS_BootSig == 0x29)) &&
		(info->clusters >= 65525)) {
		info->type = FAT32;
	}
	else if (((bsbpb->fat.BS_BootSig == 0x28) || (bsbpb->fat.BS_BootSig == 0x29)) && (info->clusters < 65525)) {
		info->type = (info->clusters >= 4085) ? FAT16 : FAT12;
	}
	else {
		free(bsbpb);
		return -EINVAL;
	}


	fat_unpackBSBPB(&info->bsbpb, bsbpb, info->type);
	free(bsbpb);
	return EOK;
}


static int fat_appendWithoutPadding(fat_dirent_t *d, uint16_t *name, bool ext)
{
	int outlen = 0;
	int len = ext ? sizeof(d->ext) : sizeof(d->name);
	const uint8_t *ptr = ext ? d->ext : d->name;
	uint8_t caseBit = ext ? FAT_NTCASE_EXT_LOWER : FAT_NTCASE_NAME_LOWER;
	for (int i = len - 1; i >= 0; i--) {
		if (ptr[i] != ' ' && outlen == 0) {
			outlen = i + 1;
		}

		name[i] = ((d->ntCase & caseBit) != 0) ? tolower(ptr[i]) : ptr[i];
	}

	return outlen;
}


bool fatdir_extractName(fat_dirent_t *d, fat_name_t *n)
{
	if ((d->attr == FAT_ATTR_LFN) && (DEBUG_DISABLE_LFN == 0)) {
		if (fat_isDeleted(d) || ((d->no & 0x1f) == 0)) {
			return false;
		}

		size_t lfnIndex = (d->no & 0x1f) - 1;
		if (lfnIndex > 19) {
			/* Entry beyond 255 character limit */
			return false;
		}

		uint16_t *np = n->chars + lfnIndex * 13;
		size_t toCopy = sizeof(d->lfn1);
		memcpy(np, d->lfn1, toCopy);
		np += toCopy / sizeof(d->lfn1[0]);
		toCopy = (lfnIndex == 19) ? 6 : sizeof(d->lfn2);
		memcpy(np, d->lfn2, toCopy);
		np += toCopy / sizeof(d->lfn2[0]);
		toCopy = (lfnIndex == 19) ? 0 : sizeof(d->lfn3);
		memcpy(np, d->lfn3, toCopy);
		np += toCopy / sizeof(d->lfn3[0]);
		if ((d->no & 0x40) != 0) {
			/* first LFN input */
			*np = 0;
			n->checksum = d->cksum;
			n->lfnRemainingBits = (1U << lfnIndex) - 1;
		}
		else {
			n->lfnRemainingBits &= ~(1U << lfnIndex);
			if (n->checksum != d->cksum) {
				/* Entry for a different file from previously parsed */
				n->lfnRemainingBits = NO_LFN_BIT;
			}
		}

		return false;
	}

	if (fat_isDeleted(d)) {
		n->chars[0] = 0;
		return true;
	}

	if (n->lfnRemainingBits == 0) {
		/* Already have real name from LFN */
		uint8_t calcChecksum = 0;
		for (int i = 0; i < sizeof(d->nameExt); i++) {
			calcChecksum = ((((calcChecksum & 1) << 7) | (calcChecksum >> 1)) + d->nameExt[i]) & 0xff;
		}

		if (calcChecksum != n->checksum) {
			TRACE("LFN checksum fail %u %u\n", calcChecksum, n->checksum);
			fat_initFatName(n);
		}
		else {
			return true;
		}
	}

	int namelen = fat_appendWithoutPadding(d, n->chars, false);
	if (namelen == 0) {
		n->chars[0] = 0;
		return true;
	}

	if (d->name[0] == 0x05) {
		n->chars[0] = 0xe5;
	}

	n->chars[namelen] = (uint16_t)'.';
	namelen++;

	int extlen = fat_appendWithoutPadding(d, n->chars + namelen, true);
	if (extlen == 0) {
		namelen--;
	}
	else {
		namelen += extlen;
	}

	n->chars[namelen] = 0;
	return true;
}


static void fat_unpackBSBPB(fat_bsbpbUnpacked_t *out, fat_bsbpb_t *bsbpb, fat_type_t type)
{
	out->BPB_TotSecL = bsbpb->BPB_TotSecL;
	out->BPB_HiddSec = bsbpb->BPB_HiddSec;
	out->BPB_BytesPerSec = bsbpb->BPB_BytesPerSec;
	out->BPB_RsvdSecCnt = bsbpb->BPB_RsvdSecCnt;
	out->BPB_RootEntCnt = bsbpb->BPB_RootEntCnt;
	out->BPB_TotSecS = bsbpb->BPB_TotSecS;
	out->BPB_FATSz16 = bsbpb->BPB_FATSz16;
	out->BPB_SecPerClus = bsbpb->BPB_SecPerClus;
	out->BPB_NumFATs = bsbpb->BPB_NumFATs;
	out->BPB_Media = bsbpb->BPB_Media;
	if (type == FAT32) {
		out->fat32.BPB_FATSz32 = bsbpb->fat32.BPB_FATSz32;
		out->fat32.BPB_ExtFlags = bsbpb->fat32.BPB_ExtFlags;
		out->fat32.BPB_FSVer = bsbpb->fat32.BPB_FSVer;
		out->fat32.BPB_RootClus = bsbpb->fat32.BPB_RootClus;
		out->fat32.BPB_FSInfo = bsbpb->fat32.BPB_FSInfo;
		out->fat32.BPB_BkBootSec = bsbpb->fat32.BPB_BkBootSec;

		out->BS_BootSig = bsbpb->fat32.BS_BootSig;
		out->BS_VolID = bsbpb->fat32.BS_VolID;
		out->BS_DrvNum = bsbpb->fat32.BS_DrvNum;
		memcpy(out->BS_VolLab, bsbpb->fat32.BS_VolLab, sizeof(out->BS_VolLab));
		memcpy(out->BS_FilSysType, bsbpb->fat32.BS_FilSysType, sizeof(out->BS_FilSysType));
	}
	else {
		out->BS_BootSig = bsbpb->fat.BS_BootSig;
		out->BS_VolID = bsbpb->fat.BS_VolID;
		out->BS_DrvNum = bsbpb->fat.BS_DrvNum;
		memcpy(out->BS_VolLab, bsbpb->fat.BS_VolLab, sizeof(out->BS_VolLab));
		memcpy(out->BS_FilSysType, bsbpb->fat.BS_FilSysType, sizeof(out->BS_FilSysType));
	}

	memcpy(out->BS_OEMName, bsbpb->BS_OEMName, sizeof(out->BS_OEMName));
}


time_t fatdir_getFileTime(fat_dirent_t *d, enum FAT_FILE_TIMES type)
{
	uint16_t fatTime, fatDate;
	uint8_t extra10ms = 0;
	struct tm outputTime;
	switch (type) {
		case FAT_FILE_MTIME:
			fatTime = d->mtime;
			fatDate = d->mdate;
			break;

		case FAT_FILE_CTIME:
			extra10ms = d->ctime_ms;
			fatTime = d->ctime;
			fatDate = d->cdate;
			break;

		case FAT_FILE_ATIME:
			fatTime = 0;
			fatDate = d->adate;
			break;

		default:
			return 0;
			break;
	}

	outputTime.tm_sec = (fatTime & 0x1f) * 2 + extra10ms / 100;
	outputTime.tm_min = (fatTime >> 5) & 0x3f;
	outputTime.tm_hour = (fatTime >> 11) & 0x1f;
	outputTime.tm_mday = fatDate & 0x1f;
	outputTime.tm_mon = ((fatDate >> 5) & 0xf) - 1;
	outputTime.tm_year = ((fatDate >> 9) & 0x7f) + 80;
	return timegm(&outputTime);
}


static int32_t UTF8toUnicode(const char **s)
{
	int32_t u = **s;
	int ones;

	for (ones = 0; (u & 0x80) != 0; ones++) {
		u <<= 1;
	}

	u &= 0xff;
	u >>= ones;
	ones--;
	(*s)++;
	for (; ones > 0; ones--) {
		if ((**s & 0xc0) != 0x80) {
			return -EPROTO;
		}

		u <<= 6;
		u |= **s & 0x3f;
		(*s)++;
	}

	return u;
}


static int32_t UTF16toUnicode(const uint16_t **s)
{
	int32_t u = **s;
	(*s)++;

	if ((u & 0xfc00) == 0xd800) {
		u = (u & 0x3ff) << 10;
	}
	else if ((u & 0xfc00) == 0xdc00) {
		u = (u & 0x3ff);
	}
	else {
		return u;
	}

	if ((**s & 0xfc00) == 0xd800) {
		u += (**s & 0x3ff) << 10;
	}
	else if ((**s & 0xfc00) == 0xdc00) {
		u += (**s & 0x3ff);
	}
	else {
		return -EPROTO;
	}

	(*s)++;
	u += 0x10000;
	return u;
}


static size_t UnicodeUTF8Size(int32_t codepoint)
{
	size_t reqdChars = 1;
	reqdChars += (codepoint >= 0x0080) ? 1 : 0;
	reqdChars += (codepoint >= 0x0800) ? 1 : 0;
	reqdChars += (codepoint >= 0x10000) ? 1 : 0;
	return reqdChars;
}


static size_t UnicodeToUTF8(int32_t codepoint, char **s, char **end)
{
	if (codepoint >= 0x110000) {
		return 0;
	}

	size_t reqdChars = UnicodeUTF8Size(codepoint);
	if (((*s) + reqdChars) > (*end)) {
		*end = *s;
		return reqdChars;
	}

	if (reqdChars == 1) {
		**s = codepoint & 0x7f;
		(*s)++;
	}
	else {
		char *c = (*s) + reqdChars - 1;
		while (c != (*s)) {
			*c = (codepoint & 0x3f) | 0x80;
			codepoint >>= 6;
			c--;
		}

		*c = ((0xf00u >> reqdChars) | codepoint) & 0xff;
		(*s) += reqdChars;
	}

	return reqdChars;
}


ssize_t fatdir_nameToUTF8(fat_name_t *name, char *out, size_t outSize)
{
	char *end = out + outSize;
	const uint16_t *chars = name->chars;
	const uint16_t *chars_end = chars + FAT_MAX_NAMELEN + 1;
	size_t totalSize = 0;
	while (chars < chars_end) {
		int32_t codepoint = UTF16toUnicode(&chars);
		if (codepoint < 0) {
			return codepoint;
		}

		size_t ret = UnicodeToUTF8(codepoint, &out, &end);
		if (ret == 0) {
			return -EINVAL;
		}

		totalSize += ret;
		if (codepoint == 0) {
			break;
		}
	}

	return totalSize;
}


static ssize_t fatio_cmpname(const char *path, fat_name_t *name)
{
	const char *p = path;
	const uint16_t *n = name->chars;
	int32_t up, un;

	if (n[0] == 0) {
		return 0;
	}

	do {
		up = UTF8toUnicode(&p);
		un = UTF16toUnicode(&n);
		if ((up < 0) || (un < 0)) {
			LOG_ERROR("Unrecognizable character in path");
			return 0;
		}
	} while ((un != 0) && (up == un));

	if (((up == '/') || (up == 0)) && (un == 0)) {
		return p - path - 1;
	}

	return 0;
}


int fatio_dirScan(fat_info_t *info, fatchain_cache_t *c, uint32_t offset, fat_dirScanCb_t cb, void *cbArg)
{
	fat_dirent_t buff[4];
	fat_name_t *name = malloc(sizeof(fat_name_t));
	if (name == NULL) {
		return -ENOMEM;
	}

	fat_initFatName(name);
	ssize_t retlen;
	do {
		retlen = fatio_read(info, c, offset, sizeof(buff), buff);
		if (retlen < 0) {
			free(name);
			return retlen;
		}

		size_t nRead = retlen / sizeof(fat_dirent_t);
		for (size_t i = 0; i < nRead; i++) {
			fat_dirent_t *d = &buff[i];
			if (fat_isDirentNull(d)) {
				free(name);
				return cb(cbArg, NULL, NULL, offset + i * sizeof(fat_dirent_t));
			}

			if (!fatdir_extractName(d, name)) {
				continue;
			}

			int cb_ret = cb(cbArg, d, name, offset + i * sizeof(fat_dirent_t));
			if (cb_ret < 0) {
				free(name);
				return cb_ret;
			}

			fat_initFatName(name);
		}

		offset += retlen;
	} while (retlen == sizeof(buff));

	free(name);
	return -ENOENT;
}


typedef struct {
	const char *path;
	fat_dirent_t *out;
	unsigned int plenOut;
	uint32_t offsetOut;
} fat_lookupScannerArg_t;


static int fatio_dirScannerLookupCallback(void *arg, fat_dirent_t *d, fat_name_t *name, uint32_t offsetInDir)
{
	fat_lookupScannerArg_t *state = arg;
	if (d == NULL) {
		return -ENOENT;
	}

	int plen = fatio_cmpname(state->path, name);
	if (plen > 0) {
		state->plenOut = plen;
		memcpy(state->out, d, sizeof(*d));
		state->offsetOut = offsetInDir;
		return -EEXIST;
	}

	return 0;
}


ssize_t fatio_lookupOne(fat_info_t *info, const char *path, fat_dirent_t *d, fat_fileID_t *id)
{
	uint32_t cluster = fat_getCluster(d, info->type);
	fat_lookupScannerArg_t state;
	state.path = path;
	state.out = d;

	fatchain_cache_t c;
	fatchain_initCache(&c, cluster);
	int ret = fatio_dirScan(info, &c, 0, fatio_dirScannerLookupCallback, &state);
	if (id != NULL) {
		id->dirCluster = cluster;
		id->offsetInDir = state.offsetOut;
	}

	return ret == -EEXIST ? state.plenOut : ret;
}


int fatio_lookupUntilEnd(fat_info_t *info, const char *path, fat_dirent_t *d, fat_fileID_t *id)
{
	ssize_t plen;
	for (;;) {
		while (*path == '/') {
			path++;
		}

		if (*path == '\0') {
			return EOK;
		}

		plen = fatio_lookupOne(info, path, d, id);
		if (plen < 0) {
			return plen;
		}

		path += plen;
		if ((!fat_isDirectory(d)) && (*path != '\0')) {
			return -ENOENT;
		}
	}

	return EOK;
}


int fatio_lookupPath(fat_info_t *info, const char *path, fat_dirent_t *d, fat_fileID_t *id)
{
	/* Create a fake "parent directory" entry for root directory */
	fat_setCluster(d, ROOT_DIR_CLUSTER);
	d->attr = FAT_ATTR_DIRECTORY;
	if (id != NULL) {
		id->raw = FAT_ROOT_ID;
	}

	return fatio_lookupUntilEnd(info, path, d, id);
}


ssize_t fatio_read(fat_info_t *info, fatchain_cache_t *c, offs_t offset, size_t size, void *buff)
{
	size_t totalRead = 0;

	unsigned int insecoff = offset % info->bsbpb.BPB_BytesPerSec;
	unsigned int secoff = offset / info->bsbpb.BPB_BytesPerSec;

	if (c->areasOffset > secoff) {
		TRACE("rewind\n");
		c->nextAfterAreas = c->chainStart;
		c->areasOffset = 0;
		c->areasLength = 0;
	}

	if (c->areasOffset + c->areasLength <= secoff) {
		/* Not enough range in c->areas to reach requested offset */
		if (c->nextAfterAreas == FAT_EOF) {
			/* c->areas reaches end of chain, offset is past the end */
			return 0;
		}

		if (c->areasLength == 0) {
			c->nextAfterAreas = c->chainStart;
		}

		if (fatchain_parseNext(info, c, secoff - c->areasOffset - c->areasLength) < 0) {
			return -ENOENT;
		}
	}

	secoff -= c->areasOffset;
	for (;;) {
		for (int i = 0; i < FAT_CHAIN_AREAS; i++) {
			if (c->areas[i].start == 0) {
				/* End of chain, cannot read more */
				return totalRead;
			}

			if (c->areas[i].size <= secoff) {
				secoff -= c->areas[i].size;
				continue;
			}

			size_t chunk_offs = (c->areas[i].start + secoff) * info->bsbpb.BPB_BytesPerSec + insecoff;
			size_t chunk_size = (c->areas[i].size - secoff) * info->bsbpb.BPB_BytesPerSec - insecoff;
			size_t read_size = min(chunk_size, size - totalRead);
			int ret = fatdev_read(info, chunk_offs, read_size, buff);
			if (ret < 0) {
				return ret;
			}


			insecoff = 0;
			secoff = 0;
			totalRead += read_size;
			buff += read_size;
			if (totalRead == size) {
				return size;
			}
		}

		if (c->nextAfterAreas == FAT_EOF) {
			return totalRead;
		}

		if (fatchain_parseNext(info, c, 0) < 0) {
			return -ENOENT;
		}
	}
}


void fat_printFilesystemInfo(fat_info_t *info, bool printFat)
{
	unsigned int i, next;
	printf("Boot Sector and Boot Parameter Block:\n");
	printf("BS_VolLab: %.*s\n", (int)sizeof(info->bsbpb.BS_VolLab), info->bsbpb.BS_VolLab);
	printf("BS_FilSysType: %.*s\n", (int)sizeof(info->bsbpb.BS_FilSysType), info->bsbpb.BS_FilSysType);
	printf("BS_OEMName: %.*s\n", (int)sizeof(info->bsbpb.BS_OEMName), info->bsbpb.BS_OEMName);
	printf("BS_DrvNum: %d\n", info->bsbpb.BS_DrvNum);
	printf("BS_BootSig: %d\n", info->bsbpb.BS_BootSig);
	printf("BS_VolID: %d\n", info->bsbpb.BS_VolID);

	printf("BPB_BytesPerSec: %d\n", info->bsbpb.BPB_BytesPerSec);
	printf("BPB_SecPerClus: %d\n", info->bsbpb.BPB_SecPerClus);
	printf("BPB_RsvdSecCnt: %d\n", info->bsbpb.BPB_RsvdSecCnt);
	printf("BPB_NumFATs: %d\n", info->bsbpb.BPB_NumFATs);
	printf("BPB_RootEntCnt: %d\n", info->bsbpb.BPB_RootEntCnt);
	printf("BPB_TotSecS: %d\n", info->bsbpb.BPB_TotSecS);
	printf("BPB_Media: %02x\n", info->bsbpb.BPB_Media);
	printf("BPB_FATSz16: %d\n", info->bsbpb.BPB_FATSz16);
	printf("BPB_HiddSec: %d\n", info->bsbpb.BPB_HiddSec);
	printf("BPB_TotSecL: %d\n", info->bsbpb.BPB_TotSecL);

	if (info->type == FAT32) {
		printf(" BPB_FATSz32: %d\n", info->bsbpb.fat32.BPB_FATSz32);
		printf(" BPB_FSVer: %d\n", info->bsbpb.fat32.BPB_FSVer);
		printf(" BPB_RootClus: %d\n", info->bsbpb.fat32.BPB_RootClus);
		printf(" BPB_FSInfo: %d\n", info->bsbpb.fat32.BPB_FSInfo);
		printf(" BPB_BkBootSec: %d\n", info->bsbpb.fat32.BPB_BkBootSec);
	}

	printf("\nFilesystem parameters:\n");
	printf(" fatoffBytes: %llu\n", (uint64_t)info->fatoffBytes);
	printf(" rootoff: %d\n", info->rootoff);
	printf(" dataoff: %d\n", info->dataoff);
	printf(" dataClusters: %d\n", info->dataClusters);
	printf(" clusters: %d\n", info->clusters);

	if (printFat) {
		printf("1st FAT");

		for (i = 0; i < 256; i++) {
			if (fatchain_getOne(info, i, &next) < 0) {
				break;
			}

			if ((i % 8) == 0) {
				printf("\n %08x:", i);
			}

			if (next == FAT_EOF) {
				printf("[xxxxxxxx] ");
			}
			else if (next == 0) {
				printf("[        ] ");
			}
			else {
				printf("[%8x] ", next);
			}
		}

		printf("\n");
	}
}
