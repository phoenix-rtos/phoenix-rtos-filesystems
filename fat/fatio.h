/*
 * Phoenix-RTOS
 *
 * Misc. FAT
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

#ifndef _MISC_FATIO_H_
#define _MISC_FATIO_H_


#include "types.h"


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
	union {
		struct {
			u8  name[8];
			u8  ext[3];
			u8  attr;
			u8  reserved[8];
			u16 clusterH;
			u16 mtime;
			u16 mdate;
			u16 clusterL;
			u32 size;
		} __attribute__((packed));
		struct {
			u8  no;
			u16 lfn1[5];
			u8  attr2;
			u8  type;
			u8  cksum;
			u16 lfn2[6];
			u16 zero;
			u16 lfn3[2];
		} __attribute__((packed));
	};
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


#define SIZE_CHAIN_AREAS  8
typedef struct _fatfat_chain_t {
	unsigned int start;
	unsigned int soff;
	unsigned int scnt;
	struct {
		unsigned int start;
		unsigned int size;
	} areas[SIZE_CHAIN_AREAS];
} fatfat_chain_t;


typedef struct _fat_name_t {
	u16 name[256];
} __attribute__((packed)) fat_name_t;


extern int fatio_read(fat_info_t *info, fat_dirent_t *d, fatfat_chain_t *c, unsigned int offset, unsigned int size, char * buff);


extern int fatio_lookup(fat_info_t *info, const char *path, fat_dirent_t *d);


extern int fatio_readsuper(void *opt, fat_info_t **out);


extern void fatio_makename(fat_dirent_t *d, fat_name_t *n);


extern void fatio_initname(fat_name_t *n);


#endif

