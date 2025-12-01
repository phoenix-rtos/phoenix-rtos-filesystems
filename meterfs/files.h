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
#include <stdbool.h>

#define HGRAIN            32u /* Must be able divide sector size */
#define HEADER_SECTOR_CNT 2u
#define HEADER_SIZE(ssz)  (HEADER_SECTOR_CNT * (ssz))
#define MAX_FILE_CNT(ssz) ((HEADER_SIZE(ssz) - HGRAIN) / HGRAIN)


typedef struct {
	unsigned int nvalid : 1;
	unsigned int no : 31;
} __attribute__((packed)) index_t;

_Static_assert(sizeof(index_t) <= HGRAIN);


typedef struct {
	unsigned int sector;
	uint32_t filesz;
	uint32_t recordsz;
	char name[8];
	uint32_t uid;     /* Unique file id, incremented on file header update */
	uint32_t firstid; /* First entry id of current file contents - any entries with lower id are ignored */
	uint32_t sectorcnt : 17;
	uint32_t ncrypt : 1;
	uint32_t unused : 14; /* Unused bits for future use */
} __attribute__((packed)) fileheader_t;

_Static_assert(sizeof(fileheader_t) <= HGRAIN);


typedef struct {
	index_t id;
	uint32_t filecnt;
	uint32_t checksum;
	unsigned char magic[4];
	uint8_t version;
} __attribute__((packed)) header_t;

_Static_assert(sizeof(header_t) <= HGRAIN);


typedef struct {
	index_t id;
	uint32_t checksum;
	unsigned char data[];
} __attribute__((packed)) entry_t;

_Static_assert(sizeof(entry_t) <= HGRAIN);


typedef struct {
	fileheader_t header;
	index_t lastidx;
	unsigned int lastoff;
	index_t firstidx;
	unsigned int firstoff;
	unsigned int recordcnt;
	bool earlyErased;
} file_t;


#endif
