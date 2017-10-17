/*
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * Meterfs
 *
 * Copyright 2017 Phoenix Systems
 * Author: Aleksander Kaminski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <sys/threads.h>
#include <sys/msg.h>
#include <string.h>
#include "spi.h"
#include "flash.h"


#define CEIL(value, size)       ((((value) + (size) - 1) / (size)) * (size))
#define ENTRIES(f)              (f->filesz / (f->recordsz + sizeof(entry_t)))
#define IS_NEXT_ID(next, prev)  (((unsigned int)next.no == (((unsigned int)prev.no + 1) & 0x7fffffff)) || (next.no == prev.no))
#define MAX_FILE_CNT            255

#ifndef NDEBUG
#define DEBUG(fmt, ...) do { \
	printf("%s:%d:%s(): " fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
} while (0)
#else
#define DEBUG(fmt, ...)
#endif


typedef struct {
	unsigned int valid:1;
	unsigned int no:31;
} __attribute__((packed)) index_t;


typedef struct {
	unsigned int sector;
	size_t filesz;
	size_t recordsz;
	char name[8];
	index_t curridx;
	unsigned int offset;
	char reserved[4];
} __attribute__((packed)) file_t;


typedef struct {
	index_t id;
	size_t filecnt;
	char reserved[24];
} __attribute__((packed)) header_t;


typedef struct {
	index_t id;
	char data[];
} __attribute__((packed)) entry_t;


struct {
	unsigned int port;
	unsigned int h1Addr;
	unsigned int hcurrAddr;
	unsigned int filecnt;
	size_t sectorsz;
	size_t flashsz;
} meterfs_common;


void meterfs_eraseFileTable(unsigned int n)
{
	unsigned int sector, i;
	size_t sectorcnt;

	if (n != 0 && n != 1)
		return;

	sector = (n == 0) ? 0 : meterfs_common.h1Addr / meterfs_common.sectorsz;
	sectorcnt = sizeof(header_t) + (meterfs_common.filecnt * sizeof(file_t));
	sectorcnt = CEIL(sectorcnt, meterfs_common.sectorsz);

	for (i = 0; i < sectorcnt; ++i)
		flash_eraseSector(sector + i);
}


void meterfs_readActiveHeader(header_t *h)
{
	if (h != NULL) {
		flash_read(meterfs_common.hcurrAddr, h, sizeof(*h));
	}
}


int meterfs_checkfs(void)
{
	unsigned int addr = 0, valid0 = 0, valid1 = 0, maxSector, src, dst, i;
	header_t h;
	file_t f;
	index_t id;

	/* Check if first header is valid */
	flash_read(addr, &h, sizeof(h));
	if (!h.id.valid) {
		valid0 = 1;
		id = h.id;
		addr = sizeof(h) + (h.filecnt * sizeof(file_t));
		addr = CEIL(addr, meterfs_common.sectorsz);
		meterfs_common.filecnt = h.filecnt;
		DEBUG("First filetable is valid.");
	}

	/* Check next header */
	if (!addr) {
		DEBUG("Could not establish filetable copy address.");
		maxSector = ((MAX_FILE_CNT * sizeof(file_t)) + sizeof(header_t)) / meterfs_common.sectorsz;
		for (addr = 0; addr <= (maxSector * meterfs_common.sectorsz); addr += meterfs_common.sectorsz) {
			flash_read(addr, &h, sizeof(h));
			if (!h.id.valid) {
				valid1 = 1;
				meterfs_common.filecnt = h.filecnt;
				DEBUG("Found copy and it's valid");
				break;
			}
		}
	}
	else {
		flash_read(addr, &h, sizeof(h));
		if (!h.id.valid) {
			valid1 = 1;
			meterfs_common.filecnt = h.filecnt;
			DEBUG("Filetable copy is valid");
		}
	}

	meterfs_common.h1Addr = addr;
	DEBUG("Filetable copy address: 0x%p", meterfs_common.h1Addr);

	if (!valid0 && !valid1) {
		printf("meterfs: Filesystem is corrupted beyond repair.\n");
		return -1;
	}

	/* Select active header and files table */
	if (!valid1 || ((!h.id.valid && !id.valid) && IS_NEXT_ID(id, h.id)))
		meterfs_common.hcurrAddr = 0;
	else
		meterfs_common.hcurrAddr = meterfs_common.h1Addr;

	meterfs_readActiveHeader(&h);
	meterfs_common.filecnt = h.filecnt;

	DEBUG("Selected header 0x%p, idx0: %d, idx1: %d, filecnt: %d", meterfs_common.hcurrAddr, h.id.no, id.no, h.filecnt);

	/* There should be copy of file table at all times. Fix it if necessary */
	if (!valid0 || !valid1) {
		DEBUG("Repairing filetable: %d", valid0 ? 1 : 0);
		if (!valid0) {
			src = meterfs_common.h1Addr;
			dst = 0;
			meterfs_eraseFileTable(0);
		}
		else {
			src = 0;
			dst = meterfs_common.h1Addr;
			meterfs_eraseFileTable(1);
		}

		/* Copy header */
		flash_read(src, &h, sizeof(h));
		flash_write(dst, &h, sizeof(h));

		src += sizeof(h);
		dst += sizeof(h);

		/* Copy file info */
		for (i = 0; i < meterfs_common.filecnt; ++i) {
			flash_read(src, &f, sizeof(f));
			flash_write(dst, &f, sizeof(f));

			src += sizeof(f);
			dst += sizeof(f);
		}
	}

	return 0;
}


