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
#include <sys/pwman.h>
#include <sys/msg.h>
#include <string.h>
#include "spi.h"
#include "flash.h"

#define MAX_FILE_CNT            255
#define TOTAL_SIZE(f)           (((f)->filesz * ((f)->recordsz + sizeof(entry_t))) / (f)->recordsz)
#define SECTORS(f, sectorsz)    ((TOTAL_SIZE(f) + meterfs_common.sectorsz - 1) / sectorsz)
#define IS_NEXT_ID(next, prev)  (((unsigned int)next.no == (((unsigned int)prev.no + 1) & 0x7fffffff)) || (next.no == prev.no))
#define FATAL(fmt, ...) \
	do { \
		printf("meterfs: FATAL: " fmt "\n", ##__VA_ARGS__); \
		for (;;) \
			usleep(10000000); \
	} while (0)

#ifndef NDEBUG
#define DEBUG(fmt, ...) do { \
	printf("%s:%d:%s(): " fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
} while (0)
#else
#define DEBUG(fmt, ...)
#endif


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
	char reserved[8];
} __attribute__((packed)) fileheader_t;


typedef struct {
	index_t id;
	size_t filecnt;
	char reserved[20];
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


static const unsigned char magic[4] = { 0xaa, 0x41, 0x4b, 0x55 };


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
	sectorcnt = sizeof(header_t) + MAX_FILE_CNT * sizeof(fileheader_t);
	sectorcnt += meterfs_common.sectorsz - 1;
	sectorcnt /= meterfs_common.sectorsz;

	for (i = 0; i < sectorcnt; ++i)
		flash_eraseSector(sector + i);
}


