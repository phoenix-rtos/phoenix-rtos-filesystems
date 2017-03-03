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


static int fat_readsuper(void *opt, fat_info_t **out)
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

	info->dataoff = info->fatend + (info->fatend - info->fatoff) * (info->bsbpb.BPB_NumFATs - 1);
	info->dataoff += (info->bsbpb.BPB_RootEntCnt >> 4);
	info->dataend = info->dataoff;
	info->dataend += info->bsbpb.BPB_FATSz16 ? info->bsbpb.BPB_TotSec16 : info->bsbpb.BPB_TotSec32;
  
	info->end = info->off + info->dataend;

	info->clusters = (info->dataend - info->dataoff) / info->bsbpb.BPB_SecPerClus;
  
	/* Check FAT type */
	info->type = FAT32;
	if (info->clusters < 4085)
		info->type = FAT12;
	else if (info->clusters < 65525)
		info->type = FAT16;
				
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


int fat_init(const char *name, unsigned int off, fat_info_t **out)
{
	fat_opt_t opt;
	int err;
  
	if ((opt.dev = fopen(name, "r+")) == NULL)
		return ERR_NOENT;

	opt.off = off;

	if ((err = fat_readsuper(&opt, out)) < 0) {
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
  printf("BPB_TotSec16: %d\n", info->bsbpb.BPB_TotSec16);
  printf("BPB_Media: %02x\n", info->bsbpb.BPB_Media);
  printf("BPB_FATSz16: %d\n", info->bsbpb.BPB_FATSz16);
  printf("BPB_SecPerTrk: %d\n", info->bsbpb.BPB_SecPerTrk);
  printf("BPB_NumHeads: %d\n", info->bsbpb.BPB_NumHeads);
  printf("BPB_HiddSec: %d\n", info->bsbpb.BPB_HiddSec);
  printf("BPB_TotSec32: %d\n", info->bsbpb.BPB_TotSec32);
    
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


int fat_list(fat_info_t *info, const char *path, char dump)
{
	fatfat_chain_t c;
	unsigned int i, k;
	u8 buff[SIZE_SECTOR * 32];

	c.start = info->bsbpb.fat32.BPB_RootClus;

	for (;;) {
   
		if (fatfat_lookup(info, &c) < 0)
			return ERR_NOENT;

		if (dump)
			printf("c.start: %d\n", c.start);

		for (i = 0; i < SIZE_CHAIN_AREAS; i++) {
			if (!c.areas[i].start)
				break;

			if (dump)
				printf("c.areas[%d].start: %d+%d\n", i, c.areas[i].start, c.areas[i].size);
  
			if (fatdev_read(info, c.areas[i].start, min(c.areas[i].size, sizeof(buff) / SIZE_SECTOR), (char *)buff))
				return ERR_PROTO;

			if (dump) {
				for (k = 0; k < sizeof(buff); k++) {
					if (!(k % 64))
						printf("\n");
					printf("%c", (isalnum(buff[k]) ? buff[k] : '.'));
				}
				printf("\n");
			}
		}

		if (c.start == FAT_EOF)
			break;
	}
  
	return ERR_NONE;
}


int main(int argc, char *argv[])
{
	fat_info_t *info = NULL;
	int err;
	unsigned int i;
	clock_t b, e;

	if (argc < 4) {
		fprintf(stderr, "To few parameters. Usage: fat <file> <offset> {dump|ls|perf|read} [path]\n");
		return ERR_ARG;
	}
    
	if ((err = fat_init(argv[1], atoi(argv[2]), &info)) < 0) {
		fprintf(stderr, "Can't initialize FAT volue (%d)!\n", err);
		return err;
	}
  
	b = clock();
  
	if (!strcmp(argv[3], "dump"))
		fat_dumpinfo(info);

	else if(!strcmp(argv[3], "ls"))
		fat_list(info, "/", 1);

	else if (!strcmp(argv[3], "perf")) {
		for (i = 0; i < 64; i++) {
			printf("dirent[%d]\n", i);
			fat_list(info, "/", 0);
		}
	}
  
	e = clock();
	printf("\nexecution time: %d [us]\n", (unsigned int)(e - b) * 1000000 / CLOCKS_PER_SEC);
  
	return ERR_NONE;
}
