/*
 * Phoenix-RTOS
 *
 * Meterfs Library
 *
 * Copyright 2017, 2018, 2020 Phoenix Systems
 * Author: Aleksander Kaminski, Hubert Buczynski
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
#include <string.h>

#include "meterfs.h"
#include "files.h"
#include "node.h"

#define TOTAL_SIZE(f)           ((((f)->filesz / (f)->recordsz) * ((f)->recordsz + sizeof(entry_t))) / (f)->recordsz)
#define SECTORS(f, sectorsz)    (((TOTAL_SIZE(f) + sectorsz - 1) / sectorsz) + 1)

#define LOG_INFO(str, ...)      do { if(0) printf(str "\n"); } while(0)


#define MIN_PARTITIONS_SECTORS_NB    3


static const unsigned char magic[4] = { 0xaa, 0x41, 0x4b, 0x55 };


/*
 * Meterfs internal functions
 */


void meterfs_eraseFileTable(unsigned int n, meterfs_ctx_t *ctx)
{
	unsigned int addr, i;
	size_t sectorcnt;

	if (n != 0 && n != 1)
		return;

	addr = (n == 0) ? 0 : ctx->h1Addr;
	sectorcnt = HGRAIN + MAX_FILE_CNT * HGRAIN;
	sectorcnt += ctx->sectorsz - 1;
	sectorcnt /= ctx->sectorsz;

	for (i = 0; i < sectorcnt; ++i)
		ctx->eraseSector(ctx->offset + addr + i * ctx->sectorsz);
}


void meterfs_checkfs(meterfs_ctx_t *ctx)
{
	unsigned int addr = 0, valid0 = 0, valid1 = 0, src, dst, i;
	index_t id;
	union {
		header_t h;
		fileheader_t f;
	} u;


	/* Check if first header is valid */
	ctx->read(ctx->offset + addr, &u.h, sizeof(u.h));
	if (!(u.h.id.nvalid || memcmp(u.h.magic, magic, 4))) {
		valid0 = 1;
		id = u.h.id;
	}

	/* Check next header */
	ctx->h1Addr = HGRAIN + MAX_FILE_CNT * HGRAIN;

	ctx->read(ctx->offset + ctx->h1Addr, &u.h, sizeof(u.h));

	if (!(u.h.id.nvalid || memcmp(u.h.magic, magic, 4)))
		valid1 = 1;

	if (!valid0 && !valid1) {
		LOG_INFO("meterfs: No valid filesystem detected. Formating.");
		ctx->partitionErase();

		u.h.filecnt = 0;
		u.h.id.no = 0;
		u.h.id.nvalid = 0;
		memcpy(u.h.magic, magic, 4);

		ctx->write(ctx->offset + 0, &u.h, sizeof(u.h));
		ctx->write(ctx->offset + ctx->h1Addr, &u.h, sizeof(u.h));

		return;
	}

	/* Select active header and files table */
	if (valid0 && valid1) {
		if (id.no + 1 == u.h.id.no)
			ctx->hcurrAddr = ctx->h1Addr;
		else
			ctx->hcurrAddr = 0;

		ctx->read(ctx->offset + ctx->hcurrAddr + offsetof(header_t, filecnt), &ctx->filecnt, sizeof(ctx->filecnt));

		return;
	}

	/* There should be copy of file table at all times. Fix it if necessary */
	if (!valid0) {
		LOG_INFO("meterfs: Filetable header #0 is damaged - repairing.");
		src = ctx->h1Addr;
		dst = 0;
		meterfs_eraseFileTable(0, ctx);
		ctx->hcurrAddr = ctx->h1Addr;
	}
	else {
		LOG_INFO("meterfs: Filetable header #1 is damaged - repairing.");
		src = 0;
		dst = ctx->h1Addr;
		meterfs_eraseFileTable(1, ctx);
		ctx->hcurrAddr = 0;
	}

	ctx->read(ctx->offset + ctx->hcurrAddr + offsetof(header_t, filecnt), &ctx->filecnt, sizeof(ctx->filecnt));

	/* Copy header */
	ctx->read(ctx->offset + src, &u.h, sizeof(u.h));
	ctx->write(ctx->offset + dst, &u.h, sizeof(u.h));

	src += HGRAIN;
	dst += HGRAIN;

	/* Copy file info */
	for (i = 0; i < ctx->filecnt; ++i) {
		ctx->read(ctx->offset + src, &u.f, sizeof(u.f));
		ctx->write(ctx->offset + dst, &u.f, sizeof(u.f));

		src += HGRAIN;
		dst += HGRAIN;
	}

}


