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

#define CEIL(value, size)       ((((value) + (size) - 1) / (size)) * (size))
#define ENTRIES(f)              ((f)->filesz / ((f)->recordsz + sizeof(entry_t)))
#define TOTAL_SIZE(f)           (((f)->filesz * ((f)->recordsz + sizeof(entry_t))) / (f)->recordsz)
#define SECTORS(f, sectorsz)    ((TOTAL_SIZE(f) / sectorsz) + 1)
#define IS_NEXT_ID(next, prev)  (((unsigned int)next.no == (((unsigned int)prev.no + 1) & 0x7fffffff)) || (next.no == prev.no))
#define MAX_FILE_CNT(sectorsz)  ((sectorsz - sizeof(header_t)) / sizeof(file_t))

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
	char reserved[24];
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
	sectorcnt = sizeof(header_t) + (meterfs_common.filecnt * sizeof(fileheader_t));
	sectorcnt /= meterfs_common.sectorsz;

	if (!sectorcnt)
		sectorcnt = 1;

	for (i = 0; i < sectorcnt; ++i)
		flash_eraseSector(sector + i);
}


int meterfs_checkfs(void)
{
	unsigned int addr = 0, valid0 = 0, valid1 = 0, maxSector, src, dst, i;
	header_t h;
	fileheader_t f;
	index_t id;

	/* Check if first header is valid */
	flash_read(addr, &h, sizeof(h));
	if (!h.id.nvalid) {
		valid0 = 1;
		id = h.id;
		addr = sizeof(h) + (h.filecnt * sizeof(fileheader_t));
		addr = CEIL(addr, meterfs_common.sectorsz);
		meterfs_common.filecnt = h.filecnt;
		DEBUG("First filetable is valid.");
	}

	/* Check next header */
	if (!addr) {
		DEBUG("Could not establish filetable backup address.");
		maxSector = ((MAX_FILE_CNT(meterfs_common.sectorsz) * sizeof(fileheader_t)) + sizeof(header_t)) / meterfs_common.sectorsz;
		for (addr = 0; addr <= (maxSector * meterfs_common.sectorsz); addr += meterfs_common.sectorsz) {
			flash_read(addr, &h, sizeof(h));
			if (!h.id.nvalid) {
				valid1 = 1;
				meterfs_common.filecnt = h.filecnt;
				DEBUG("Found copy and it's valid");
				break;
			}
		}
	}
	else {
		flash_read(addr, &h, sizeof(h));
		if (!h.id.nvalid) {
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
	if (!valid1 || ((!h.id.nvalid && !id.nvalid) && IS_NEXT_ID(id, h.id)))
		meterfs_common.hcurrAddr = 0;
	else
		meterfs_common.hcurrAddr = meterfs_common.h1Addr;


	flash_read(meterfs_common.hcurrAddr, &h, sizeof(h));
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


int meterfs_getFileInfoPos(unsigned int n, fileheader_t *f)
{
	if (f == NULL || n >= meterfs_common.filecnt)
		return -1;

	flash_read(meterfs_common.hcurrAddr + sizeof(header_t) + (n * sizeof(fileheader_t)), f, sizeof(fileheader_t));

	return 0;
}


int meterfs_getFileInfoName(const char *name, fileheader_t *f)
{
	header_t header;
	fileheader_t file;
	size_t i;

	if (name == NULL || f == NULL)
		return -1;

	flash_read(meterfs_common.hcurrAddr, &header, sizeof(header));

	for (i = 0; i < header.filecnt; ++i) {
		meterfs_getFileInfoPos(i, &file);
		if (strcmp(name, file.name) == 0) {
			memcpy(f, &file, sizeof(file));
			return 0;
		}
	}

	return -1;
}


int meterfs_updateFileInfo(fileheader_t *f)
{
	header_t h;
	fileheader_t t;
	unsigned int headerOld, headerNew, i, offset;

	if (f == NULL)
		return -1;

	/* Check if file exist */
	if (meterfs_getFileInfoName(f->name, &t) != 0)
		return -1;

	/* File can not exceed prealocated sector count */
	if (f->filesz != t.filesz || f->recordsz != t.recordsz) {
		if (SECTORS(f, meterfs_common.sectorsz) > t.sectorcnt)
			return -1;
	}

	f->sector = t.sector;
	f->sectorcnt = t.sectorcnt;

	/* Clear file content */
	for (i = 0; i < f->sectorcnt; ++i)
		flash_eraseSector(f->sector + i);

	headerOld = meterfs_common.hcurrAddr;
	headerNew = (meterfs_common.hcurrAddr == meterfs_common.h1Addr) ? 0 : meterfs_common.h1Addr;

	/* Make space for new file table */
	meterfs_eraseFileTable((headerNew == 0) ? 0 : 1);

	for (i = 0, offset = sizeof(header_t); i < meterfs_common.filecnt; ++i) {
		meterfs_getFileInfoPos(i, &t);
		if (strcmp(f->name, t.name) == 0)
			flash_write(headerNew + offset, f, sizeof(fileheader_t));
		else
			flash_write(headerNew + offset, &t, sizeof(fileheader_t));

		offset += sizeof(fileheader_t);
	}

	/* Prepare new header */
	flash_read(headerOld, &h, sizeof(h));
	++h.id.no;

	flash_write(headerNew, &h, sizeof(header_t));

	/* Use new header from now on */
	meterfs_common.hcurrAddr = headerNew;

	return 0;
}


void meterfs_getFilePos(file_t *f)
{
	unsigned int addr, endaddr, maxrecord;
	entry_t e;

	if (f == NULL)
		return;

	f->firstidx.no = 0;
	f->firstidx.nvalid = 1;
	f->lastidx.no = 0;
	f->lastidx.nvalid = 1;
	f->firstoff = 0;
	f->lastoff = 0;
	f->recordcnt = 0;

	addr = f->header.sector * meterfs_common.sectorsz;
	endaddr = addr + f->header.sectorcnt * meterfs_common.sectorsz;
	maxrecord = f->header.filesz / f->header.recordsz;

	/* Find newest record */
	for (; addr < endaddr; addr += f->header.recordsz + sizeof(entry_t)) {
		flash_read(addr, &e, sizeof(e));
		if (!e.id.nvalid) {
			if (f->lastidx.nvalid || IS_NEXT_ID(e.id, f->lastidx)) {
				f->lastidx = e.id;
				f->lastoff = addr - (f->header.sector * meterfs_common.sectorsz);
				DEBUG("Found new idx %u @ offset %u", f->lastidx.no, f->lastoff);
			}
			else {
				break;
			}
		}
		else if (!f->lastidx.nvalid) {
			break;
		}
	}

	DEBUG("Last: idx %u, pos %u", f->lastidx.no, f->lastoff);

	/* File's empty */
	if (f->lastidx.nvalid)
		return;

	f->firstidx = f->lastidx;
	f->firstoff = f->lastoff;

	for (addr = f->lastoff + f->header.sector * meterfs_common.sectorsz, f->recordcnt = 1; f->recordcnt <= maxrecord; ++(f->recordcnt)) {
		addr -= f->header.recordsz + sizeof(entry_t);
		if (addr < f->header.sector * meterfs_common.sectorsz) {
			/* Go to last possible offset */
			addr = ((f->header.sectorcnt * meterfs_common.sectorsz) / (f->header.recordsz + sizeof(entry_t))) - 1;
			addr *= f->header.recordsz + sizeof(entry_t);
			addr += f->header.sector * meterfs_common.sectorsz;
			DEBUG("Wrap-around, new addr %u", addr);
		}

		flash_read(addr, &e, sizeof(e));
		if (!e.id.nvalid && (f->firstidx.nvalid || IS_NEXT_ID(f->firstidx, e.id))) {
			f->firstidx = e.id;
			f->firstoff = addr - (f->header.sector * meterfs_common.sectorsz);
			DEBUG("Found new idx %u @ offset %u", f->firstidx.no, f->firstoff);
		}
		else {
			break;
		}
	}

	DEBUG("First: idx %u, pos %u", f->firstidx.no, f->firstoff);
}


int meterfs_writeRecord(file_t *f, void *buff)
{
	/* This function assumes that f contains valid lastidx and lastoff */
	entry_t e;
	unsigned int offset, sector, maxrecord;

	if (f == NULL || buff == NULL)
		return -1;

	offset = f->lastoff;

	if (!f->lastidx.nvalid) {
		offset += f->header.recordsz + sizeof(entry_t);
	}
	else {
		f->firstidx.no = f->lastidx.no + 1;
		f->firstidx.nvalid = 0;
		f->firstoff = 0;
	}

	if (offset + f->header.recordsz + sizeof(entry_t) >= f->header.sectorcnt * meterfs_common.sectorsz)
		offset = 0;

	DEBUG("lastoff %u, offset %u", f->lastoff, offset);

	/* Check if we have to erase sector to write new data */
	if (offset == 0 || (offset / meterfs_common.sectorsz) != ((offset + f->header.recordsz + sizeof(entry_t)) / meterfs_common.sectorsz)) {
		sector = ((offset + f->header.recordsz + sizeof(entry_t)) / meterfs_common.sectorsz) % f->header.sectorcnt;
		sector += f->header.sector;
		DEBUG("Erasing sector %u", sector);
		flash_eraseSector(sector);
	}

	e.id.no = f->lastidx.no + 1;
	e.id.nvalid = 0;

	flash_write(f->header.sector * meterfs_common.sectorsz + offset + sizeof(entry_t), buff, f->header.recordsz);
	flash_write(f->header.sector * meterfs_common.sectorsz + offset, &e, sizeof(entry_t));

	f->lastidx.no += 1;
	f->lastidx.nvalid = 0;
	f->lastoff = offset;

	if (f->recordcnt < (f->header.filesz / f->header.recordsz)) {
		++f->recordcnt;
	}
	else {
		++f->firstidx.no;
		f->firstoff += f->header.recordsz;
		if (f->firstoff + f->header.recordsz + sizeof(entry_t) >= f->header.sectorcnt * meterfs_common.sectorsz)
			f->firstoff = 0;
	}

	return f->header.recordsz;
}


int meterfs_readRecord(fileheader_t *f, void *buff, unsigned int idx)
{
	return -1;
}

#ifndef NDEBUG
int meterfs_alocateFile(fileheader_t *f)
{
	header_t h;
	fileheader_t t;
	unsigned int addr, sectors, i, headerOld, headerNew;

	if (f == NULL)
		return -EINVAL;

	sectors = SECTORS(f, meterfs_common.sectorsz) + 1;

	if (sectors <= 1)
		return -EINVAL;

	flash_read(meterfs_common.hcurrAddr, &h, sizeof(h));

	if (h.filecnt >= MAX_FILE_CNT(meterfs_common.sectorsz))
		return -ENOMEM;

	if (h.filecnt != 0) {
		flash_read(meterfs_common.hcurrAddr + sizeof(header_t) + (h.filecnt - 1) * sizeof(fileheader_t), &t, sizeof(t));

		f->sector = t.sector + t.sectorcnt;
		addr = f->sector * meterfs_common.sectorsz;

		if (addr + (sectors + meterfs_common.sectorsz) >= meterfs_common.flashsz)
			return -ENOMEM;
	}
	else {
		addr = meterfs_common.h1Addr << 1;
		f->sector = addr / meterfs_common.sectorsz;
	}

	/* Prepare data space */
	for (i = 0; i < sectors; ++i) {
		if (!flash_regionIsBlank(addr + (i * meterfs_common.sectorsz), meterfs_common.sectorsz))
			flash_eraseSector((addr / meterfs_common.sectorsz) + i);
	}

	/* Prepare new header */
	h.filecnt += 1;
	h.id.no += 1;
	headerOld = meterfs_common.hcurrAddr;
	headerNew = (meterfs_common.hcurrAddr == 0) ? meterfs_common.h1Addr : 0;

	if (!flash_regionIsBlank(headerNew, sizeof(header_t) + (h.filecnt * sizeof(fileheader_t))))
		meterfs_eraseFileTable((headerNew == 0) ? 0 : 1);

	for (i = 0; i < h.filecnt - 1; ++i) {
		flash_read(headerOld + sizeof(header_t) + (i * sizeof(fileheader_t)), &t, sizeof(t));
		flash_write(headerNew + sizeof(header_t) + (i * sizeof(fileheader_t)), &t, sizeof(t));
	}

	flash_write(headerNew + sizeof(header_t) + ((h.filecnt - 1) * sizeof(fileheader_t)), f, sizeof(fileheader_t));

	/* Commit new header and update global info */
	flash_write(headerNew, &h, sizeof(h));
	meterfs_common.filecnt += 1;
	meterfs_common.hcurrAddr = headerNew;

	return EOK;
}


void meterfs_format(void)
{
	header_t h;

	h.filecnt = 0;
	h.id.no = 0;
	h.id.nvalid = 0;

	spi_transaction(cmd_wren, 0, 0, NULL, 0);
	spi_transaction(cmd_chip_erase, 0, 0, NULL, 0);
	spi_transaction(cmd_wrdi, 0, 0, NULL, 0);

	unsigned char status;

	do
		spi_transaction(cmd_rdsr, 0, spi_read, &status, 1);
	while (status & 1);

	flash_write(0, &h, sizeof(h));
	flash_write(meterfs_common.h1Addr, &h, sizeof(h));
}


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
	unsigned int addr;
//	unsigned char *buff;
	unsigned char buff[36];
	entry_t *e;

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

//	buff = malloc(f.recordsz + sizeof(entry_t));
	e = (entry_t *)buff;

	printf("\nData:\n");
	for (; addr + f->header.recordsz + sizeof(entry_t) < (f->header.sector + f->header.sectorcnt) * meterfs_common.sectorsz; addr += f->header.recordsz + sizeof(entry_t)) {
		flash_read(addr, buff, f->header.recordsz + sizeof(entry_t));
		printf("ID: %u (%s)\n", e->id.no, e->id.nvalid ? "not valid" : "valid");
		printf("Address: 0x%p\n", addr);
		meterfs_hexdump(e->data, f->header.recordsz);
		printf("\n");
	}
}
#endif


int main(void)
{
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
	header_t h;
	file_t f;
	unsigned char data[32];

	keepidle(1);

	meterfs_common.filecnt = 0;
	meterfs_common.h1Addr = meterfs_common.sectorsz;
	meterfs_common.hcurrAddr = 0;
	DEBUG("Formating volume");
	meterfs_format();
	DEBUG("Done.");

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

	meterfs_alocateFile(&f.header);

	flash_read(meterfs_common.hcurrAddr, &h, sizeof(h));
	printf("h.fcnt %u, h.id %u\n", h.filecnt, h.id);

	meterfs_fileDump(&f);
	printf("Curr header 0x%p\n", meterfs_common.hcurrAddr);

#if 0

	DEBUG("Writing 225 records");
	for (int i = 0; i < 225; ++i) {
		memset(data, i, 32);
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
		memset(data, i, 32);
		meterfs_writeRecord(&f, data);
	}
	DEBUG("Done.");

	for (int i = 0; i < 100; ++i) {
		memset(data, i, 32);
		meterfs_writeRecord(&f, data);
		meterfs_fileDump(&f);
		printf("Old pos: idx %u, off %u (%s)\n", f.lastidx.no, f.lastoff, f.lastidx.nvalid ? "not valid" : "valid");
		meterfs_getFileInfoName("test", &f.header);
		meterfs_getFilePos(&f);
		printf("New pos: idx %u, off %u (%s)\n", f.lastidx.no, f.lastoff, f.lastidx.nvalid ? "not valid" : "valid");
	}
#else
	DEBUG("Writing 23 records");
	for (int i = 0; i < 23; ++i) {
		memset(data, i, 32);
		meterfs_writeRecord(&f, data);
	}
	DEBUG("Done.");

	DEBUG("Adding file 'test1'");
	printf("Curr header 0x%p\n", meterfs_common.hcurrAddr);

	strcpy(f.header.name, "test1");
	f.header.filesz = 512;
	f.header.recordsz = 16;
	f.header.sectorcnt = 2;
	f.firstidx.no = (unsigned int)(-1);
	f.firstidx.nvalid = 1;
	f.firstoff = 0;
	f.lastidx.no = 0;
	f.lastidx.nvalid = 1;
	f.lastoff = 0;
	f.recordcnt = 0;

	meterfs_alocateFile(&f.header);

	DEBUG("Writing 23 records");
	for (int i = 0; i < 23; ++i) {
		memset(data, i, 32);
		meterfs_writeRecord(&f, data);
	}
	DEBUG("Done.");

	DEBUG("Adding file 'test2'");
	printf("Curr header 0x%p\n", meterfs_common.hcurrAddr);

	strcpy(f.header.name, "test1");
	f.header.filesz = 256;
	f.header.recordsz = 16;
	f.header.sectorcnt = 2;
	f.firstidx.no = (unsigned int)(-1);
	f.firstidx.nvalid = 1;
	f.firstoff = 0;
	f.lastidx.no = 0;
	f.lastidx.nvalid = 1;
	f.lastoff = 0;
	f.recordcnt = 0;

	meterfs_alocateFile(&f.header);

	DEBUG("Writing 23 records");
	for (int i = 0; i < 23; ++i) {
		memset(data, i, 32);
		meterfs_writeRecord(&f, data);
	}
	DEBUG("Done.");

	DEBUG("Dumping files...");

	meterfs_getFileInfoName("test", &f.header);
	meterfs_getFilePos(&f);
	meterfs_fileDump(&f);

	meterfs_getFileInfoName("test1", &f.header);
	meterfs_getFilePos(&f);
	meterfs_fileDump(&f);

	meterfs_getFileInfoName("test2", &f.header);
	meterfs_getFilePos(&f);
	meterfs_fileDump(&f);

	DEBUG("Recreating fs data...");

	meterfs_common.filecnt = 0xbabababa;
	meterfs_common.h1Addr = 0xbabababa;
	meterfs_common.hcurrAddr = 0xbabababa;

	meterfs_checkfs();

	printf("Filecnt: %u\n", meterfs_common.filecnt);
	printf("h1Addr: %u\n", meterfs_common.h1Addr);
	printf("hcurrAddr: %u\n", meterfs_common.hcurrAddr);

	DEBUG("Dumping files...");

	meterfs_getFileInfoName("test", &f.header);
	meterfs_getFilePos(&f);
	meterfs_fileDump(&f);

	meterfs_getFileInfoName("test1", &f.header);
	meterfs_getFilePos(&f);
	meterfs_fileDump(&f);

	meterfs_getFileInfoName("test2", &f.header);
	meterfs_getFilePos(&f);
	meterfs_fileDump(&f);



#endif


#endif


#if 0
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

