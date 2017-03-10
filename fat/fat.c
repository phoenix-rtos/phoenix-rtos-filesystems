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


#define min(a, b) ({__typeof__(a) _a = (a); \
	__typeof__(b) _b = (b); \
	_a < _b ? _a : _b;})


int fat_init(const char *name, unsigned int off, fat_info_t **out)
{
	fat_opt_t opt;
	int err;

	if ((opt.dev = fopen(name, "r+")) == NULL)
		return ERR_NOENT;

	opt.off = off;

	if ((err = fatio_readsuper(&opt, out)) < 0) {
		fclose(opt.dev);
		return err;
	}

	return ERR_NONE;
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


static void fat_dumpdirent(fat_dirent_t *d)
{
	if (d->attr != 0x0F) {
		if (d->name[0] == 0xE5) {
			printf("deleted file\n");
			return;
		}
		printf("name: %c%.7s.%.3s\n", (d->name[0] == 0x05) ? 0xE5 : d->name[0], d->name + 1, d->ext);
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
		printf("\n");
		printf("mtime: %d\n", d->mtime);
		printf("mdate: %d\n", d->mdate);
		printf("start cluster: %d\n", d->cluster);
		printf("size: %d\n", d->size);
	} else
		printf("LFN no %u\n", d->no);
}


int fat_list(fat_info_t *info, const char *path, unsigned int off, unsigned int size, char dump)
{
	fatfat_chain_t c;
	unsigned int k, r;
	int ret;
	char buff[32 * 6];
	fat_dirent_t d, *tmpd;

	if (fatio_lookup(info, path, &d) < 0) {
		printf("No such file or directory\n");
		return ERR_NOENT;
	}

	if (d.attr & 0x10) {
		printf("Directory %s found\n", path);
	} else {
		printf("File %s with size %u found\n", path, d.size);
		if (size == 0)
			size = d.size;
		if (size + off > d.size)
			size = d.size - off;
		if (off >= size)
			return ERR_NONE;
		r = off;
	}

	c.start = d.cluster;
	c.soff = 0;
	c.scnt = 0;

	for (r = 0; (d.attr & 0x10) || (size != r); r += ret) {
		ret = fatio_read(info, &d, &c, r + off, (d.attr & 0x10) ? sizeof(buff) : min(sizeof(buff), size - r), buff);
		if (ret < 0)
			return ret;
		if (dump) {
			if (d.attr & 0x10) {
				for (tmpd = (fat_dirent_t *) buff; (char *) tmpd < ret + buff; tmpd++) {
					if (tmpd->name[0] == 0) {
						return ERR_NONE;
					}
					fat_dumpdirent(tmpd);
					printf("\n");
				}
			} else {
				for (k = 0; k < ret; k++) {
					if (!(k % 64))
						printf("\n");
					printf("%c", (isalnum(buff[k]) ? buff[k] : '.'));
				}
			}
		}
		if ((d.attr & 0x10) && (ret < sizeof(buff)))
			break;
	}
	printf("\n");
	return ERR_NONE;
}


int main(int argc, char *argv[])
{
	fat_info_t *info = NULL;
	int err;
	unsigned int i, fo, fs;
	char *path;
	clock_t b, e;

	if (argc < 4) {
		fprintf(stderr, "To few parameters. Usage: fat <file> <offset> {dump|ls|perf|read} [path] [file_offset] [file_dump_size]\n");
		return ERR_ARG;
	}

	if ((err = fat_init(argv[1], atoi(argv[2]), &info)) < 0) {
		fprintf(stderr, "Can't initialize FAT volue (%d)!\n", err);
		return err;
	}

	b = clock();

	if (!strcmp(argv[3], "dump"))
		fat_dumpinfo(info);

	else if(!strcmp(argv[3], "ls")) {
		path = (argc < 5) ? "/" : argv[4];
		fo = (argc < 6) ? 0 : atoi(argv[5]);
		fs = (argc < 7) ? 0 : atoi(argv[6]);
		fat_list(info, path, fo, fs, 1);
	}

	else if (!strcmp(argv[3], "perf")) {
		for (i = 0; i < 64; i++) {
			printf("dirent[%d]\n", i);
			fat_list(info, "/", 0, 0, 0);
		}
	}

	e = clock();
	printf("\nexecution time: %d [us]\n", (unsigned int)((unsigned int)(e - b) * 1000000 / CLOCKS_PER_SEC));

	return ERR_NONE;
}