int meterfs_getFileInfoName(const char *name, fileheader_t *f, meterfs_ctx_t *ctx)
{
	fileheader_t t;
	size_t i, filecnt;

	ctx->read(ctx->offset + ctx->hcurrAddr + offsetof(header_t, filecnt), &filecnt, sizeof(filecnt));

	for (i = 0; i < filecnt; ++i) {
		ctx->read(ctx->offset + ctx->hcurrAddr + HGRAIN + (i * HGRAIN) + offsetof(fileheader_t, name), &t.name, sizeof(t.name));
		if (strncmp(name, t.name, sizeof(t.name)) == 0) {
			if (f != NULL)
				ctx->read(ctx->offset + ctx->hcurrAddr + HGRAIN + (i * HGRAIN), f, sizeof(*f));

			return i;
		}
	}

	return -ENOENT;
}


int meterfs_updateFileInfo(fileheader_t *f, meterfs_ctx_t *ctx)
{
	unsigned int headerNew, i;
	union {
		header_t h;
		fileheader_t t;
	} u;

	if (f == NULL)
		return -1;

	/* Check if file exist */
	if (meterfs_getFileInfoName(f->name, &u.t, ctx) < 0)
		return -EINVAL;

	if (!f->recordsz)
		return -EINVAL;

	/* File can not exceed prealocated sector count */
	if ((f->filesz != u.t.filesz || f->recordsz != u.t.recordsz) && (SECTORS(f, ctx->sectorsz) > u.t.sectorcnt))
		return -ENOMEM;

	f->sector = u.t.sector;
	f->sectorcnt = u.t.sectorcnt;

	/* Clear file content */
	for (i = 0; i < f->sectorcnt; ++i)
		ctx->eraseSector(ctx->offset + (f->sector + i) * ctx->sectorsz);

	headerNew = (ctx->hcurrAddr == ctx->h1Addr) ? 0 : ctx->h1Addr;

	/* Make space for new file table */
	meterfs_eraseFileTable((headerNew == 0) ? 0 : 1, ctx);

	for (i = 0; i < ctx->filecnt; ++i) {
		ctx->read(ctx->offset + ctx->hcurrAddr + HGRAIN + (i * HGRAIN), &u.t, sizeof(fileheader_t));
		if (strncmp(f->name, u.t.name, sizeof(f->name)) == 0)
			ctx->write(ctx->offset + headerNew + HGRAIN + (i * HGRAIN), f, sizeof(fileheader_t));
		else
			ctx->write(ctx->offset + headerNew + HGRAIN + (i * HGRAIN), &u.t, sizeof(fileheader_t));
	}

	/* Prepare new header */
	ctx->read(ctx->offset + ctx->hcurrAddr, &u.h, sizeof(u.h));
	++u.h.id.no;

	ctx->write(ctx->offset + headerNew, &u.h, sizeof(u.h));

	/* Use new header from now on */
	ctx->hcurrAddr = headerNew;

	return EOK;
}


