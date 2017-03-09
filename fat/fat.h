/*
 * Phoenix-RTOS
 *
 * Misc. utilities - FAT
 *
 * FAT implementation
 *
 * Copyright 2012 Phoenix Systems
 * Author: Pawel Pisarczyk
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _MISC_FAT_H_
#define _MISC_FAT_H_

#include <stdio.h>

#include "types.h"

// #define FATDEBUG(...) printf("DBG: " __VA_ARGS__)
#define FATDEBUG(...)


typedef struct _fat_opt_t {
	FILE *dev;
	u32 off;
} fat_opt_t;


typedef struct _fat_bsbpb_t {
	u8 BS_jmpBoot[3];
	u8 BS_OEMName[8];
	u16 BPB_BytesPerSec;
	u8 BPB_SecPerClus;
	u16 BPB_RsvdSecCnt;
	u8 BPB_NumFATs;
	u16 BPB_RootEntCnt;
	u16 BPB_TotSecS;
	u8 BPB_Media;
	u16 BPB_FATSz16;
	u16 BPB_SecPerTrk;
	u16 BPB_NumHeads;
	u32 BPB_HiddSec;
	u32 BPB_TotSecL;

	union {
		/* FAT12 and FAT16 */
		struct {
			u8 BS_DrvNum;
			u8 BS_Reserved1;
			u8 BS_BootSig;
			u32 BS_VolID;
			u8 BS_VolLab[11];
			u8 BS_FilSysType[8];
			
			u8 padding[450];
		} __attribute__((packed)) fat;
		
		/* FAT32 */
		struct {
			u32 BPB_FATSz32;
			u16 BPB_ExtFlags;
			u16 BPB_FSVer;
			u32 BPB_RootClus;
			u16 BPB_FSInfo;
			u16 BPB_BkBootSec;
			u8 BPB_Reserved[12];

			u8 BS_DrvNum;
			u8 BS_Reserved1;
			u8 BS_BootSig;
			u32 BS_VolID;
			u8 BS_VolLab[11];
			u8 BS_FilSysType[8];
			
			u8 padding[422];
		} __attribute__((packed)) fat32;
	};
} __attribute__((packed)) fat_bsbpb_t;


typedef struct _fat_fsinfo_t {
	u32 FSI_LeadSig;
	u32 FSI_Reserved1[480];
	u32 FSI_StrucSig;
	u32 FSI_Free_Count;
	u32 FSI_Nxt_Free;
	u8 FSI_Reserved2[12];
} __attribute__((packed)) fat_fsinfo_t;


typedef struct _fat_dirent_t {
	u8  name[8];
	u8  ext[3];
	u8  attr;
	u8  reserved[10];
	u16 mtime;
	u16 mdate;
	u16 cluster;
	u32 size;
} __attribute__((packed)) fat_dirent_t;


typedef enum { FAT12 = 0, FAT16, FAT32 } fat_type_t;


typedef struct _fat_info_t {
	FILE *dev;

	fat_type_t type;
	fat_bsbpb_t bsbpb;
	fat_fsinfo_t *fsinfo;

	unsigned int off;
	unsigned int end;
	
	unsigned int fatoff;
	unsigned int fatend;
	unsigned int dataoff;
	unsigned int dataend;
	unsigned int rootoff;

	unsigned int clusters;
} fat_info_t;


#endif