void meterfs_checkfs(void)
{
	unsigned int addr = 0, valid0 = 0, valid1 = 0, src, dst, i;
	header_t h;
	fileheader_t f;
	index_t id;

	/* Check if first header is valid */
	flash_read(addr, &h, sizeof(h));
	if (!(h.id.nvalid || memcmp(h.magic, magic, 4))) {
		valid0 = 1;
		id = h.id;
	}

	/* Check next header */
	meterfs_common.h1Addr = sizeof(header_t) + MAX_FILE_CNT * sizeof(fileheader_t);

	flash_read(meterfs_common.h1Addr, &h, sizeof(h));

	if (!(h.id.nvalid || memcmp(h.magic, magic, 4)))
		valid1 = 1;

	if (!valid0 && !valid1) {
		printf("meterfs: No valid filesystem detected. Formating.\n");
		flash_chipErase();

		h.filecnt = 0;
		h.id.no = 0;
		h.id.nvalid = 0;
		memcpy(h.magic, magic, 4);

		flash_write(0, &h, sizeof(h));
		flash_write(meterfs_common.h1Addr, &h, sizeof(h));

		return;
	}

	/* Select active header and files table */
	if (!valid1 || ((!h.id.nvalid && !id.nvalid) && IS_NEXT_ID(id, h.id)))
		meterfs_common.hcurrAddr = 0;
	else
		meterfs_common.hcurrAddr = meterfs_common.h1Addr;

	flash_read(meterfs_common.hcurrAddr, &h, sizeof(h));
	meterfs_common.filecnt = h.filecnt;

	/* There should be copy of file table at all times. Fix it if necessary */
	if (!valid0 || !valid1) {
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
}


int meterfs_getFileInfoName(const char *name, fileheader_t *f)
{
	header_t header;
	fileheader_t t;
	size_t i;

	if (name == NULL || f == NULL)
		return -EINVAL;

	flash_read(meterfs_common.hcurrAddr, &header, sizeof(header));

	for (i = 0; i < header.filecnt; ++i) {
		flash_read(meterfs_common.hcurrAddr + sizeof(header_t) + (i * sizeof(fileheader_t)), &t, sizeof(fileheader_t));
		if (strncmp(name, t.name, sizeof(f->name)) == 0) {
			memcpy(f, &t, sizeof(t));
			return EOK;
		}
	}

	return -ENOENT;
}


int meterfs_updateFileInfo(fileheader_t *f)
{
	unsigned int headerNew, i;
	union {
		header_t h;
		fileheader_t t;
	} u;

	if (f == NULL)
		return -1;

	/* Check if file exist */
	if (meterfs_getFileInfoName(f->name, &u.t) != 0)
		return -EINVAL;

	/* File can not exceed prealocated sector count */
	if ((f->filesz != u.t.filesz || f->recordsz != u.t.recordsz) && (SECTORS(f, meterfs_common.sectorsz) > u.t.sectorcnt))
		return -ENOMEM;

	f->sector = u.t.sector;
	f->sectorcnt = u.t.sectorcnt;

	/* Clear file content */
	for (i = 0; i < f->sectorcnt; ++i)
		flash_eraseSector(f->sector + i);

	headerNew = (meterfs_common.hcurrAddr == meterfs_common.h1Addr) ? 0 : meterfs_common.h1Addr;

	/* Make space for new file table */
	meterfs_eraseFileTable((headerNew == 0) ? 0 : 1);

	for (i = 0; i < meterfs_common.filecnt; ++i) {
		flash_read(meterfs_common.hcurrAddr + sizeof(header_t) + (i * sizeof(fileheader_t)), f, sizeof(fileheader_t));
		if (strcmp(f->name, u.t.name) == 0)
			flash_write(headerNew + sizeof(header_t) + i * sizeof(fileheader_t), f, sizeof(fileheader_t));
		else
			flash_write(headerNew + sizeof(header_t) + i * sizeof(fileheader_t), &u.t, sizeof(fileheader_t));
	}

	/* Prepare new header */
	flash_read(meterfs_common.hcurrAddr, &u.h, sizeof(u.h));
	++u.h.id.no;

	flash_write(headerNew, &u.h, sizeof(header_t));

	/* Use new header from now on */
	meterfs_common.hcurrAddr = headerNew;

	return EOK;
}


void meterfs_getFilePos(file_t *f)
{
	unsigned int baddr, eaddr, totalrecord, maxrecord;
	int i, offset, interval, diff, idx;
	entry_t e;

	if (f == NULL)
		return;

	f->lastidx.no = 0;
	f->lastidx.nvalid = 1;
	f->lastoff = 0;
	f->recordcnt = 0;

	baddr = f->header.sector * meterfs_common.sectorsz;
	eaddr = baddr + f->header.sectorcnt * meterfs_common.sectorsz;
	totalrecord = (eaddr - baddr) / (f->header.recordsz + sizeof(entry_t));
	maxrecord = f->header.filesz / f->header.recordsz - 1;
	diff = 0;

	/* Find any valid record (starting point) */
	flash_read(baddr, &e, sizeof(e));
	if (!e.id.nvalid) {
		f->lastidx = e.id;
		f->lastoff = 0;
	}
	else {
		for (interval = totalrecord - 1; interval > 0 && f->lastidx.nvalid; interval >>= 1) {
			for (i = interval; i <= totalrecord - 1; i += (interval << 1)) {
				offset = i * (f->header.recordsz + sizeof(entry_t));
				flash_read(baddr + offset, &e, sizeof(e));
				if (!e.id.nvalid) {
					f->lastidx = e.id;
					f->lastoff = offset;
					break;
				}
			}
		}
	}

	f->firstidx = f->lastidx;
	f->firstoff = f->lastoff;

	/* Is file empty? */
	if (f->lastidx.nvalid)
		return;

	/* Find newest record */
	for (interval = totalrecord - 1; interval != 0; ) {
		idx = ((f->lastoff / (f->header.recordsz + sizeof(entry_t))) + interval) % totalrecord;
		offset = idx * (f->header.recordsz + sizeof(entry_t));
		flash_read(baddr + offset, &e, sizeof(e));
		if (!e.id.nvalid && ((f->lastidx.no + interval) % (1 << 31)) == e.id.no) {
			f->lastidx = e.id;
			f->lastoff = offset;
			diff += interval;
			if (interval == 1)
				continue;
		}

		interval /= 2;
	}

	if (diff > 2 * maxrecord) {
		f->firstidx = f->lastidx;
		f->firstoff = f->lastoff;
		diff = 0;
	}
	diff -= maxrecord;

	/* Find oldest record */
	for (interval = diff; interval != 0 && diff != 0; ) {
		idx = (int)(f->firstoff / (f->header.recordsz + sizeof(entry_t))) + interval;
		if (idx < 0)
			idx += totalrecord;
		else
			idx %= totalrecord;
		offset = idx * (f->header.recordsz + sizeof(entry_t));
		flash_read(baddr + offset, &e, sizeof(e));
		if (!e.id.nvalid && ((f->firstidx.no + interval) % (1 << 31)) == e.id.no) {
			f->firstidx = e.id;
			f->firstoff = offset;
			diff -= interval;
			if (interval == 1 || interval == -1)
				continue;
		}

		interval /= 2;
	}

	f->recordcnt = f->lastidx.no - f->firstidx.no + 1;
}


int meterfs_writeRecord(file_t *f, void *buff)
{
	/* This function assumes that f contains valid lastidx and lastoff */
	entry_t e;
	unsigned int offset;

	if (f == NULL || buff == NULL)
		return -EINVAL;

	offset = f->lastoff;

	if (!f->lastidx.nvalid)
		offset += f->header.recordsz + sizeof(entry_t);

	if (offset + f->header.recordsz + sizeof(entry_t) > f->header.sectorcnt * meterfs_common.sectorsz)
		offset = 0;

	/* Check if we have to erase sector to write new data */
	if (offset == 0 || (offset / meterfs_common.sectorsz) != ((offset + f->header.recordsz + sizeof(entry_t)) / meterfs_common.sectorsz))
		flash_eraseSector(f->header.sector + (offset + f->header.recordsz + sizeof(entry_t)) / meterfs_common.sectorsz);

	e.id.no = f->lastidx.no + 1;
	e.id.nvalid = 0;

	flash_write(f->header.sector * meterfs_common.sectorsz + offset + sizeof(entry_t), buff, f->header.recordsz);
	flash_write(f->header.sector * meterfs_common.sectorsz + offset, &e, sizeof(entry_t));

	f->lastidx.no += 1;
	f->lastidx.nvalid = 0;
	f->lastoff = offset;

	if (f->recordcnt < (f->header.filesz / f->header.recordsz)) {
		++f->recordcnt;

		if (f->firstidx.nvalid) {
			f->firstidx = f->lastidx;
			f->firstoff = f->lastoff;
		}
	}
	else {
		++f->firstidx.no;
		f->firstoff += f->header.recordsz + sizeof(entry_t);
		if (f->firstoff + f->header.recordsz + sizeof(entry_t) > f->header.sectorcnt * meterfs_common.sectorsz)
			f->firstoff = 0;
	}

	return f->header.recordsz;
}


int meterfs_readRecord(file_t *f, void *buff, unsigned int idx)
{
	/* This function assumes that f contains valid firstidx and firstoff */
	entry_t e;
	unsigned int addr, pos;

	if (f == NULL || buff == NULL)
		return -EINVAL;

	if (f->firstidx.nvalid || idx > f->recordcnt)
		return -ENOENT;

	/* Find record position in flash */
	pos = (f->firstoff / (f->header.recordsz + sizeof(entry_t))) + idx;
	pos = pos % ((f->header.sectorcnt * meterfs_common.sectorsz) / (f->header.recordsz + sizeof(entry_t)));
	addr = pos * (f->header.recordsz + sizeof(entry_t)) + f->header.sector * meterfs_common.sectorsz;

	/* Check if entry's valid */
	flash_read(addr, &e, sizeof(e));

	if (e.id.nvalid || e.id.no != f->firstidx.no + idx)
		return -ENOENT;

	/* Read data */
	flash_read(addr + sizeof(entry_t), buff, f->header.recordsz);

	return f->header.recordsz;
}


int meterfs_alocateFile(fileheader_t *f)
{
	header_t h;
	fileheader_t t;
	unsigned int addr, i, headerNew;

	if (f == NULL)
		return -EINVAL;

	/* Check if file exists */
	if (meterfs_getFileInfoName(f->name, &t) == EOK)
		return -EEXIST;

	/* Check if sectorcnt is valid */
	if (SECTORS(f, meterfs_common.sectorsz) > f->sectorcnt || f->sectorcnt < 2)
		return -EINVAL;

	flash_read(meterfs_common.hcurrAddr, &h, sizeof(h));

	if (h.filecnt >= MAX_FILE_CNT)
		return -ENOMEM;

	/* Find free sectors */
	if (h.filecnt != 0) {
		flash_read(meterfs_common.hcurrAddr + sizeof(header_t) + (h.filecnt - 1) * sizeof(fileheader_t), &t, sizeof(t));

		f->sector = t.sector + t.sectorcnt;
		addr = f->sector * meterfs_common.sectorsz;

		if (addr + (f->sectorcnt * meterfs_common.sectorsz) >= meterfs_common.flashsz)
			return -ENOMEM;
	}
	else {
		addr = meterfs_common.h1Addr << 1;
		f->sector = addr / meterfs_common.sectorsz;
	}

	/* Prepare data space */
	for (i = 0; i < f->sectorcnt; ++i)
		flash_eraseSector(f->sector + i);

	headerNew = (meterfs_common.hcurrAddr == 0) ? meterfs_common.h1Addr : 0;
	meterfs_eraseFileTable((headerNew == 0) ? 0 : 1);

	/* Copy data from the old header */
	for (i = 0; i < h.filecnt; ++i) {
		flash_read(meterfs_common.hcurrAddr + sizeof(header_t) + (i * sizeof(fileheader_t)), &t, sizeof(t));
		flash_write(headerNew + sizeof(header_t) + (i * sizeof(fileheader_t)), &t, sizeof(t));
	}

	/* Store new file header */
	flash_write(headerNew + sizeof(header_t) + (h.filecnt * sizeof(fileheader_t)), f, sizeof(fileheader_t));

	/* Commit new header and update global info */
	h.filecnt += 1;
	h.id.no += 1;

	flash_write(headerNew, &h, sizeof(h));
	meterfs_common.filecnt += 1;
	meterfs_common.hcurrAddr = headerNew;

	return EOK;
}

#ifndef NDEBUG
void meterfs_hexdump(const unsigned char *buff, size_t bufflen)
{
	size_t i;

	for (i = 0; i < bufflen; ++i) {
		if (i % 16 == 0)
			printf("\n");
		printf("0x%02x ", buff[i]);
	}

	printf("\n");
}


void meterfs_fileDump(file_t *f)
{
	int i;
	unsigned int addr;
	unsigned char *buff;

	printf("Dumping %s\n", f->header.name);

	addr = f->header.sector * meterfs_common.sectorsz;

	printf("File info:\n");
	printf("Name: %s\n", f->header.name);
	printf("Sector: %u\n", f->header.sector);
	printf("Sector cnt: %u\n", f->header.sectorcnt);
	printf("File size: %u\n", f->header.filesz);
	printf("Record size: %u\n", f->header.recordsz);
	printf("Last index: %u, (%s)\n", f->lastidx.no, f->lastidx.nvalid ? "not valid" : "valid");
	printf("Last offset: %u\n", f->lastoff);
	printf("First index: %u, (%s)\n", f->firstidx.no, f->firstidx.nvalid ? "not valid" : "valid");
	printf("First offset: %u\n", f->firstoff);
	printf("File begin: 0x%p\n", addr);
	printf("Record count: %u\n", f->recordcnt);

	buff = malloc(f->header.recordsz);

	printf("\nData:\n");
	for (i = 0; i < f->recordcnt; ++i) {
		if (meterfs_readRecord(f, buff, i) < 0) {
			printf("Could not read record %d!\n", i);
			break;
		}
		printf("Record %d:\n", i);
		meterfs_hexdump(buff, f->header.recordsz);
	}

	free(buff);
}

unsigned char data[1024];

void meterfs_test(void)
{
	header_t h;
	file_t f;

	keepidle(1);

	flash_read(0, &h, sizeof(h));
	printf("Header 0:\n");
	meterfs_hexdump((unsigned char *)&h, sizeof(h));

	flash_read(meterfs_common.h1Addr, &h, sizeof(h));
	printf("Header 1:\n");
	meterfs_hexdump((unsigned char *)&h, sizeof(h));

	DEBUG("Adding file 'test'");
	printf("Curr header 0x%p\n", meterfs_common.hcurrAddr);

	strcpy(f.header.name, "test");
	f.header.filesz = 1024;
	f.header.recordsz = 32;
	f.header.sectorcnt = 2;
	f.firstidx.no = (unsigned int)(-1);
	f.firstidx.nvalid = 1;
	f.firstoff = 0;
	f.lastidx.no = 0;
	f.lastidx.nvalid = 1;
	f.lastoff = 0;
	f.recordcnt = 0;

	if (meterfs_alocateFile(&f.header) != EOK) {
		meterfs_getFileInfoName("test", &f.header);
		meterfs_getFilePos(&f);
	}

	flash_read(meterfs_common.hcurrAddr, &h, sizeof(h));
	printf("h.fcnt %u, h.id %u\n", h.filecnt, h.id);

	printf("Curr header 0x%p\n", meterfs_common.hcurrAddr);

#if 0

	DEBUG("Writing 225 records");
	for (int i = 0; i < 225; ++i) {
		memset(data, i, 128);
		meterfs_writeRecord(&f, data);
	}
	DEBUG("Done.");

	meterfs_fileDump(&f);

	DEBUG("Changing record size to 23");
	f.header.recordsz = 23;
	meterfs_updateFileInfo(&f.header);

	memset(&f, 0, sizeof(file_t));

	meterfs_getFileInfoName("test", &f.header);
	meterfs_getFilePos(&f);

	meterfs_fileDump(&f);

	DEBUG("Writing 225 records");
	for (int i = 0; i < 225; ++i) {
		memset(data, i, 128);
		meterfs_writeRecord(&f, data);
	}
	DEBUG("Done.");

	for (int i = 0; i < 100; ++i) {
		memset(data, i, 128);
		meterfs_writeRecord(&f, data);
		meterfs_fileDump(&f);
		printf("Old pos: idx %u, off %u (%s)\n", f.lastidx.no, f.lastoff, f.lastidx.nvalid ? "not valid" : "valid");
		meterfs_getFileInfoName("test", &f.header);
		meterfs_getFilePos(&f);
		printf("New pos: idx %u, off %u (%s)\n", f.lastidx.no, f.lastoff, f.lastidx.nvalid ? "not valid" : "valid");
	}
#endif
	DEBUG("Writing 23 records");
	for (int i = 0; i < 23; ++i) {
		memset(data, i & 0xff, 128);
		meterfs_writeRecord(&f, data);
		DEBUG("f.lastidx %u, f.lastoff %u, recordcnt %u, data[0] %u", f.lastidx.no, f.lastoff, f.recordcnt, data[0]);
	}
	DEBUG("Done.");

	meterfs_fileDump(&f);

	DEBUG("Adding file 'test1'");
	printf("Curr header 0x%p\n", meterfs_common.hcurrAddr);

	strcpy(f.header.name, "test1");
	f.header.filesz = 1024;
	f.header.recordsz = 64;
	f.header.sectorcnt = 2;
	f.firstidx.no = (unsigned int)(-1);
	f.firstidx.nvalid = 1;
	f.firstoff = 0;
	f.lastidx.no = 0;
	f.lastidx.nvalid = 1;
	f.lastoff = 0;
	f.recordcnt = 0;

	if (meterfs_alocateFile(&f.header) != EOK) {
		meterfs_getFileInfoName("test1", &f.header);
		meterfs_getFilePos(&f);
	}

	DEBUG("Writing 23 records");
	for (int i = 0; i < 23; ++i) {
		memset(data, i  & 0xff, 128);
		meterfs_writeRecord(&f, data);
		DEBUG("f.lastidx %u, f.lastoff %u, recordcnt %u, data[0] %u", f.lastidx.no, f.lastoff, f.recordcnt, data[0]);
	}
	DEBUG("Done.");

	meterfs_fileDump(&f);

	DEBUG("Trying to add invalid file (filesz > sectorcnt * sectorsz)");
	strcpy(f.header.name, "toobig");
	f.header.filesz = 100000;
	f.header.recordsz = 100;
	f.header.sectorcnt = 10;
	f.firstidx.no = (unsigned int)(-1);
	f.firstidx.nvalid = 1;
	f.firstoff = 0;
	f.lastidx.no = 0;
	f.lastidx.nvalid = 1;
	f.lastoff = 0;
	f.recordcnt = 0;
	DEBUG("Result: %d", meterfs_alocateFile(&f.header));

	DEBUG("Adding file 'bigone'");
	printf("Curr header 0x%p\n", meterfs_common.hcurrAddr);

	strcpy(f.header.name, "bigone");
	f.header.filesz = 2048;
	f.header.recordsz = 125;
	f.header.sectorcnt = 2;
	f.firstidx.no = (unsigned int)(-1);
	f.firstidx.nvalid = 1;
	f.firstoff = 0;
	f.lastidx.no = 0;
	f.lastidx.nvalid = 1;
	f.lastoff = 0;
	f.recordcnt = 0;

	if (meterfs_alocateFile(&f.header) != EOK) {
		meterfs_getFileInfoName("bigone", &f.header);
		meterfs_getFilePos(&f);
	}

	DEBUG("Writing 64 records");
	for (int i = 0; i < 64; ++i) {
		memset(data, i, 128);
		meterfs_writeRecord(&f, data);
		DEBUG("f.lastidx %u, f.lastoff %u, recordcnt %u, data[0] %u", f.lastidx.no, f.lastoff, f.recordcnt, data[0]);
	}
	DEBUG("Done.");

	meterfs_fileDump(&f);

	DEBUG("Dumping files...");

	meterfs_getFileInfoName("test", &f.header);
	meterfs_getFilePos(&f);
	meterfs_fileDump(&f);

	meterfs_getFileInfoName("test1", &f.header);
	meterfs_getFilePos(&f);
	meterfs_fileDump(&f);

	meterfs_getFileInfoName("bigone", &f.header);
	meterfs_getFilePos(&f);
	meterfs_fileDump(&f);

	DEBUG("Writing 20 records");
	for (int i = 0; i < 20; ++i) {
		memset(data, i, 128);
		meterfs_writeRecord(&f, data);
		DEBUG("f.lastidx %u, f.lastoff %u, recordcnt %u, data[0] %u", f.lastidx.no, f.lastoff, f.recordcnt, data[0]);
	}
	DEBUG("Done.");

	meterfs_fileDump(&f);

	meterfs_getFileInfoName("bigone", &f.header);
	meterfs_getFilePos(&f);
	meterfs_fileDump(&f);

	DEBUG("Erasing 1st sector of bigone");
	flash_eraseSector(f.header.sector);

	meterfs_getFileInfoName("bigone", &f.header);
	meterfs_getFilePos(&f);
	meterfs_fileDump(&f);

	DEBUG("Adding file 'longone'");
	printf("Curr header 0x%p\n", meterfs_common.hcurrAddr);

	strcpy(f.header.name, "longone");
	f.header.filesz = 1000;
	f.header.recordsz = 20;
	f.header.sectorcnt = 10;
	f.firstidx.no = 0;
	f.firstidx.nvalid = 1;
	f.firstoff = 0;
	f.lastidx.no = 0;
	f.lastidx.nvalid = 1;
	f.lastoff = 0;
	f.recordcnt = 0;

	if (meterfs_alocateFile(&f.header) != EOK) {
		meterfs_getFileInfoName("longone", &f.header);
		meterfs_getFilePos(&f);
	}

	f.lastidx.no = 0x7ffffff0;

	DEBUG("Writing 1000 records");
	for (int i = 0; i < 1000; ++i) {
		memset(data, i & 0xff, 128);
		meterfs_writeRecord(&f, data);
		DEBUG("f.lastidx %u, f.lastoff %u, recordcnt %u, data[0] %u", f.lastidx.no, f.lastoff, f.recordcnt, data[0]);
	}
	DEBUG("Done.");

	meterfs_fileDump(&f);

	meterfs_getFileInfoName("longone", &f.header);
	meterfs_getFilePos(&f);
	meterfs_fileDump(&f);
}

#endif


int main(void)
{
	meterfs_common.sectorsz = 4 * 1024;
	meterfs_common.flashsz = 2 * 1024 * 1024;

	spi_init();

	flash_init(meterfs_common.flashsz, meterfs_common.sectorsz);
flash_chipErase();
	meterfs_checkfs();

	if (portCreate(&meterfs_common.port) != EOK)
		FATAL("Could not create port");

	if (portRegister(meterfs_common.port, "/") != EOK)
		FATAL("Could not register port");

meterfs_test();

	for (;;) {
		usleep(10000000);
	}
}