void meterfs_getFilePos(file_t *f, meterfs_ctx_t *ctx)
{
	unsigned int baddr, eaddr, totalrecord, maxrecord;
	int i, offset, interval, diff, idx;
	index_t id;

	f->lastidx.no = 0;
	f->lastidx.nvalid = 1;
	f->lastoff = 0;
	f->recordcnt = 0;

	baddr = f->header.sector * ctx->sectorsz;
	eaddr = baddr + f->header.sectorcnt * ctx->sectorsz;
	totalrecord = (eaddr - baddr) / (f->header.recordsz + sizeof(entry_t));
	maxrecord = f->header.filesz / f->header.recordsz - 1;
	diff = 0;

	/* Find any valid record (starting point) */
	interval = ctx->sectorsz / (f->header.recordsz + sizeof(entry_t));
	interval = (interval + 1) * (f->header.recordsz + sizeof(entry_t));

	for (i = 0, offset = 0; i < f->header.sectorcnt; ++i) {
		ctx->read(ctx->offset + baddr + offset + offsetof(entry_t, id), &id, sizeof(id));
		if (!id.nvalid) {
			f->lastidx = id;
			f->lastoff = offset;
			break;
		}

		offset += interval;

		if (offset % ctx->sectorsz > f->header.recordsz + sizeof(entry_t))
			offset -= f->header.recordsz + sizeof(entry_t);
	}

	f->firstidx = f->lastidx;
	f->firstoff = f->lastoff;

	/* Is file empty? */
	if (f->lastidx.nvalid) {
		return;
	}

	/* Find newest record */
	for (interval = totalrecord - 1; interval != 0; ) {
		idx = ((f->lastoff / (f->header.recordsz + sizeof(entry_t))) + interval) % totalrecord;
		offset = idx * (f->header.recordsz + sizeof(entry_t));
		ctx->read(ctx->offset + baddr + offset + offsetof(entry_t, id), &id, sizeof(id));
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
		ctx->read(ctx->offset + baddr + offset + offsetof(entry_t, id), &id, sizeof(id));
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


int meterfs_writeRecord(file_t *f, const void *buff, size_t bufflen, meterfs_ctx_t *ctx)
{
	/* This function assumes that f contains valid lastidx and lastoff */
	entry_t e;
	unsigned int offset;

	if (bufflen > f->header.recordsz)
		bufflen = f->header.recordsz;

	offset = f->lastoff;

	if (!f->lastidx.nvalid)
		offset += f->header.recordsz + sizeof(entry_t);

	if (offset + f->header.recordsz + sizeof(entry_t) > f->header.sectorcnt * ctx->sectorsz)
		offset = 0;

	/* Check if we have to erase sector to write new data */
	if (offset == 0 || (offset / ctx->sectorsz) != ((offset + f->header.recordsz + sizeof(entry_t)) / ctx->sectorsz))
		ctx->eraseSector(ctx->offset + (f->header.sector * ctx->sectorsz) + (offset + f->header.recordsz + sizeof(entry_t)));

	e.id.no = f->lastidx.no + 1;
	e.id.nvalid = 0;

	ctx->write(ctx->offset + f->header.sector * ctx->sectorsz + offset + sizeof(entry_t), (void *)buff, bufflen);
	ctx->write(ctx->offset + f->header.sector * ctx->sectorsz + offset, &e, sizeof(entry_t));

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
		if (f->firstoff + f->header.recordsz + sizeof(entry_t) > f->header.sectorcnt * ctx->sectorsz)
			f->firstoff = 0;
	}

	return f->header.recordsz;
}


int meterfs_readRecord(file_t *f, void *buff, size_t bufflen, unsigned int idx, size_t offset, meterfs_ctx_t *ctx)
{
	/* This function assumes that f contains valid firstidx and firstoff */
	unsigned int addr, pos;
	unsigned char fbuff[32];
	index_t id;
	entry_t *eptr = (entry_t *)fbuff;

	if (f->firstidx.nvalid || idx > f->recordcnt)
		return -ENOENT;

	/* Calculate record position in a storage */
	pos = (f->firstoff / (f->header.recordsz + sizeof(entry_t))) + idx;
	pos = pos % ((f->header.sectorcnt * ctx->sectorsz) / (f->header.recordsz + sizeof(entry_t)));
	addr = pos * (f->header.recordsz + sizeof(entry_t)) + f->header.sector * ctx->sectorsz;

	if (bufflen > f->header.recordsz - offset)
		bufflen = f->header.recordsz - offset;

	if (f->header.recordsz + sizeof(entry_t) <= sizeof(fbuff)) {
		ctx->read(ctx->offset + addr, fbuff, f->header.recordsz + sizeof(entry_t));

		if (eptr->id.nvalid || eptr->id.no != f->firstidx.no + idx)
			return -ENOENT;

		memcpy(buff, fbuff + sizeof(entry_t) + offset, bufflen);
	}
	else {
		ctx->read(ctx->offset + addr + offsetof(entry_t, id), &id, sizeof(id));

		if (id.nvalid || id.no != f->firstidx.no + idx)
			return -ENOENT;

		/* Read data */
		ctx->read(ctx->offset + addr + sizeof(entry_t) + offset, buff, bufflen);
	}

	return bufflen;
}


/*
 * Meterfs interface functions
 */


int meterfs_open(oid_t *oid, meterfs_ctx_t *ctx)
{
	if (node_getById(oid->id, &ctx->nodesTree) != NULL)
		return EOK;

	return -ENOENT;
}


int meterfs_close(oid_t *oid, meterfs_ctx_t *ctx)
{
	return node_put(oid->id, &ctx->nodesTree);
}


int meterfs_lookup(const char *name, oid_t *res, meterfs_ctx_t *ctx)
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

	if (node_getByName(bname, &res->id, &ctx->nodesTree) != NULL) {
		node_put(res->id, &ctx->nodesTree);

		return i;
	}
	else if ((err = meterfs_getFileInfoName(bname, &f.header, ctx)) < 0) {
		return -ENOENT;
	}

	res->id = err;

	meterfs_getFilePos(&f, ctx);

	if ((err = node_add(&f, res->id, &ctx->nodesTree)) < 0)
		return err;

	return i;
}


