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
	index_t id;
	union {
		header_t h;
		fileheader_t f;
	} u;

	/* Check if first header is valid */
	flash_read(addr, &u.h, sizeof(u.h));
	if (!(u.h.id.nvalid || memcmp(u.h.magic, magic, 4))) {
		valid0 = 1;
		id = u.h.id;
	}

	/* Check next header */
	meterfs_common.h1Addr = HGRAIN + MAX_FILE_CNT * HGRAIN;

	flash_read(meterfs_common.h1Addr, &u.h, sizeof(u.h));

	if (!(u.h.id.nvalid || memcmp(u.h.magic, magic, 4)))
		valid1 = 1;

	if (!valid0 && !valid1) {
		printf("meterfs: No valid filesystem detected. Formating.\n");
		flash_chipErase();

		u.h.filecnt = 0;
		u.h.id.no = 0;
		u.h.id.nvalid = 0;
		memcpy(u.h.magic, magic, 4);

		flash_write(0, &u.h, sizeof(u.h));
		flash_write(meterfs_common.h1Addr, &u.h, sizeof(u.h));

		return;
	}

	/* Select active header and files table */
	if (valid0 && valid1) {
		if (id.no == (u.h.id.no + 1) % (1 << 31) || id.no == u.h.id.no)
			meterfs_common.hcurrAddr = meterfs_common.h1Addr;
		else
			meterfs_common.hcurrAddr = 0;

		flash_read(meterfs_common.hcurrAddr + offsetof(header_t, filecnt), &meterfs_common.filecnt, sizeof(meterfs_common.filecnt));

		return;
	}

	/* There should be copy of file table at all times. Fix it if necessary */
	if (!valid0) {
		printf("meterfs: Filetable header #0 is damaged - repairing\n");
		src = meterfs_common.h1Addr;
		dst = 0;
		meterfs_eraseFileTable(0);
		meterfs_common.hcurrAddr = meterfs_common.h1Addr;
	}
	else {
		printf("meterfs: Filetable header #1 is damaged - repairing\n");
		src = 0;
		dst = meterfs_common.h1Addr;
		meterfs_eraseFileTable(1);
		meterfs_common.hcurrAddr = 0;
	}

	flash_read(meterfs_common.hcurrAddr + offsetof(header_t, filecnt), &meterfs_common.filecnt, sizeof(meterfs_common.filecnt));

	/* Copy header */
	flash_read(src, &u.h, sizeof(u.h));
	flash_write(dst, &u.h, sizeof(u.h));

	src += HGRAIN;
	dst += HGRAIN;

	/* Copy file info */
	for (i = 0; i < meterfs_common.filecnt; ++i) {
		flash_read(src, &u.f, sizeof(u.f));
		flash_write(dst, &u.f, sizeof(u.f));

		src += HGRAIN;
		dst += HGRAIN;
	}
}


