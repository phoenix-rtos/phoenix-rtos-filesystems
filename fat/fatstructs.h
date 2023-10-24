/*
 * Phoenix-RTOS
 *
 * FAT filesystem driver
 *
 * Filesystem data structure definitions
 *
 * Copyright 2012, 2023 Phoenix Systems
 * Author: Pawel Pisarczyk, Jacek Maksymowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */


#ifndef _FATSTRUCTS_H_
#define _FATSTRUCTS_H_

#include <stdint.h>

typedef struct _fat_bsbpb_t {
	uint8_t BS_jmpBoot[3];
	uint8_t BS_OEMName[8];
	uint16_t BPB_BytesPerSec;
	uint8_t BPB_SecPerClus;
	uint16_t BPB_RsvdSecCnt;
	uint8_t BPB_NumFATs;
	uint16_t BPB_RootEntCnt;
	uint16_t BPB_TotSecS;
	uint8_t BPB_Media;
	uint16_t BPB_FATSz16;
	uint16_t BPB_SecPerTrk;
	uint16_t BPB_NumHeads;
	uint32_t BPB_HiddSec;
	uint32_t BPB_TotSecL;

	union {
		/* FAT12 and FAT16 */
		struct {
			uint8_t BS_DrvNum;
			uint8_t BS_Reserved1;
			uint8_t BS_BootSig;
			uint32_t BS_VolID;
			uint8_t BS_VolLab[11];
			char BS_FilSysType[8];

			uint8_t padding[450];
		} __attribute__((packed)) fat;

		/* FAT32 */
		struct {
			uint32_t BPB_FATSz32;
			uint16_t BPB_ExtFlags;
			uint16_t BPB_FSVer;
			uint32_t BPB_RootClus;
			uint16_t BPB_FSInfo;
			uint16_t BPB_BkBootSec;
			uint8_t BPB_Reserved[12];

			uint8_t BS_DrvNum;
			uint8_t BS_Reserved1;
			uint8_t BS_BootSig;
			uint32_t BS_VolID;
			uint8_t BS_VolLab[11];
			char BS_FilSysType[8];

			uint8_t padding[422];
		} __attribute__((packed)) fat32;
	};
} __attribute__((packed)) fat_bsbpb_t;


typedef struct _fat_fsinfo_t {
	uint32_t FSI_LeadSig;
	uint32_t FSI_Reserved1[480];
	uint32_t FSI_StrucSig;
	uint32_t FSI_Free_Count;
	uint32_t FSI_Nxt_Free;
	uint8_t FSI_Reserved2[12];
} __attribute__((packed)) fat_fsinfo_t;


typedef struct {
	uint32_t BS_VolID;
	uint32_t BPB_TotSecL;
	uint32_t BPB_HiddSec;
	uint16_t BPB_BytesPerSec;
	uint16_t BPB_RsvdSecCnt;
	uint16_t BPB_RootEntCnt;
	uint16_t BPB_TotSecS;
	uint16_t BPB_FATSz16;
	uint8_t BPB_SecPerClus;
	uint8_t BPB_NumFATs;
	uint8_t BPB_Media;
	uint8_t BS_BootSig;
	uint8_t BS_DrvNum;
	char BS_VolLab[11];
	char BS_FilSysType[8];
	char BS_OEMName[8];
	struct {
		uint32_t BPB_FATSz32;
		uint32_t BPB_RootClus;
		uint16_t BPB_ExtFlags;
		uint16_t BPB_FSVer;
		uint16_t BPB_FSInfo;
		uint16_t BPB_BkBootSec;
	} fat32;
} fat_bsbpbUnpacked_t;


typedef struct _fat_dirent_t {
	union {
		/* NOTE: The structure below assumes a DOS 7.0 VFAT-compatible file system,
		 * not one of the countless extensions made by different DOS implementations over the years
		 */
		struct {
			union {
				struct {
					uint8_t name[8];
					uint8_t ext[3];
				} __attribute__((packed));
				uint8_t nameExt[11];
			};
			uint8_t attr;
			uint8_t ntCase;   /* Lowercase name/extension flags used by Windows NT */
			uint8_t ctime_ms; /* Unit is 10 ms */
			uint16_t ctime;
			uint16_t cdate;
			uint16_t adate;
			uint16_t clusterH; /* Only valid under FAT32 */
			uint16_t mtime;
			uint16_t mdate;
			uint16_t clusterL;
			uint32_t size;
		} __attribute__((packed));
		struct {
			uint8_t no;
			uint16_t lfn1[5];
			uint8_t attr2;
			uint8_t type;
			uint8_t cksum;
			uint16_t lfn2[6];
			uint16_t zero;
			uint16_t lfn3[2];
		} __attribute__((packed));
	};
} __attribute__((packed)) fat_dirent_t;


#define FAT_ATTR_READ_ONLY (1 << 0)
#define FAT_ATTR_HIDDEN    (1 << 1)
#define FAT_ATTR_SYSTEM    (1 << 2)
#define FAT_ATTR_VOLUME_ID (1 << 3)
#define FAT_ATTR_DIRECTORY (1 << 4)
#define FAT_ATTR_ARCHIVE   (1 << 5)
#define FAT_ATTR_LFN       (FAT_ATTR_READ_ONLY | FAT_ATTR_HIDDEN | FAT_ATTR_SYSTEM | FAT_ATTR_VOLUME_ID)

#define FAT_NTCASE_NAME_LOWER 0x08
#define FAT_NTCASE_EXT_LOWER  0x10

#define FAT_MAX_NAMELEN 255

#define FAT_EOF 0x0fffffff

typedef enum {
	FAT12 = 0,
	FAT16,
	FAT32
} fat_type_t;

typedef uint32_t fat_cluster_t;
typedef uint32_t fat_sector_t;

#endif /* _FATSTRUCTS_H_ */