int meterfs_allocateFile(const char *name, size_t sectorcnt, size_t filesz, size_t recordsz, meterfs_ctx_t *ctx)
{
	header_t h;
	fileheader_t hdr, t;
	unsigned int addr, i, headerNew;

	if (meterfs_getFileInfoName(name, &hdr, ctx) >= 0)
		return -EEXIST;

	if (recordsz > filesz || !recordsz)
		return -EINVAL;

	strncpy(hdr.name, name, sizeof(hdr.name));
	hdr.filesz = filesz;
	hdr.recordsz = recordsz;
	hdr.sector = 0;
	hdr.sectorcnt = sectorcnt;

	/* Check if sectorcnt is valid */
	if (SECTORS(&hdr, ctx->sectorsz) > hdr.sectorcnt || hdr.sectorcnt < 2)
		return -EINVAL;

	ctx->read(ctx->offset + ctx->hcurrAddr, &h, sizeof(h));

	if (h.filecnt >= MAX_FILE_CNT) {
		return -ENOMEM;
	}

	/* Find free sectors */
	if (h.filecnt != 0) {
		ctx->read(ctx->offset + ctx->hcurrAddr + HGRAIN + (h.filecnt - 1) * HGRAIN, &t, sizeof(t));

		hdr.sector = t.sector + t.sectorcnt;
		addr = hdr.sector * ctx->sectorsz;

		if (addr + (hdr.sectorcnt * ctx->sectorsz) >= ctx->sz) {
			return -ENOMEM;
		}
	}
	else {
		addr = ctx->h1Addr << 1;
		hdr.sector = addr / ctx->sectorsz;
	}

	/* Prepare data space */
	for (i = 0; i < hdr.sectorcnt; ++i)
		ctx->eraseSector(ctx->offset + (hdr.sector + i) * ctx->sectorsz);

	headerNew = (ctx->hcurrAddr == 0) ? ctx->h1Addr : 0;
	meterfs_eraseFileTable((headerNew == 0) ? 0 : 1, ctx);

	/* Copy data from the old header */
	for (i = 0; i < h.filecnt; ++i) {
		ctx->read(ctx->offset + ctx->hcurrAddr + HGRAIN + (i * HGRAIN), &t, sizeof(t));
		ctx->write(ctx->offset + headerNew + HGRAIN + (i * HGRAIN), &t, sizeof(t));
	}

	/* Store new file header */
	ctx->write(ctx->offset + headerNew + HGRAIN + (h.filecnt * HGRAIN), &hdr, sizeof(fileheader_t));

	/* Commit new header and update global info */
	h.filecnt += 1;
	h.id.no += 1;

	ctx->write(ctx->offset + headerNew, &h, sizeof(h));
	ctx->filecnt += 1;
	ctx->hcurrAddr = headerNew;

	return EOK;
}


