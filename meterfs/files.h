/*
 * Phoenix-RTOS
 *
 * Files definitions
 *
 * Copyright 2017, 2018 Phoenix Systems
 * Author: Aleksander Kaminski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _METERFS_FILES_H_
#define _METERFS_FILES_H_

#include <stdint.h>

#define HGRAIN            32u /* Must be able divide sector size */
#define HEADER_SECTOR_CNT 2u
#define HEADER_SIZE(ssz)  (HEADER_SECTOR_CNT * (ssz))
#define MAX_FILE_CNT(ssz) ((HEADER_SIZE(ssz) - HGRAIN) / HGRAIN)


typedef struct {
	unsigned int nvalid:1;
	unsigned int no:31;
} __attribute__((packed)) index_t;


typedef struct {
	unsigned int sector;
	uint32_t sectorcnt;
	uint32_t filesz;
	uint32_t recordsz;
	char name[8];
} __attribute__((packed)) fileheader_t;


typedef struct {
	index_t id;
	uint32_t filecnt;
	uint32_t checksum;
	unsigned char magic[4];
} __attribute__((packed)) header_t;


typedef struct {
	index_t id;
	uint32_t checksum;
	unsigned char data[];
} __attribute__((packed)) entry_t;


typedef struct {
	fileheader_t header;
	index_t lastidx;
	unsigned int lastoff;
	index_t firstidx;
	unsigned int firstoff;
	unsigned int recordcnt;
} file_t;


#endif
