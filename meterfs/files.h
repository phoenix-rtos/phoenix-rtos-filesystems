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

#define MAX_FILE_CNT 255
#define HGRAIN       32


typedef struct {
	unsigned int nvalid:1;
	unsigned int no:31;
} __attribute__((packed)) index_t;


typedef struct {
	unsigned int sector;
	size_t sectorcnt;
	size_t filesz;
	size_t recordsz;
	char name[8];
} __attribute__((packed)) fileheader_t;


typedef struct {
	index_t id;
	size_t filecnt;
	unsigned char magic[4];
} __attribute__((packed)) header_t;


typedef struct {
	index_t id;
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
