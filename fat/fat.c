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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <locale.h>


#include "fat.h"
#include "fatio.h"
#include "fatfat.h"
#include "fatdev.h"


#define min(a, b) ({__typeof__(a) _a = (a); \
	__typeof__(b) _b = (b); \
	_a < _b ? _a : _b;})


int fat_init(const char *name, offs_t off, fat_info_t *out)
{
	fat_opt_t opt;
	int err;

	opt.off = off;
	opt.bufpsz = 128 * 1024;
	opt.bufsz = opt.bufpsz * 512;

	if ((err = fatdev_init(name, &opt, out)) != EOK)
		return err;
	if ((err = fatio_readsuper(&opt, out)) < 0) {
		fatdev_deinit(out);
		return err;
	}

	return EOK;
}


static void fat_dumpstr(char *label, char *s, unsigned int len, char hex)
{
	unsigned int i;

	printf("%s: ", label);
	for (i = 0; i < len; i++) {
		if (hex)
			printf("%02x ", s[i] & 0xff);
		else
			printf("%c", s[i]);
	}
	printf("\n");
}


static void fat_dumpinfo(fat_info_t *info)
{
	unsigned int i, next;

	fat_dumpstr("BS_jmpBoot", (char *)info->bsbpb.BS_jmpBoot, 3, 1);
	fat_dumpstr("BS_OEMName", (char *)info->bsbpb.BS_OEMName, 7, 0);

	printf("BPB_BytesPerSec: %d\n", info->bsbpb.BPB_BytesPerSec);
	printf("BPB_SecPerClus: %d\n", info->bsbpb.BPB_SecPerClus);
	printf("BPB_RsvdSecCnt: %d\n", info->bsbpb.BPB_RsvdSecCnt);
	printf("BPB_NumFATs: %d\n", info->bsbpb.BPB_NumFATs);
	printf("BPB_RootEntCnt: %d\n", info->bsbpb.BPB_RootEntCnt);
	printf("BPB_TotSecS: %d\n", info->bsbpb.BPB_TotSecS);
	printf("BPB_Media: %02x\n", info->bsbpb.BPB_Media);
	printf("BPB_FATSz16: %d\n", info->bsbpb.BPB_FATSz16);
	printf("BPB_SecPerTrk: %d\n", info->bsbpb.BPB_SecPerTrk);
	printf("BPB_NumHeads: %d\n", info->bsbpb.BPB_NumHeads);
	printf("BPB_HiddSec: %d\n", info->bsbpb.BPB_HiddSec);
	printf("BPB_TotSecL: %d\n", info->bsbpb.BPB_TotSecL);

	if ((info->type == FAT16) || (info->type == FAT12)) {
		printf(" BS_DrvNum: %d\n", info->bsbpb.fat.BS_DrvNum);
		printf(" BS_Reserved1: \n");
		printf(" BS_BootSig: %d\n", info->bsbpb.fat.BS_BootSig);
		printf(" BS_VolID: %d\n", info->bsbpb.fat.BS_VolID);
		fat_dumpstr(" BS_VolLab", (char *)info->bsbpb.fat.BS_VolLab, 11, 0);
		fat_dumpstr(" BS_FilSysType", (char *)info->bsbpb.fat.BS_FilSysType, 8, 0);
	}
	else {
		printf(" BPB_FATSz32: %d\n", info->bsbpb.fat32.BPB_FATSz32);
		printf(" BPB_FSVer: %d\n", info->bsbpb.fat32.BPB_FSVer);
		printf(" BPB_RootClus: %d\n", info->bsbpb.fat32.BPB_RootClus);
		printf(" BPB_FSInfo: %d\n", info->bsbpb.fat32.BPB_FSInfo);
		printf(" BPB_BkBootSec: %d\n", info->bsbpb.fat32.BPB_BkBootSec);
		printf(" BPB_Reserved:\n");
		printf(" BS_DrvNum: %d\n", info->bsbpb.fat32.BS_DrvNum);
		printf(" BS_Reserved1: \n");
		printf(" BS_BootSig: %d\n", info->bsbpb.fat32.BS_BootSig);
		printf(" BS_VolID: %d\n", info->bsbpb.fat32.BS_VolID);

		fat_dumpstr(" BS_VolLab", (char *)info->bsbpb.fat32.BS_VolLab, 11, 0);
		fat_dumpstr(" BS_FilSysType", (char *)info->bsbpb.fat32.BS_FilSysType, 8, 0);
	}

	printf("\nFAT driver parameters\n");

	printf(" off: %d\n", info->off);
	printf(" end: %d\n", info->end);
	printf(" fatoff: %d\n", info->fatoff);
	printf(" fatend: %d\n", info->fatend);
	printf(" rootoff: %d\n", info->rootoff);
	printf(" dataoff: %d\n", info->dataoff);
	printf(" dataend: %d\n", info->dataend);
	printf(" clusters: %d\n", info->clusters);

	printf("\n 1st FAT");

	for (i = 0;; i++) {

		if (fatfat_get(info, i, &next) < 0)
			break;

		if (!(i % 8))
			printf("\n %08x:", i);

		if (next == 0xfffffff)
			printf("[xxxxxxxx] ");
		else if (next == 0)
			printf("[        ] ");
		else
			printf("[%8x] ", next); 
	}
	printf("\n");

	return;
}


