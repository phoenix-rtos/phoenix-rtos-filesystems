/*
 * Phoenix-RTOS
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
#include <sys/mount.h>
#include <sys/msg.h>
#include <string.h>

#include "meterfs.h"
#include "spi.h"
#include "flash.h"
#include "files.h"
#include "node.h"

#define TOTAL_SIZE(f)           (((f)->filesz * ((f)->recordsz + sizeof(entry_t))) / (f)->recordsz)
#define SECTORS(f, sectorsz)    (((TOTAL_SIZE(f) + sectorsz - 1) / sectorsz) + 1)


static const unsigned char magic[4] = { 0xaa, 0x41, 0x4b, 0x55 };


msg_t gmsg;


oid_t multidrv;


struct {
	unsigned int port;
	unsigned int h1Addr;
	unsigned int hcurrAddr;
	unsigned int filecnt;
	size_t sectorsz;
	size_t flashsz;
} meterfs_common;


/*
 * Meterfs internal functions
 */


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

	if (name == NULL)
		return -EINVAL;

	flash_read(meterfs_common.hcurrAddr, &header, sizeof(header));

	for (i = 0; i < header.filecnt; ++i) {
		flash_read(meterfs_common.hcurrAddr + HGRAIN + (i * HGRAIN), &t, sizeof(t));
		if (strncmp(name, t.name, sizeof(f->name)) == 0) {
			if (f != NULL)
				memcpy(f, &t, sizeof(t));
			return i;
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
	if (meterfs_getFileInfoName(f->name, &u.t) < 0)
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
		flash_read(meterfs_common.hcurrAddr + HGRAIN + (i * HGRAIN), &u.t, sizeof(fileheader_t));
		if (strcmp(f->name, u.t.name) == 0)
			flash_write(headerNew + HGRAIN + (i * HGRAIN), f, sizeof(fileheader_t));
		else
			flash_write(headerNew + HGRAIN + (i * HGRAIN), &u.t, sizeof(fileheader_t));
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

	if (bufflen > f->header.recordsz)
		bufflen = f->header.recordsz;

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

	flash_write(f->header.sector * meterfs_common.sectorsz + offset + sizeof(entry_t), buff, bufflen);
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


/*
 * Meterfs interface functions
 */


int meterfs_openFile(char *name, unsigned int *id)
{
	file_t f;
	header_t h;
	oid_t oid;
	int fid;

	if (node_claim(name, id) == 0)
		return 0;

	if ((fid = meterfs_getFileInfoName(name, &f.header)) < 0) {
		/* We have to create new file (for now only in RAM) */
		strncpy(f.header.name, name, sizeof(f.header.name));
		f.header.filesz = 0;
		f.header.recordsz = 0;
		f.header.sector = 0;
		f.header.sectorcnt = 0;

		f.firstidx.no = 0;
		f.firstidx.nvalid = 1;
		f.lastidx.no = 0;
		f.lastidx.nvalid = 1;
		f.firstoff = 0;
		f.lastoff = 0;
		f.recordcnt = 0;

		if (flash_read(meterfs_common.hcurrAddr, &h, sizeof(header_t)) != EOK)
			return -1;

		fid = h.filecnt;
	}
	else {
		meterfs_getFilePos(&f);
	}

	oid.port = meterfs_common.port;
	oid.id = fid;

	if (node_add(&f, NULL, &oid) != 0)
		return -ENOMEM;

	*id = oid.id;

	return 0;
}


size_t meterfs_readFile(unsigned int id, unsigned char *buff, size_t bufflen, size_t pos)
{
	file_t *f;
	unsigned int idx;
	size_t chunk, i = 0;

	if ((f = node_findFile(id)) == NULL)
		return -ENOENT;

	if (f->header.sector == 0) {
		/* File not alocated yet */
		return -EINVAL;
	}

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


int meterfs_writeFile(unsigned int id, unsigned char *buff, size_t bufflen)
{
	file_t *f;

	if ((f = node_findFile(id)) == NULL)
		return -ENOENT;

	if (f->header.sector == 0 || f->header.filesz == 0 || f->header.recordsz == 0) {
		/* File not alocated yet */
		return -EINVAL;
	}

	return meterfs_writeRecord(f, buff, bufflen);
}


int meterfs_configure(unsigned int id, unsigned int cmd, unsigned long arg)
{
	file_t *f;

	if (cmd != METERFS_CHIPERASE && (f = node_findFile(id)) == NULL)
		return -ENOENT;

	switch (cmd) {
		case METERFS_ALOCATE:
			if (f->header.sector != 0)
				return -EPERM;

			if (arg < 2)
				return -EINVAL;

			if (f->header.filesz == 0 || f->header.recordsz == 0)
				return -EINVAL;

			if (f->header.filesz > (arg - 1) * meterfs_common.sectorsz)
				return -EINVAL;

			f->header.sectorcnt = arg;

			f->firstidx.no = 0;
			f->firstidx.nvalid = 1;
			f->lastidx.no = 0;
			f->lastidx.nvalid = 1;
			f->firstoff = 0;
			f->lastoff = 0;
			f->recordcnt = 0;

			return meterfs_alocateFile(&f->header);

		case METERFS_RESIZE:
			if (f->header.sector != 0) {
				if (f->header.recordsz && f->header.recordsz > arg)
					return -EINVAL;

				if (arg > (f->header.sectorcnt - 1) * meterfs_common.sectorsz)
					return -EINVAL;

				f->header.filesz = arg;

				f->firstidx.no = 0;
				f->firstidx.nvalid = 1;
				f->lastidx.no = 0;
				f->lastidx.nvalid = 1;
				f->firstoff = 0;
				f->lastoff = 0;
				f->recordcnt = 0;

				return meterfs_updateFileInfo(&f->header);
			}
			else {
				if (f->header.recordsz && f->header.recordsz > arg)
					return -EINVAL;

				f->header.filesz = arg;

				return EOK;
			}

		case METERFS_RECORDSZ:
			if (f->header.sector != 0) {
				if (f->header.filesz && arg > f->header.filesz)
					return -EINVAL;

				if (arg > (f->header.sectorcnt - 1) * meterfs_common.sectorsz)
					return -EINVAL;

				f->header.recordsz = arg;

				f->firstidx.no = 0;
				f->firstidx.nvalid = 1;
				f->lastidx.no = 0;
				f->lastidx.nvalid = 1;
				f->firstoff = 0;
				f->lastoff = 0;
				f->recordcnt = 0;

				return meterfs_updateFileInfo(&f->header);
			}
			else {
				if (f->header.filesz && f->header.filesz < arg)
					return -EINVAL;

				f->header.recordsz = arg;

				return EOK;
			}

		case METERFS_CHIPERASE:
			flash_chipErase();
			node_cleanAll();
			meterfs_checkfs();

			return EOK;

		default:
			return -EINVAL;
	}
}

#if 0
int meterfs_lookup(fslookup_t *lckup)
{
	size_t len, i;
	oid_t oid;
	int fid;

	if (lckup == NULL)
		return -EINVAL;

	len = strlen(lckup->path);

	/* This fs does not support directories */
	for (i = lckup->pos; i < len; ++i) {
		if (lckup->path[i] == '/')
			return -EINVAL;
	}

	/* String does not contain '/', so path contains only filename */
	if (node_findMount(&oid, lckup->path + lckup->pos) == EOK) {
		/* Found mounted port */
		lckup->pos = len;
		lckup->oid = oid;
		return EOK;
	}

	/* Mount not found, look for file */
	if ((fid = meterfs_getFileInfoName(lckup->path + lckup->pos, NULL)) < 0) {
		/* File not found */
		return -ENOENT;
	}

	lckup->pos = len;
	lckup->oid.port = meterfs_common.port;
	lckup->oid.id = fid;

	return EOK;
}


void dump_header(void)
{
	header_t h;
	fileheader_t f;
	int i;

	printf("curr header 0x%08x\n", meterfs_common.hcurrAddr);

	flash_read(meterfs_common.hcurrAddr, &h, sizeof(h));

	printf("index: %d\n", h.id.no);
	printf("files: %d\n", h.filecnt);

	if (h.filecnt > 99999) {
		printf("Invalid file count\n");
		return;
	}

	for (i = 0; i < h.filecnt; ++i) {
		flash_read(meterfs_common.hcurrAddr + (i+1) * HGRAIN, &f, sizeof(f));
		printf("name: %s\n", f.name);
		printf("\tsector: %d\n", f.sector);
		printf("\tsectorcnt: %d\n", f.sectorcnt);
		printf("\tsize: %d\n", f.filesz);
		printf("\trecordsz: %d\n", f.recordsz);
		printf("\n");
	}
}


void meterfs_test(void *arg)
{
	int ret;
	oid_t oid;

	union {
		fsopen_t open;
		fsdata_t data;
		fsclose_t close;
		fsfcntl_t fcntl;
		unsigned char buff[sizeof(fsdata_t) + 20];
	} u;

	printf("Creating files\n");

	dump_header();

	u.fcntl.cmd = METERFS_CHIPERASE;
	ret = send(meterfs_common.port, DEVCTL, &u.fcntl, sizeof(u.fcntl), NORMAL, NULL, 0);
	printf("Erase ret %d\n", ret);

	printf("test1\n");
	strncpy(u.open.name, "test1", 8);
	u.open.mode = 0;
	ret = send(meterfs_common.port, OPEN, &u.open, sizeof(u.open) + 6, NORMAL, &oid, sizeof(oid));
	printf("open ret %d id %u\n", ret, oid.id);

	u.fcntl.id = oid.id;
	u.fcntl.cmd = METERFS_RESIZE;
	u.fcntl.arg = 1000;
	ret = send(meterfs_common.port, DEVCTL, &u.fcntl, sizeof(u.fcntl), NORMAL, NULL, 0);
	printf("resize ret %d\n", ret);

	u.fcntl.id = oid.id;
	u.fcntl.cmd = METERFS_RECORDSZ;
	u.fcntl.arg = 10;
	ret = send(meterfs_common.port, DEVCTL, &u.fcntl, sizeof(u.fcntl), NORMAL, NULL, 0);
	printf("recordcnt ret %d\n", ret);

	u.fcntl.id = oid.id;
	u.fcntl.cmd = METERFS_ALOCATE;
	u.fcntl.arg = 2;
	ret = send(meterfs_common.port, DEVCTL, &u.fcntl, sizeof(u.fcntl), NORMAL, NULL, 0);
	printf("alocate ret %d\n", ret);

	u.close = oid.id;
	ret = send(meterfs_common.port, CLOSE, &u.close, sizeof(u.close), NORMAL, NULL, 0);
	printf("close ret %d\n", ret);


	dump_header();


	printf("test2\n");
	strncpy(u.open.name, "test2", 8);
	u.open.mode = 0;
	ret = send(meterfs_common.port, OPEN, &u.open, sizeof(u.open) + 6, NORMAL, &oid, sizeof(oid));
	printf("open ret %d id %u\n", ret, oid.id);

	u.fcntl.id = oid.id;
	u.fcntl.cmd = METERFS_RESIZE;
	u.fcntl.arg = 1000;
	ret = send(meterfs_common.port, DEVCTL, &u.fcntl, sizeof(u.fcntl), NORMAL, NULL, 0);
	printf("resize ret %d\n", ret);

	u.fcntl.id = oid.id;
	u.fcntl.cmd = METERFS_RECORDSZ;
	u.fcntl.arg = 15;
	ret = send(meterfs_common.port, DEVCTL, &u.fcntl, sizeof(u.fcntl), NORMAL, NULL, 0);
	printf("recordcnt ret %d\n", ret);

	u.fcntl.id = oid.id;
	u.fcntl.cmd = METERFS_ALOCATE;
	u.fcntl.arg = 2;
	ret = send(meterfs_common.port, DEVCTL, &u.fcntl, sizeof(u.fcntl), NORMAL, NULL, 0);
	printf("alocate ret %d\n", ret);

	u.close = oid.id;
	ret = send(meterfs_common.port, CLOSE, &u.close, sizeof(u.close), NORMAL, NULL, 0);
	printf("close ret %d\n", ret);


	dump_header();


	printf("test3\n");
	strncpy(u.open.name, "test3", 8);
	u.open.mode = 0;
	ret = send(meterfs_common.port, OPEN, &u.open, sizeof(u.open) + 6, NORMAL, &oid, sizeof(oid));
	printf("open ret %d id %u\n", ret, oid.id);

	u.fcntl.id = oid.id;
	u.fcntl.cmd = METERFS_RESIZE;
	u.fcntl.arg = 100;
	ret = send(meterfs_common.port, DEVCTL, &u.fcntl, sizeof(u.fcntl), NORMAL, NULL, 0);
	printf("resize ret %d\n", ret);

	u.fcntl.id = oid.id;
	u.fcntl.cmd = METERFS_RECORDSZ;
	u.fcntl.arg = 5;
	ret = send(meterfs_common.port, DEVCTL, &u.fcntl, sizeof(u.fcntl), NORMAL, NULL, 0);
	printf("recordcnt ret %d\n", ret);

	u.fcntl.id = oid.id;
	u.fcntl.cmd = METERFS_ALOCATE;
	u.fcntl.arg = 2;
	ret = send(meterfs_common.port, DEVCTL, &u.fcntl, sizeof(u.fcntl), NORMAL, NULL, 0);
	printf("alocate ret %d\n", ret);

	u.close = oid.id;
	ret = send(meterfs_common.port, CLOSE, &u.close, sizeof(u.close), NORMAL, NULL, 0);
	printf("close ret %d\n", ret);


	dump_header();

	int i;
	for (i = 0; i < 20; ++i) {
		strncpy(u.open.name, "test4", 8);
		u.open.name[4] += i;
		printf("%s\n", u.open.name);
		u.open.mode = 0;
		ret = send(meterfs_common.port, OPEN, &u.open, sizeof(u.open) + 6, NORMAL, &oid, sizeof(oid));
		printf("open ret %d id %u\n", ret, oid.id);

		u.fcntl.id = oid.id;
		u.fcntl.cmd = METERFS_RESIZE;
		u.fcntl.arg = 100;
		ret = send(meterfs_common.port, DEVCTL, &u.fcntl, sizeof(u.fcntl), NORMAL, NULL, 0);
		printf("resize ret %d\n", ret);

		u.fcntl.id = oid.id;
		u.fcntl.cmd = METERFS_RECORDSZ;
		u.fcntl.arg = 5;
		ret = send(meterfs_common.port, DEVCTL, &u.fcntl, sizeof(u.fcntl), NORMAL, NULL, 0);
		printf("recordcnt ret %d\n", ret);

		u.fcntl.id = oid.id;
		u.fcntl.cmd = METERFS_ALOCATE;
		u.fcntl.arg = 2;
		ret = send(meterfs_common.port, DEVCTL, &u.fcntl, sizeof(u.fcntl), NORMAL, NULL, 0);
		printf("alocate ret %d\n", ret);

		u.close = oid.id;
		ret = send(meterfs_common.port, CLOSE, &u.close, sizeof(u.close), NORMAL, NULL, 0);
		printf("close ret %d\n", ret);

		dump_header();
	}


	printf("test1\n");
	strcpy(u.open.name, "test1");
	u.open.mode = 0;
	ret = send(meterfs_common.port, OPEN, &u.open, sizeof(u.open) + 6, NORMAL, &oid, sizeof(oid));
	printf("ID %u, returned %d\n", oid.id, ret);

	for (int i = 0; i < 20; ++i) {
		printf("Writing 0x%02x\n", i);
		u.data.id = oid.id;
		u.data.pos = 0;
		memset(u.data.buff, i, 20);
		ret = send(meterfs_common.port, WRITE, &u.data, sizeof(u.data) + 20, NORMAL, NULL, 0);
		printf("ret %d\n", ret);
	}

	printf("Reading records:\n");
	for (int i = 0; i < 20; ++i) {
		u.data.id = oid.id;
		u.data.pos = i * 10;
		ret = send(meterfs_common.port, READ, &u.data, sizeof(u.data), NORMAL, u.data.buff, 10);
		printf("ret %d\n", ret);
		for (int j = 0; j < 10; ++j)
			printf("0x%02x ", u.data.buff[j]);
	}

	printf("test2\n");
	strcpy(u.open.name, "test2");
	u.open.mode = 0;
	ret = send(meterfs_common.port, OPEN, &u.open, sizeof(u.open) + 6, NORMAL, &oid, sizeof(oid));
	printf("ID %u, returned %d\n", oid.id, ret);

	printf("test3\n");
	strcpy(u.open.name, "test3");
	u.open.mode = 0;
	ret = send(meterfs_common.port, OPEN, &u.open, sizeof(u.open) + 6, NORMAL, &oid, sizeof(oid));
	printf("ID %u, returned %d\n", oid.id, ret);

	printf("test3\n");
	strcpy(u.open.name, "test3");
	u.open.mode = 0;
	ret = send(meterfs_common.port, OPEN, &u.open, sizeof(u.open) + 6, NORMAL, &oid, sizeof(oid));
	printf("ID %u, returned %d\n", oid.id, ret);

	u.close = 1;
	ret = send(meterfs_common.port, CLOSE, &u.close, sizeof(u.close), NORMAL, NULL, 0);
	printf("Closed file test2 (%d)\n", ret);

	printf("test3\n");
	strcpy(u.open.name, "test3");
	u.open.mode = 0;
	ret = send(meterfs_common.port, OPEN, &u.open, sizeof(u.open) + 6, NORMAL, &oid, sizeof(oid));
	printf("ID %u, returned %d\n", oid.id, ret);

	printf("test2\n");
	strcpy(u.open.name, "test2");
	u.open.mode = 0;
	ret = send(meterfs_common.port, OPEN, &u.open, sizeof(u.open) + 6, NORMAL, &oid, sizeof(oid));
	printf("ID %u, returned %d\n", oid.id, ret);

	u.close = 0;
	ret = send(meterfs_common.port, CLOSE, &u.close, sizeof(u.close), NORMAL, NULL, 0);
	printf("Closed file test1 (%d)\n", ret);

	u.close = 1;
	ret = send(meterfs_common.port, CLOSE, &u.close, sizeof(u.close), NORMAL, NULL, 0);
	printf("Closed file test2 (%d)\n", ret);

	printf("test3\n");
	strcpy(u.open.name, "test3");
	u.open.mode = 0;
	ret = send(meterfs_common.port, OPEN, &u.open, sizeof(u.open) + 6, NORMAL, &oid, sizeof(oid));
	printf("ID %u, returned %d\n", oid.id, ret);

	printf("test2\n");
	strcpy(u.open.name, "test2");
	u.open.mode = 0;
	ret = send(meterfs_common.port, OPEN, &u.open, sizeof(u.open) + 6, NORMAL, &oid, sizeof(oid));
	printf("ID %u, returned %d\n", oid.id, ret);

	printf("test1\n");
	strcpy(u.open.name, "test1");
	u.open.mode = 0;
	ret = send(meterfs_common.port, OPEN, &u.open, sizeof(u.open) + 6, NORMAL, &oid, sizeof(oid));
	printf("ID %u, returned %d\n", oid.id, ret);

	printf("Read byte by byte\n");
	u.data.id = oid.id;
	u.data.pos = 0;
	do {
		ret = send(meterfs_common.port, READ, &u.data, sizeof(u.data), NORMAL, u.data.buff, 1);
		printf("0x%02x (ret %d)\n", *(u.data.buff), ret);
		++u.data.pos;
	} while (ret);

	printf("test2\n");
	strcpy(u.open.name, "test2");
	u.open.mode = 0;
	ret = send(meterfs_common.port, OPEN, &u.open, sizeof(u.open) + 6, NORMAL, &oid, sizeof(oid));
	printf("ID %u, returned %d\n", oid.id, ret);

	printf("Write byte by byte\n");

	for (int i = 0; i < 20; ++i) {
		printf("Writing 0x%02x\n", i);
		u.data.id = oid.id;
		u.data.pos = 0;
		memset(u.data.buff, i, 20);
		send(meterfs_common.port, WRITE, &u.data, sizeof(u.data) + i + 1, NORMAL, &ret, sizeof(ret));
		printf("Wrote %d bytes\n", ret);
	}

	printf("Reading records:\n");
	for (int i = 0; i < 20; ++i) {
		u.data.id = oid.id;
		u.data.pos = i * 15;
		ret = send(meterfs_common.port, READ, &u.data, sizeof(u.data), NORMAL, u.data.buff, 20);
		printf("ret %d\n", ret);
		for (int j = 0; j < 20; ++j)
			printf("0x%02x ", u.data.buff[j]);
	}

	printf("Reading records from invalid ID:\n");
	for (int i = 0; i < 20; ++i) {
		u.data.id = 666;
		u.data.pos = i * 15;
		ret = send(meterfs_common.port, READ, &u.data, sizeof(u.data), NORMAL, u.data.buff, 20);
		printf("ret %d\n", ret);
		for (int j = 0; j < 20; ++j)
			printf("0x%02x ", u.data.buff[j]);
	}

	for (;;);
}


void dummyDriver(void *arg)
{
	oid_t oid;
	unsigned int myport;
	int ret;
	union {
		fsopen_t open;
		fsfcntl_t fcntl;
		fsclose_t close;
	} u;

	portCreate(&myport);

	printf("Mounting dummy (port %d), ", myport);
	printf("ret %d\n", mount("/dummy", myport));

	printf("Lookup \"/plik\", ret %d\n", lookup("/plik", &oid));
	printf("OID: port %d, id %d\n", oid.port, oid.id);

	printf("Lookup \"/dummy\", ret %d\n", lookup("/dummy", &oid));
	printf("OID: port %d, id %d\n", oid.port, oid.id);

	printf("Creating \"plik\"\n");
	printf("plik\n");
	strncpy(u.open.name, "plik", 8);
	u.open.mode = 0;
	ret = send(meterfs_common.port, OPEN, &u.open, sizeof(u.open) + 5, NORMAL, &oid, sizeof(oid));
	printf("open ret %d id %u\n", ret, oid.id);

	u.fcntl.id = oid.id;
	u.fcntl.cmd = METERFS_ALOCATE;
	u.fcntl.arg = 2;
	ret = send(meterfs_common.port, DEVCTL, &u.fcntl, sizeof(u.fcntl), NORMAL, NULL, 0);
	printf("alocate ret %d\n", ret);

	u.fcntl.id = oid.id;
	u.fcntl.cmd = METERFS_RESIZE;
	u.fcntl.arg = 1000;
	ret = send(meterfs_common.port, DEVCTL, &u.fcntl, sizeof(u.fcntl), NORMAL, NULL, 0);
	printf("resize ret %d\n", ret);

	u.fcntl.id = oid.id;
	u.fcntl.cmd = METERFS_RECORDSZ;
	u.fcntl.arg = 10;
	ret = send(meterfs_common.port, DEVCTL, &u.fcntl, sizeof(u.fcntl), NORMAL, NULL, 0);
	printf("recordcnt ret %d\n", ret);

	u.close = oid.id;
	ret = send(meterfs_common.port, CLOSE, &u.close, sizeof(u.close), NORMAL, NULL, 0);
	printf("close ret %d\n", ret);

	printf("Lookup \"/plik\", ret %d", lookup("/plik", &oid));
	printf("OID: port %d, id %d\n", oid.port, oid.id);


	for (;;)
		usleep(100000);

}
#endif


int main(void)
{
	int s, err, cnt;
	unsigned int id;
	oid_t oid;

	spi_init();
	flash_init(&meterfs_common.flashsz, &meterfs_common.sectorsz);
	node_init();

//flash_chipErase();
	meterfs_checkfs();
	portCreate(&meterfs_common.port);
	portRegister(meterfs_common.port, "/", NULL);
#if 0
//beginthread(meterfs_test, 4, malloc(1024), 1024, NULL);
//beginthread(dummyDriver, 4, malloc(1024), 1024, NULL);

	for (;;) {
		s = recv(meterfs_common.port, &meterfs_common.msg_buff.buff, sizeof(meterfs_common.msg_buff.buff), &hdr);

		if (hdr.type == NOTIFY)
			continue;

		switch (hdr.op) {
			case READ:
				if (s != sizeof(fsdata_t)) {
					respond(meterfs_common.port, EINVAL, NULL, 0);
					break;
				}

				cnt = hdr.rsize > sizeof(meterfs_common.msg_buff) ? sizeof(meterfs_common.msg_buff) : hdr.rsize;

				cnt = meterfs_readFile(meterfs_common.msg_buff.data.id, meterfs_common.msg_buff.buff,
					cnt, meterfs_common.msg_buff.data.pos);

				if (cnt < 0)
					respond(meterfs_common.port, cnt, NULL, 0);
				else
					respond(meterfs_common.port, EOK, meterfs_common.msg_buff.buff, cnt);
				break;

			case WRITE:
				if (s <= sizeof(fsdata_t)) {
					respond(meterfs_common.port, EINVAL, NULL, 0);
					break;
				}

				cnt = meterfs_writeFile(meterfs_common.msg_buff.data.id, meterfs_common.msg_buff.data.buff,
					s - sizeof(fsdata_t));

				if (cnt < 0)
					respond(meterfs_common.port, cnt, NULL, 0);
				else
					respond(meterfs_common.port, EOK, &cnt, sizeof(cnt));
				break;

			case OPEN:
				if (s < sizeof(fsopen_t) + 2) {
					respond(meterfs_common.port, EINVAL, NULL, 0);
					break;
				}

				err = meterfs_openFile(meterfs_common.msg_buff.open.name, &id);
				oid.port = meterfs_common.port;
				oid.id = id;

				respond(meterfs_common.port, err, &oid, sizeof(oid));
				break;

			case CLOSE:
				if (s != sizeof(fsclose_t)) {
					respond(meterfs_common.port, EINVAL, NULL, 0);
					break;
				}

				err = node_remove(meterfs_common.msg_buff.close);

				respond(meterfs_common.port, err, NULL, 0);
				break;

			case MOUNT:
				if (s <= sizeof(fsmount_t)) {
					respond(meterfs_common.port, EINVAL, NULL, 0);
					break;
				}

				err = meterfs_mount(meterfs_common.msg_buff.mount.port, meterfs_common.msg_buff.mount.name);

				respond(meterfs_common.port, err, NULL, 0);
				break;

			case UMOUNT:
				if (s < 2) {
					respond(meterfs_common.port, EINVAL, NULL, 0);
					break;
				}

				err = meterfs_umount((const char *)meterfs_common.msg_buff.buff);

				respond(meterfs_common.port, err, NULL, 0);
				break;

			case LOOKUP:
				if (s <= sizeof(fslookup_t)) {
					respond(meterfs_common.port, EINVAL, NULL, 0);
					break;
				}

				err = meterfs_lookup(&meterfs_common.msg_buff.lckup);

				respond(meterfs_common.port, err, &meterfs_common.msg_buff.lckup, s);
				break;

			case DEVCTL:
				if (s != sizeof(fsfcntl_t)) {
					respond(meterfs_common.port, EINVAL, NULL, 0);
					break;
				}

				err = meterfs_configure(meterfs_common.msg_buff.fcntl.id, meterfs_common.msg_buff.fcntl.cmd,
					meterfs_common.msg_buff.fcntl.arg);

				respond(meterfs_common.port, err, NULL, 0);
				break;

			default:
				respond(meterfs_common.port, EINVAL, NULL, 0);
				break;
		}
	}
#endif
}