int meterfs_getFileInfoName(const char *name, fileheader_t *f)
{
	fileheader_t t;
	size_t i, filecnt;

	flash_read(meterfs_common.hcurrAddr + offsetof(header_t, filecnt), &filecnt, sizeof(filecnt));

	for (i = 0; i < filecnt; ++i) {
		flash_read(meterfs_common.hcurrAddr + HGRAIN + (i * HGRAIN) + offsetof(fileheader_t, name), &t.name, sizeof(t.name));
		if (strncmp(name, t.name, sizeof(t.name)) == 0) {
			if (f != NULL)
				flash_read(meterfs_common.hcurrAddr + HGRAIN + (i * HGRAIN), f, sizeof(*f));
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
	index_t id;

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
		flash_read(baddr + offset + offsetof(entry_t, id), &id, sizeof(id));
		if (!id.nvalid) {
			f->lastidx = id;
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
		flash_read(baddr + offset + offsetof(entry_t, id), &id, sizeof(id));
		if (!id.nvalid && ((f->lastidx.no + interval) % (1 << 31)) == id.no) {
			f->lastidx = id;
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
		flash_read(baddr + offset + offsetof(entry_t, id), &id, sizeof(id));
		if (!id.nvalid && ((f->firstidx.no + interval) % (1 << 31)) == id.no) {
			f->firstidx = id;
			f->firstoff = offset;
			diff -= interval;
			if (interval == 1 || interval == -1)
				continue;
		}

		interval /= 2;
	}

	f->recordcnt = f->lastidx.no - f->firstidx.no + 1;
}


int meterfs_writeRecord(file_t *f, const void *buff, size_t bufflen)
{
	/* This function assumes that f contains valid lastidx and lastoff */
	entry_t e;
	unsigned int offset;

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

	flash_write(f->header.sector * meterfs_common.sectorsz + offset + sizeof(entry_t), (void *)buff, bufflen);
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
	index_t id;
	unsigned int addr, pos;

	if (f->firstidx.nvalid || idx > f->recordcnt)
		return -ENOENT;

	/* Calculate record position in flash */
	pos = (f->firstoff / (f->header.recordsz + sizeof(entry_t))) + idx;
	pos = pos % ((f->header.sectorcnt * meterfs_common.sectorsz) / (f->header.recordsz + sizeof(entry_t)));
	addr = pos * (f->header.recordsz + sizeof(entry_t)) + f->header.sector * meterfs_common.sectorsz;

	/* Check if entry's valid */
	flash_read(addr + offsetof(entry_t, id), &id, sizeof(id));

	if (id.nvalid || id.no != f->firstidx.no + idx)
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


int meterfs_open(oid_t *oid)
{
	if (oid->port != meterfs_common.port)
		return -ENOENT;

	if (node_getById(oid->id) != NULL)
		return EOK;

	return -ENOENT;
}


int meterfs_close(oid_t *oid)
{
	if (oid->port != meterfs_common.port)
		return -ENOENT;

	return node_put(oid->id);
}


int meterfs_lookup(const char *name, oid_t *res)
{
	file_t f;
	char bname[sizeof(f.header.name)];
	int i = 0, j, err;

	if (name[0] == '/')
		++i;

	for (j = 0; j < sizeof(bname); ++j, ++i) {
		bname[j] = name[i];

		if (name[i] == '/')
			return -ENOENT;

		if (name[i] == '\0')
			break;
	}

	if (node_getByName(bname, &res->id) != NULL) {
		res->port = meterfs_common.port;

		return EOK;
	}

	if ((err = meterfs_getFileInfoName(bname, &f.header)) < 0)
		return -ENOENT;

	res->port = meterfs_common.port;
	res->id = err;

	meterfs_getFilePos(&f);

	node_add(&f, res->id);

	return EOK;
}


int meterfs_create(const char *name, oid_t *oid)
{
	file_t f;

	if (meterfs_getFileInfoName(name, &f.header) >= 0)
		return -EEXIST;

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

	oid->port = meterfs_common.port;
	oid->id = node_getMaxId();

	node_add(&f, oid->id);

	return EOK;
}

#if 0
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

#endif


int meterfs_readFile(oid_t *oid, offs_t offs, char *buff, size_t bufflen)
{
	file_t *f;
	unsigned int idx;
	size_t chunk, i = 0, pos = offs;

	if (oid->port != meterfs_common.port || (f = node_getById(oid->id)) == NULL)
		return -ENOENT;

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


int meterfs_writeFile(oid_t *oid, const char *buff, size_t bufflen)
{
	file_t *f;

	if (oid->port != meterfs_common.port || (f = node_getById(oid->id)) == NULL)
		return -ENOENT;

	return meterfs_writeRecord(f, buff, bufflen);
}


int meterfs_devctl(meterfs_i_devctl_t *devctl)
{
	return -ENOSYS;
}


int main(void)
{
	msg_t msg;
	unsigned int rid;
	meterfs_i_devctl_t *idevctl = (meterfs_i_devctl_t *)msg.i.raw;
	meterfs_o_devctl_t *odevctl = (meterfs_o_devctl_t *)msg.o.raw;

	while (lookup("/multidrv", &multidrv) < 0)
		usleep(10000);

	spi_init();
	flash_init(&meterfs_common.flashsz, &meterfs_common.sectorsz);
	node_init();

	printf("meterfs: Started\n");

//flash_chipErase();
	meterfs_checkfs();

	printf("meterfs: Filesystem check done\n");

	portCreate(&meterfs_common.port);
	portRegister(meterfs_common.port, "/", NULL);

	for (;;) {
		if (msgRecv(meterfs_common.port, &msg, &rid) < 0)
			continue;

		switch (msg.type) {
			case mtRead:
				msg.o.io.err = meterfs_readFile(&msg.i.io.oid, msg.i.io.offs, msg.o.data, msg.o.size);
				break;

			case mtWrite:
				msg.o.io.err = meterfs_writeFile(&msg.i.io.oid, msg.i.data, msg.i.size);
				break;

			case mtLookup:
				msg.o.lookup.err = meterfs_lookup(msg.i.data, &msg.o.lookup.res);
				break;

			case mtOpen:
				msg.o.io.err = meterfs_open(&msg.i.openclose.oid);
				break;

			case mtClose:
				msg.o.io.err = meterfs_close(&msg.i.openclose.oid);
				break;

			case mtDevCtl:
				odevctl->err = meterfs_devctl(idevctl);
				break;

			default:
				msg.o.io.err = -EINVAL;
				break;
		}

		msgRespond(meterfs_common.port, &msg, rid);
	}
}