int fat_list(fat_info_t *info, const char *path, unsigned int off, unsigned int size, char dump)
{
	fatfat_chain_t c;
	const u16 *n;
	unsigned int k, r, first;
	int ret;
	char buff[512];
	fat_dirent_t d, *tmpd;
	fat_name_t name;
	s32 u;

	if (fatio_lookup(info, path, &d, NULL) < 0) {
		printf("No such file or directory\n");
		return -ENOENT;
	}

	if (d.attr & 0x10) {
		if (dump)
			printf("Directory %s found\n", path);
	} else {
		if (dump)
			printf("File %s with size %u found\n", path, d.size);
		if (size == 0)
			size = d.size;
		if (size + off > d.size)
			size = d.size - off;
		if (off >= size)
			return EOK;
		r = off;
	}

	c.start = 0;
	c.soff = 0;
	c.scnt = 0;
	setlocale(LC_ALL, "");
	name[0] = 0;
	first = 1;
	for (r = 0; (d.attr & 0x10) || (size != r); r += ret) {
		ret = fatio_read(info, ((int) d.clusterL) | (((int) d.clusterH) << 16), &c, r + off, (d.attr & 0x10) ? sizeof(buff) : min(sizeof(buff), size - r), buff);
		if (ret < 0)
			return ret;
		if ((d.attr & 0x10) && (dump != 2)) {
			for (tmpd = (fat_dirent_t *) buff; (char *) tmpd < ret + buff; tmpd++) {
				if (tmpd->attr == 0x0F) { /* long file name (LFN) data */
					fatio_makename(tmpd, &name);
					continue;
				}
				if (tmpd->name[0] == 0x00) {
					printf("\n");
					return EOK;
				}
				if ((tmpd->name[0] == 0xE5) || (tmpd->attr & 0x08)) {
					name[0] = 0;
					continue;
				} else if (first)
					first = 0;
				else
					printf("%c",0x0A);
				fatio_makename(tmpd, &name);
				for (n = name, u = UTF16toUnicode(&n); u != 0; u = UTF16toUnicode(&n))
					printf("%lc", u);
				name[0] = 0;
			}
		} else {
			for (k = 0; k < ret; k++) {
				if (dump) {
					if (!(k % 64))
						printf("\n");
					printf("%c", (isalnum(buff[k]) ? buff[k] : '.'));
				} else
					printf("%c", buff[k]);
			}
		}
		if ((d.attr & 0x10) && (ret < sizeof(buff)))
			break;
	}
	if ((d.attr & 0x10) || dump)
		printf("\n");
	return EOK;
}


int main(int argc, char *argv[])
{
	fat_info_t info;
	int err;
	unsigned int i, fo, fs;
	char *path;
	clock_t b, e;

	if (argc < 4) {
		fprintf(stderr, "To few parameters. Usage: fat <file> <offset> {dump|ls|perf|read} [path] [file_offset] [file_dump_size]\n");
		return -EINVAL;
	}

	if ((err = fat_init(argv[1], atoi(argv[2]), &info)) < 0) {
		fprintf(stderr, "Can't initialize FAT volue (%d)!\n", err);
		return err;
	}

	b = clock();

	if (!strcmp(argv[3], "dump"))
		fat_dumpinfo(&info);

	else if(!strcmp(argv[3], "ls")) {
		path = (argc < 5) ? "/" : argv[4];
		fat_list(&info, path, 0, 0, 1);
	}

	else if(!strcmp(argv[3], "cat")) {
		path = (argc < 5) ? "/" : argv[4];
		fo = (argc < 6) ? 0 : atoi(argv[5]);
		fs = (argc < 7) ? 0 : atoi(argv[6]);
		fat_list(&info, path, fo, fs, 2);
	}

	else if(!strcmp(argv[3], "test")) {
		path = (argc < 5) ? "/" : argv[4];
		fo = (argc < 6) ? 0 : atoi(argv[5]);
		fs = (argc < 7) ? 0 : atoi(argv[6]);
		fat_list(&info, path, fo, fs, 0);
	}

	else if (!strcmp(argv[3], "perf")) {
		for (i = 0; i < 64; i++) {
			printf("dirent[%d]\n", i);
			fat_list(&info, "/", 0, 0, 0);
		}
	}

	e = clock();
	fprintf(stderr, "\nexecution time: %d [us]\n", (unsigned int)((unsigned int)(e - b) * 1000000 / CLOCKS_PER_SEC));

	return EOK;
}