int meterfs_readFileInfo(unsigned int n, file_t *f)
{
	if (f == NULL || n >= meterfs_common.filecnt)
		return -1;

	flash_read(meterfs_common.hcurrAddr + sizeof(header_t) + (n * sizeof(file_t)), f, sizeof(file_t));

	return 0;
}


int meterfs_getFileInfo(const char *name, file_t *f)
{
	header_t header;
	file_t file;
	size_t i;

	if (name == NULL || f == NULL)
		return -1;

	flash_read(meterfs_common.hcurrAddr, &header, sizeof(header));

	for (i = 0; i < header.filecnt; ++i) {
		flash_read(meterfs_common.hcurrAddr + sizeof(header) + (sizeof(file) * i), &file, sizeof(file));
		if (strcmp(name, file.name) == 0) {
			memcpy(f, &file, sizeof(file));
			return 0;
		}
	}

	return -1;
}


int meterfs_setFileInfo(file_t *f)
{
	header_t h;
	file_t t;
	unsigned int headerOld, headerNew, i, offset;

	if (f == NULL)
		return -1;

	/* Check if file exist */
	if (meterfs_getFileInfo(f->name, &t) != 0)
		return -1;

	headerOld = meterfs_common.hcurrAddr;
	headerNew = (meterfs_common.hcurrAddr == meterfs_common.h1Addr) ? 0 : meterfs_common.h1Addr;

	/* Make space for new file table */
	meterfs_eraseFileTable((headerNew == 0) ? 0 : 1);

	for (i = 0, offset = sizeof(header_t); i < meterfs_common.filecnt; ++i) {
		flash_read(headerOld + offset, &t, sizeof(file_t));
		if (strcmp(f->name, t.name) == 0)
			flash_write(headerNew + offset, f, sizeof(file_t));
		else
			flash_write(headerNew + offset, &t, sizeof(file_t));

		offset += sizeof(file_t);
	}

	/* Write new header */
	meterfs_readActiveHeader(&h);
	++h.id.no;

	flash_write(headerNew, &h, sizeof(header_t));

	/* Use new header from now on */
	meterfs_common.hcurrAddr = headerNew;

	return 0;
}


void meterfs_getFilePos(file_t *f)
{
	unsigned int i, addr;
	entry_t e;

	if (f == NULL)
		return;

	f->offset = 0;
	f->curridx.no = 0;
	f->curridx.valid = 0;

	for (i = 0, addr = f->sector * meterfs_common.sectorsz; i < ENTRIES(f); ++i, addr += (f->recordsz + sizeof(entry_t))) {
		flash_read(addr, &e, sizeof(e));
		if (!e.id.valid && IS_NEXT_ID(e.id, f->curridx)) {
			f->curridx = e.id;
			f->offset = addr;
		}
	}
}


int meterfs_writeRecord(file_t *f, void *buff)
{
	/* This function assumes that f contains valid curridx and offset */
	size_t sectorcnt;
	entry_t e;
	unsigned int offset, sector;

	if (f == NULL || buff == NULL)
		return -1;

	sectorcnt = CEIL(f->filesz, meterfs_common.sectorsz) + 1;

	if (f->offset + f->recordsz + sizeof(entry_t) > sectorcnt * meterfs_common.sectorsz) {
		/* Wrap-around */
		offset = 0;
	}
	else {
		offset = f->offset;
	}

	/* Check if we have to erase sector to write new data */
	if (!flash_regionIsBlank(f->sector * meterfs_common.sectorsz + offset, f->recordsz + sizeof(entry_t))) {
		/* Find sector to erase and erase it */
		sector = f->sector * meterfs_common.sectorsz + offset + f->recordsz + sizeof(entry_t);
		sector /= meterfs_common.sectorsz;
		flash_eraseSector(sector);
	}

	e.id.no = f->curridx.no + 1;
	e.id.valid = 0;

	flash_write(f->sector * meterfs_common.sectorsz + offset + sizeof(entry_t), buff, f->recordsz);
	flash_write(f->sector * meterfs_common.sectorsz + offset, &e, sizeof(entry_t));

	f->offset = offset + f->recordsz + sizeof(entry_t);
	f->curridx.no += 1;

	return f->recordsz;
}


int main(void)
{
	header_t header;

	meterfs_common.sectorsz = 4 * 1024;
	meterfs_common.flashsz = 2 * 1024 * 1024;

	spi_init();
	DEBUG("SPI init done");

	flash_init(meterfs_common.flashsz, meterfs_common.sectorsz);
	DEBUG("Flash init done");
/*
	if (meterfs_checkfs()) {
		for (;;)
			usleep(10000);
	}
*/


#if 1
	unsigned char id[3];

	printf("Init done.\n");

	for(;;) {
		spi_transaction(cmd_jedecid, 0, spi_read, id, 3);
		printf("ID: 0x%02x 0x%02x 0x%02x\n", id[0], id[1], id[2]);
		usleep(2000000);
	}
#endif

	/* TODO */
	for (;;)
		usleep(10000 * 1000);
}

