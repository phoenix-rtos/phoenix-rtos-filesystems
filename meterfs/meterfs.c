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
#include "meterfs.h"
#include "spi.h"
#include "flash.h"
#include "files.h"
#include "opened.h"

#define TOTAL_SIZE(f)           (((f)->filesz * ((f)->recordsz + sizeof(entry_t))) / (f)->recordsz)
#define SECTORS(f, sectorsz)    (((TOTAL_SIZE(f) + sectorsz - 1) / sectorsz) + 1)
#define FATAL(fmt, ...) \
	do { \
		printf("meterfs: FATAL: " fmt "\n", ##__VA_ARGS__); \
		for (;;) \
			usleep(10000000); \
	} while (0)


static const unsigned char magic[4] = { 0xaa, 0x41, 0x4b, 0x55 };


struct {
	unsigned int port;
	unsigned int h1Addr;
	unsigned int hcurrAddr;
	unsigned int filecnt;
	size_t sectorsz;
	size_t flashsz;

	union {
		unsigned char buff[256];
		meterfsdata_t data;
		meterfsopen_t open;
		meterfsclose_t close;
		meterfsconfig_t config;
	} msg_buff;
} meterfs_common;


void meterfs_eraseFileTable(unsigned int n)
{
	unsigned int addr, i;
	size_t sectorcnt;

	if (n != 0 && n != 1)
		return;

	addr = (n == 0) ? 0 : meterfs_common.h1Addr;
	sectorcnt = HGRAIN + MAX_FILE_CNT * HGRAIN;
	sectorcnt += meterfs_common.sectorsz - 1;
	sectorcnt /= meterfs_common.sectorsz;

	for (i = 0; i < sectorcnt; ++i)
		flash_eraseSector(addr + i * meterfs_common.sectorsz);
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
	meterfs_common.h1Addr = HGRAIN + MAX_FILE_CNT * HGRAIN;

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
	if (valid1) {
		meterfs_common.hcurrAddr = meterfs_common.h1Addr;
	}
	else if (valid0) {
		meterfs_common.hcurrAddr = 0;
	}
	else {
		if (id.no == (h.id.no + 1) % (1 << 31) || id.no == h.id.no)
			meterfs_common.hcurrAddr = meterfs_common.h1Addr;
		else
			meterfs_common.hcurrAddr = 0;
	}

	flash_read(meterfs_common.hcurrAddr, &h, sizeof(h));
	meterfs_common.filecnt = h.filecnt;

	/* There should be copy of file table at all times. Fix it if necessary */
	if (!valid0 || !valid1) {
		if (!valid0) {
			printf("meterfs: Filetable header #0 is damaged - repairing\n");
			src = meterfs_common.h1Addr;
			dst = 0;
			meterfs_eraseFileTable(0);
		}
		else {
			printf("meterfs: Filetable header #1 is damaged - repairing\n");
			src = 0;
			dst = meterfs_common.h1Addr;
			meterfs_eraseFileTable(1);
		}

		/* Copy header */
		flash_read(src, &h, sizeof(h));
		flash_write(dst, &h, sizeof(h));

		src += HGRAIN;
		dst += HGRAIN;

		/* Copy file info */
		for (i = 0; i < meterfs_common.filecnt; ++i) {
			flash_read(src, &f, sizeof(f));
			flash_write(dst, &f, sizeof(f));

			src += HGRAIN;
			dst += HGRAIN;
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
		flash_read(meterfs_common.hcurrAddr + HGRAIN + (i * HGRAIN), &t, sizeof(t));
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
		flash_eraseSector((f->sector + i) * meterfs_common.sectorsz);

	headerNew = (meterfs_common.hcurrAddr == meterfs_common.h1Addr) ? 0 : meterfs_common.h1Addr;

	/* Make space for new file table */
	meterfs_eraseFileTable((headerNew == 0) ? 0 : 1);

	for (i = 0; i < meterfs_common.filecnt; ++i) {
		flash_read(meterfs_common.hcurrAddr + HGRAIN + (i * HGRAIN), f, sizeof(fileheader_t));
		if (strcmp(f->name, u.t.name) == 0)
			flash_write(headerNew + HGRAIN + i * HGRAIN, f, sizeof(fileheader_t));
		else
			flash_write(headerNew + HGRAIN + i * HGRAIN, &u.t, sizeof(fileheader_t));
	}

	/* Prepare new header */
	flash_read(meterfs_common.hcurrAddr, &u.h, sizeof(u.h));
	++u.h.id.no;

	flash_write(headerNew, &u.h, sizeof(u.h));

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
	interval = meterfs_common.sectorsz / (f->header.recordsz + sizeof(entry_t));
	interval = (interval + 1) * (f->header.recordsz + sizeof(entry_t));
	for (i = 0, offset = 0; i < f->header.sectorcnt; ++i) {
		flash_read(baddr + offset, &e, sizeof(e));
		if (!e.id.nvalid) {
			f->lastidx = e.id;
			f->lastoff = offset;
			break;
		}

		offset += interval;

		if (offset % meterfs_common.sectorsz > f->header.recordsz + sizeof(entry_t))
			offset -= f->header.recordsz + sizeof(entry_t);
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


int meterfs_writeRecord(file_t *f, void *buff, size_t bufflen)
{
	/* This function assumes that f contains valid lastidx and lastoff */
	entry_t e;
	unsigned int offset;

	if (f == NULL || buff == NULL)
		return -EINVAL;

	if (bufflen > f->header.filesz)
		bufflen = f->header.filesz;

	offset = f->lastoff;

	if (!f->lastidx.nvalid)
		offset += f->header.recordsz + sizeof(entry_t);

	if (offset + f->header.recordsz + sizeof(entry_t) > f->header.sectorcnt * meterfs_common.sectorsz)
		offset = 0;

	/* Check if we have to erase sector to write new data */
	if (offset == 0 || (offset / meterfs_common.sectorsz) != ((offset + f->header.recordsz + sizeof(entry_t)) / meterfs_common.sectorsz))
		flash_eraseSector((f->header.sector * meterfs_common.sectorsz) + (offset + f->header.recordsz + sizeof(entry_t)));

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


int meterfs_readRecord(file_t *f, void *buff, size_t bufflen, unsigned int idx, size_t offset)
{
	/* This function assumes that f contains valid firstidx and firstoff */
	entry_t e;
	unsigned int addr, pos;

	if (f == NULL || buff == NULL || offset >= f->header.recordsz)
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

	if (bufflen > f->header.recordsz - offset)
		bufflen = f->header.recordsz - offset;

	/* Read data */
	flash_read(addr + sizeof(entry_t) + offset, buff, bufflen);

	return bufflen;
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
		flash_read(meterfs_common.hcurrAddr + HGRAIN + (h.filecnt - 1) * HGRAIN, &t, sizeof(t));

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
		flash_eraseSector((f->sector + i) * meterfs_common.sectorsz);

	headerNew = (meterfs_common.hcurrAddr == 0) ? meterfs_common.h1Addr : 0;
	meterfs_eraseFileTable((headerNew == 0) ? 0 : 1);

	/* Copy data from the old header */
	for (i = 0; i < h.filecnt; ++i) {
		flash_read(meterfs_common.hcurrAddr + HGRAIN + (i * HGRAIN), &t, sizeof(t));
		flash_write(headerNew + HGRAIN + (i * HGRAIN), &t, sizeof(t));
	}

	/* Store new file header */
	flash_write(headerNew + HGRAIN + (h.filecnt * HGRAIN), f, sizeof(fileheader_t));

	/* Commit new header and update global info */
	h.filecnt += 1;
	h.id.no += 1;

	flash_write(headerNew, &h, sizeof(h));
	meterfs_common.filecnt += 1;
	meterfs_common.hcurrAddr = headerNew;

	return EOK;
}


int meterfs_openFile(char *name, unsigned int *id)
{
	file_t f;

	if (opened_claim(name, id) == 0)
		return 0;

	if (meterfs_getFileInfoName(name, &f.header) < 0)
		return -EEXIST;

	meterfs_getFilePos(&f);

	if (opened_add(&f, id) != 0)
		return -ENOMEM;

	return 0;
}


size_t meterfs_readFile(unsigned int id, unsigned char *buff, size_t bufflen, size_t pos)
{
	file_t *f;
	unsigned int idx;
	size_t chunk, i = 0;

	if ((f = opened_find(id)) == NULL)
		return -EEXIST;

	idx = pos / f->header.recordsz;
	pos %= f->header.recordsz;

	while (i < bufflen) {
		chunk = (bufflen - i <= f->header.recordsz) ? bufflen - i : f->header.recordsz;
		if (meterfs_readRecord(f, buff + i, chunk, idx, pos) <= 0)
			break;

		pos = 0;
		i += chunk;
		++idx;
	}

	return i;
}

unsigned char buff[20];
int main(void)
{
	int s, err;
	unsigned int id;
	msghdr_t hdr;

	spi_init();
	flash_init(&meterfs_common.flashsz, &meterfs_common.sectorsz);
	opened_init();

	if (meterfs_common.flashsz == 0)
		FATAL("Could not detect flash memory");
flash_chipErase();
	meterfs_checkfs();

	if (portCreate(&meterfs_common.port) != EOK)
		FATAL("Could not create port");

	if (portRegister(meterfs_common.port, "/") != EOK)
		FATAL("Could not register port");


	file_t f, *p;
	int ret;

	printf("Creating files\n");

	strncpy(f.header.name, "test1", 8);
	f.header.filesz = 1000;
	f.header.recordsz = 10;
	f.header.sectorcnt = 2;
	meterfs_alocateFile(&f.header);

	strncpy(f.header.name, "test2", 8);
	f.header.filesz = 1000;
	f.header.recordsz = 20;
	f.header.sectorcnt = 2;
	meterfs_alocateFile(&f.header);

	strncpy(f.header.name, "test3", 8);
	f.header.filesz = 100;
	f.header.recordsz = 5;
	f.header.sectorcnt = 2;
	meterfs_alocateFile(&f.header);

	printf("test1\n");
	ret = meterfs_openFile("test1", &id);
	printf("ID %u, returned %d\n", id, ret);

	p = opened_find(id);
	printf("ID find: 0x%04x\n", p);

	for (int i = 0; i < 20; ++i) {
		memset(buff, i, 20);
		meterfs_writeRecord(p, buff, p->header.recordsz);
	}

	printf("Reading records:\n");
	for (int i = 0; i < 20; ++i) {
		meterfs_readFile(id, buff, p->header.recordsz, i * p->header.recordsz);
		printf("\n");
		for (int j = 0; j < 20; ++j)
			printf("0x%02x ", buff[j]);
	}

	printf("test2\n");
	ret = meterfs_openFile("test2", &id);
	printf("ID %u, returned %d\n", id, ret);

	printf("test3\n");
	ret = meterfs_openFile("test3", &id);
	printf("ID %u, returned %d\n", id, ret);

	printf("test3\n");
	ret = meterfs_openFile("test3", &id);
	printf("ID %u, returned %d\n", id, ret);

	printf("Closing file test2\n");
	opened_remove(1);

	printf("test3\n");
	ret = meterfs_openFile("test3", &id);
	printf("ID %u, returned %d\n", id, ret);

	printf("test2\n");
	ret = meterfs_openFile("test2", &id);
	printf("ID %u, returned %d\n", id, ret);

	printf("Closing file test1 and test2\n");
	opened_remove(0);
	opened_remove(1);

	printf("test3\n");
	ret = meterfs_openFile("test3", &id);
	printf("ID %u, returned %d\n", id, ret);

	printf("test2\n");
	ret = meterfs_openFile("test2", &id);
	printf("ID %u, returned %d\n", id, ret);

	printf("test1\n");
	ret = meterfs_openFile("test1", &id);
	printf("ID %u, returned %d\n", id, ret);

	printf("Read byte by byte\n");
	int i = 0;
	do {
		ret = meterfs_readFile(id, buff, 1, i);
		printf("0x%02x (ret %d)\n", *buff, ret);
		i += ret;
	} while (ret);


	for (;;);

	for (;;) {
		s = recv(meterfs_common.port, &meterfs_common.msg_buff, sizeof(meterfs_common.msg_buff), &hdr);

		switch (hdr.op) {
			case READ:
				if (s <= sizeof(meterfsdata_t)) {
					err = -EINVAL;
					break;
				}

			case WRITE:
				if (s <= sizeof(meterfsdata_t)) {
					err = -EINVAL;
					break;
				}

			case OPEN:
				if (s != sizeof(meterfsopen_t)) {
					err = -EINVAL;
					break;
				}

				err = meterfs_openFile(meterfs_common.msg_buff.open.name, &id);

				if (hdr.type == NORMAL)
					respond(meterfs_common.port, err, &id, sizeof(id));

				break;

			case CLOSE:
				if (s != sizeof(meterfsclose_t)) {
					err = -EINVAL;
					break;
				}

			case DEVCTL:
				if (s != sizeof(meterfsconfig_t)) {
					err = -EINVAL;
					break;
				}
		}



	}
}