int meterfs_resizeFile(const char *name, size_t filesz, size_t recordsz, meterfs_ctx_t *ctx)
{
	fileheader_t hdr;

	if (meterfs_getFileInfoName(name, &hdr, ctx) < 0)
		return -ENOENT;

	if (!hdr.sector || !hdr.sectorcnt)
		return -EFAULT;

	if (!recordsz)
		return -EINVAL;

	hdr.filesz = filesz;
	hdr.recordsz = recordsz;

	if (SECTORS(&hdr, ctx->sectorsz) > hdr.sectorcnt)
		return -EINVAL;

	return meterfs_updateFileInfo(&hdr, ctx);
}


int meterfs_readFile(oid_t *oid, offs_t offs, char *buff, size_t bufflen, meterfs_ctx_t *ctx)
{
	file_t *f;
	unsigned int idx;
	size_t chunk, i = 0, pos = offs;

	if ((f = node_getById(oid->id, &ctx->nodesTree)) == NULL)
		return -ENOENT;

	if (!f->header.filesz || !f->header.recordsz)
		return 0;

	idx = pos / f->header.recordsz;
	pos %= f->header.recordsz;

	while (i < bufflen) {
		chunk = (bufflen - i <= f->header.recordsz) ? bufflen - i : f->header.recordsz;
		if (meterfs_readRecord(f, buff + i, chunk, idx, pos, ctx) <= 0)
			break;

		pos = 0;
		i += chunk;
		++idx;
	}

	node_put(oid->id, &ctx->nodesTree);

	return i;
}


int meterfs_writeFile(oid_t *oid, const char *buff, size_t bufflen, meterfs_ctx_t *ctx)
{
	file_t *f;
	int err;

	if ((f = node_getById(oid->id, &ctx->nodesTree)) == NULL)
		return -ENOENT;

	if (!f->header.filesz || !f->header.recordsz)
		return 0;

	err = meterfs_writeRecord(f, buff, bufflen, ctx);

	node_put(oid->id, &ctx->nodesTree);

	return err;
}


int meterfs_devctl(meterfs_i_devctl_t *i, meterfs_o_devctl_t *o, meterfs_ctx_t *ctx)
{
	fileheader_t h;
	file_t *p;
	int err = EOK;

	switch (i->type) {
		case meterfs_allocate:
			if (!i->allocate.filesz || !i->allocate.recordsz)
				return -EINVAL;

			if (i->allocate.filesz < i->allocate.recordsz)
				return -EINVAL;

			h.filesz = i->allocate.filesz;
			h.recordsz = i->allocate.recordsz;

			if (SECTORS(&h, ctx->sectorsz) > i->allocate.sectors)
				return -EINVAL;

			if ((err = meterfs_allocateFile(i->allocate.name, i->allocate.sectors, i->allocate.filesz, i->allocate.recordsz, ctx)) < 0)
				return err;

			break;

		case meterfs_resize:
			if ((p = node_getById(i->resize.oid.id, &ctx->nodesTree)) == NULL)
				return -ENOENT;

			if ((err = meterfs_resizeFile(p->header.name, i->resize.filesz, i->resize.recordsz, ctx)) == EOK) {
				p->header.filesz = i->resize.filesz;
				p->header.recordsz = i->resize.recordsz;
			}

			meterfs_getFileInfoName(p->header.name, &p->header, ctx);
			meterfs_getFilePos(p, ctx);

			node_put(i->resize.oid.id, &ctx->nodesTree);
			break;

		case meterfs_info:
			if ((p = node_getById(i->oid.id, &ctx->nodesTree)) == NULL)
				return -ENOENT;

			o->info.sectors = p->header.sectorcnt;
			o->info.filesz = p->header.filesz;
			o->info.recordsz = p->header.recordsz;
			o->info.recordcnt = p->recordcnt;

			node_put(i->oid.id, &ctx->nodesTree);
			break;

		case meterfs_chiperase:
			ctx->partitionErase();
			node_cleanAll(&ctx->nodesTree);
			meterfs_checkfs(ctx);
			break;

		default:
			err = -EINVAL;
			break;
	}

	return err;
}

int meterfs_init(meterfs_ctx_t *ctx)
{
	if (ctx == NULL)
		return -1;

	if (ctx->sz < MIN_PARTITIONS_SECTORS_NB * ctx->sectorsz)
		return -1;

	node_init(&ctx->nodesTree);

	meterfs_checkfs(ctx);
	LOG_INFO("meterfs: Filesystem check done. Found %u files.", ctx->filecnt);

	return 0;
}