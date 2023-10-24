/*
 * Phoenix-RTOS
 *
 * FAT filesystem driver
 *
 * Filesystem structures and operations header file
 *
 * Copyright 2012, 2023 Phoenix Systems
 * Author: Pawel Pisarczyk, Jacek Maksymowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _FATIO_H_
#define _FATIO_H_

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/minmax.h>
#include <sys/rb.h>
#include <errno.h>
#include <time.h>

#include <storage/storage.h>
#include "fatstructs.h"

#define FATFS_DEBUG 0

#define FAT_CHAIN_AREAS  8 /* Number of contiguous areas that can be cached at once */
#define FAT_ROOT_ID      UINT64_MAX
#define ROOT_DIR_CLUSTER 0
#define NO_LFN_BIT       (1U << 31)

enum FAT_FILE_TIMES {
	FAT_FILE_MTIME,
	FAT_FILE_CTIME,
	FAT_FILE_ATIME,
};


/* To optimize reads, contiguous areas are parsed out of the FAT chain
 * and cached within this data structure.
 */
typedef struct {
	fat_cluster_t chainStart;     /* Cluster where this FAT chain stats */
	fat_cluster_t nextAfterAreas; /* First cluster past the end of this data chunk */
	fat_sector_t areasOffset;     /* Offset in FAT chain where this data chunk begins */
	fat_sector_t areasLength;     /* Length of this data chunk */
	struct {
		fat_sector_t start;
		fat_sector_t size;
	} areas[FAT_CHAIN_AREAS]; /* Contiguous areas making up this data chunk */
} fatchain_cache_t;


typedef struct _fat_info_t {
	storage_t *strg;
	unsigned int port;
	uint16_t fsPermissions;

	fat_type_t type;
	fat_bsbpbUnpacked_t bsbpb;

	offs_t fatoffBytes;         /* Start of first FAT (in bytes) */
	fat_sector_t rootoff;       /* Start of root directory (for FAT12/16) */
	fat_sector_t dataoff;       /* Start of data space */
	fat_cluster_t dataClusters; /* Total clusters in data space */
	fat_cluster_t clusters;     /* Total clusters on drive */

	rbtree_t openObjs; /* Tree of open objects */
	handle_t objLock;  /* Lock for object add/remove/lookup operations */
} fat_info_t;


typedef union {
	struct {
		uint32_t offsetInDir;
		fat_cluster_t dirCluster;
	} __attribute__((packed));
	uint64_t raw;
} fat_fileID_t;


typedef struct {
	uint16_t chars[FAT_MAX_NAMELEN + 1];
	uint32_t lfnRemainingBits;
	uint8_t checksum;
} fat_name_t;


static inline void fat_initFatName(fat_name_t *name)
{
	name->chars[0] = 0;
	name->lfnRemainingBits = NO_LFN_BIT;
}


static inline fat_cluster_t fat_getCluster(fat_dirent_t *dirent, fat_type_t type)
{
	uint32_t clusterH = (type == FAT32) ? dirent->clusterH : 0;
	return (clusterH << 16) | dirent->clusterL;
}


static inline void fat_setCluster(fat_dirent_t *dirent, fat_cluster_t cluster)
{
	dirent->clusterL = cluster & 0xffff;
	dirent->clusterH = cluster >> 16;
}


static inline bool fat_isDirectory(fat_dirent_t *dirent)
{
	return (dirent->attr & FAT_ATTR_DIRECTORY) != 0;
}


static inline bool fat_isDeleted(fat_dirent_t *dirent)
{
	return dirent->name[0] == 0xe5;
}


static inline bool fat_isDirentNull(fat_dirent_t *dirent)
{
	return dirent->name[0] == 0;
}


extern time_t fatdir_getFileTime(fat_dirent_t *d, enum FAT_FILE_TIMES type);


/* Extract name or part of name (for LFN scheme) from directory entry */
extern bool fatdir_extractName(fat_dirent_t *d, fat_name_t *n);


/* Returns size of resulting UTF-8 string with null terminator. out can be NULL. */
extern ssize_t fatdir_nameToUTF8(fat_name_t *name, char *out, size_t outSize);


/* d == NULL means directory scan has finished */
typedef int (*fat_dirScanCb_t)(void *arg, fat_dirent_t *d, fat_name_t *name, uint32_t offsetInDir);


extern int fatio_dirScan(fat_info_t *info, fatchain_cache_t *c, uint32_t offset, fat_dirScanCb_t cb, void *cbArg);


extern ssize_t fatio_read(fat_info_t *info, fatchain_cache_t *c, offs_t offset, size_t size, void *buff);


/* Lookup path from root to end (d is output only) */
extern int fatio_lookupPath(fat_info_t *info, const char *path, fat_dirent_t *d, fat_fileID_t *id);


/* Lookup path from from d until end (d is input and output) */
extern int fatio_lookupUntilEnd(fat_info_t *info, const char *path, fat_dirent_t *d, fat_fileID_t *id);


/* Lookup one element of the path, return the number of characters consumed from path or < 0 */
extern ssize_t fatio_lookupOne(fat_info_t *info, const char *path, fat_dirent_t *d, fat_fileID_t *id);


extern int fat_readFilesystemInfo(fat_info_t *info);


extern void fat_printFilesystemInfo(fat_info_t *info, bool printFat);

#endif /* _FATIO_H_ */
