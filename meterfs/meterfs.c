/*
 * Phoenix-RTOS
 *
 * Meterfs Library
 *
 * Copyright 2017, 2018, 2020, 2021, 2023 Phoenix Systems
 * Author: Aleksander Kaminski, Hubert Buczynski, Tomasz Korniluk, Hubert Badocha
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
#include <sys/mount.h>
#include <string.h>
#include <stddef.h>
#include <sys/reboot.h>

#include "meterfs.h"
#include "files.h"
#include "node.h"

/* clang-format off */
#define TOTAL_SIZE(f)        (((f)->filesz / (f)->recordsz) * ((f)->recordsz + sizeof(entry_t)))
#define SECTORS(f, sectorsz) (((TOTAL_SIZE(f) + (sectorsz) - 1) / (sectorsz)) + 1)

#define LOG_INFO(str, ...)  do { if(1) {(void)printf(str "\n", ##__VA_ARGS__);} } while(0)
#define LOG_DEBUG(str, ...) do { if(0) {(void)printf(str "\n", ##__VA_ARGS__);} } while(0)
/* clang-format on */

#define MIN_PARTITIONS_SECTORS_NB ((2 * HEADER_SECTOR_CNT) + 2)
#define JOURNAL_OFFSET(ssz)       (2 * HEADER_SIZE(ssz))
#define SPARE_SECTOR_OFFSET(ssz)  (JOURNAL_OFFSET(ssz) + (ssz))

#define UNRELIABLE_WRITE 0 /* DEBUG ONLY! */
#if UNRELIABLE_WRITE
static struct {
	int rwcnt;
	int wrreboot;
} debug_common;
#define UNRELIABLE_WRITE_TRIGGER        32
#define UNRELIABLE_WRITE_REBOOT         1
#define UNRELIABLE_WRITE_REBOOT_TRIGGER 5
#warning "meterfs UNRELIABLE_WRITE is ON. Did you really intend that?"
#endif

static const unsigned char magicConst[4] = { 0x66, 0x41, 0x4b, 0xbb };

struct partialEraseJournal_s {
	unsigned int sector;
	uint32_t eraseOffset;
	unsigned char magic[4];
} __attribute__((packed));


/*
 * Meterfs internal functions
 */

/* Just a simple and somewhat fast BCC checksum */
static uint32_t meterfs_calcChecksum(const void *buff, size_t bufflen)
{
	uint8_t checksum = 0;
	size_t i;

	for (i = 0; i < bufflen; ++i)
		checksum ^= ((const uint8_t *)buff)[i];

	return checksum;
}

static void meterfs_powerCtrl(int state, meterfs_ctx_t *ctx)
{
	if (ctx->powerCtrl != NULL) {
		(ctx->powerCtrl)(ctx->devCtx, state);
	}
}

static ssize_t meterfs_writeVerify(unsigned int addr, const void *buff, size_t bufflen, meterfs_ctx_t *ctx)
{
	ssize_t err;
	unsigned char vbuff[32];
	size_t i, chunk;

#if UNRELIABLE_WRITE
	if (++debug_common.rwcnt > UNRELIABLE_WRITE_TRIGGER) {
		LOG_DEBUG("Unreliable write trigger - write");
		debug_common.rwcnt = 0;
		return -EIO;
	}
#endif

	err = ctx->write(ctx->devCtx, addr, buff, bufflen);
	if (err < 0)
		return err;

	for (i = 0; i < bufflen; i += chunk) {
		chunk = bufflen - i;
		if (chunk > sizeof(vbuff))
			chunk = sizeof(vbuff);

		err = ctx->read(ctx->devCtx, addr + i, vbuff, chunk);
		if (err < 0)
			return err;

#if UNRELIABLE_WRITE
		if (++debug_common.rwcnt > UNRELIABLE_WRITE_TRIGGER) {
			LOG_DEBUG("Unreliable write trigger - verify");
			debug_common.rwcnt = 0;
			return -EIO;
		}
#endif

		if (memcmp((const unsigned char *)buff + i, vbuff, chunk))
			return -EIO;
	}

	return (ssize_t)bufflen;
}


static ssize_t meterfs_copyData(unsigned int dst, unsigned int src, size_t len, meterfs_ctx_t *ctx)
{
	char buff[64];
	ssize_t err;
	size_t chunk, offs;

	for (offs = 0; offs < len; offs += chunk) {
		chunk = (offs + sizeof(buff) >= len) ? len - offs : sizeof(buff);

#if UNRELIABLE_WRITE
		/* Let it copy data... We need to do partial erase */
		debug_common.rwcnt = 0;
#endif

		err = ctx->read(ctx->devCtx, src + offs, buff, chunk);
		if (err < 0)
			return err;

		err = meterfs_writeVerify(dst + offs, buff, chunk, ctx);
		if (err < 0)
			return err;
	}

	return (ssize_t)len;
}


static int meterfs_doPartialErase(meterfs_ctx_t *ctx)
{
	int err;
	struct partialEraseJournal_s journal;

	do {
		err = ctx->read(ctx->devCtx, ctx->offset + JOURNAL_OFFSET(ctx->sectorsz), &journal, sizeof(journal));
		if (err < 0)
			break;

		if (memcmp(journal.magic, magicConst, sizeof(journal.magic)) != 0) {
			err = 1;
			break; /* Nothing to do */
		}

		LOG_DEBUG("doPartialErase");

		/* Erase original sector */
		err = ctx->eraseSector(ctx->devCtx, ctx->offset + journal.sector * ctx->sectorsz);
		if (err < 0)
			break;

		/* Copy backup up to the erase offset */
		err = meterfs_copyData(ctx->offset + journal.sector * ctx->sectorsz,
			ctx->offset + SPARE_SECTOR_OFFSET(ctx->sectorsz),
			journal.eraseOffset, ctx);

		if (err < 0)
			break;

		err = ctx->eraseSector(ctx->devCtx, ctx->offset + JOURNAL_OFFSET(ctx->sectorsz));
	} while (0);

	if (err >= 0)
		LOG_DEBUG("doPartialErase OK");

	/* Leave spare sector not erased - it will be erased during next use anyway */
	return err;
}


static int meterfs_startPartialErase(uint32_t addr, meterfs_ctx_t *ctx)
{
	int err;
	struct partialEraseJournal_s journal;

	LOG_DEBUG("startPartialErase");

	journal.sector = (addr - ctx->offset) / ctx->sectorsz;
	journal.eraseOffset = addr % ctx->sectorsz;

	do {
		/* Check if there's already pending recovery  - if so, just do it.
		 * Ignore errors:
		 * 1) There shouldn't be pending operation at this point - this is
		 *    just in case,
		 * 2) If this failed there's not much we can do. We need to perform
		 *    partial erase *right now*, or we'll "forget" that we need
		 *    to do it.
		 */
		(void)meterfs_doPartialErase(ctx);

		/* Should be erased, just in case */
		err = ctx->eraseSector(ctx->devCtx, ctx->offset + JOURNAL_OFFSET(ctx->sectorsz));
		if (err < 0) {
			break;
		}

		err = ctx->eraseSector(ctx->devCtx, ctx->offset + SPARE_SECTOR_OFFSET(ctx->sectorsz));
		if (err < 0) {
			break;
		}

		/* Backup original sector */
		err = meterfs_copyData(ctx->offset + SPARE_SECTOR_OFFSET(ctx->sectorsz),
			ctx->offset + journal.sector * ctx->sectorsz,
			journal.eraseOffset, ctx);
		if (err < 0) {
			break;
		}

		/* Prepare and store journal */
		(void)memcpy(journal.magic, magicConst, sizeof(journal.magic));

		err = meterfs_writeVerify(ctx->offset + JOURNAL_OFFSET(ctx->sectorsz), &journal, sizeof(journal), ctx);
		if (err < 0) {
			break;
		}

#if UNRELIABLE_WRITE
		if (++debug_common.wrreboot > UNRELIABLE_WRITE_REBOOT) {
			LOG_DEBUG("Unreliable write trigger - reboot during partial erase");
			reboot(PHOENIX_REBOOT_MAGIC);
		}
#endif

		/* Now finish the operation */
		err = meterfs_doPartialErase(ctx);
	} while (0);

	return err;
}


/* Warning - this function is only for writing records as it may erase whole sector above addr! */
static ssize_t meterfs_safeWrite(unsigned int addr, void *buff, size_t bufflen, meterfs_ctx_t *ctx)
{
	ssize_t err;

	err = meterfs_writeVerify(addr, buff, bufflen, ctx);

	if (err < 0) {
		/* Write-verify failed - perhaps some bits were already programmed.
		 * Do partial erase and try again */
		err = meterfs_startPartialErase(addr, ctx);
		if (err < 0) {
			return err;
		}

		err = meterfs_writeVerify(addr, buff, bufflen, ctx);

		if (err < 0) {
			/* Ok, so we failed again. Let's just try to clean this mess up */
			(void)meterfs_startPartialErase(addr, ctx);
		}
	}

	return err;
}


static int meterfs_eraseFileTable(unsigned int n, meterfs_ctx_t *ctx)
{
	uint32_t addr;
	size_t i;
	int err;

	if (n != 0 && n != 1)
		return -EINVAL;

	addr = (n == 0) ? 0 : ctx->h1Addr;

	for (i = 0; i < HEADER_SECTOR_CNT; ++i) {
		err = ctx->eraseSector(ctx->devCtx, ctx->offset + addr + i * ctx->sectorsz);
		if (err < 0)
			return err;
	}

	return 0;
}


static int meterfs_checkfs(meterfs_ctx_t *ctx)
{
	unsigned int valid0 = 0, valid1 = 0, src, dst, retry = 0, i;
	index_t id = { 0 };
	ssize_t err;
	uint32_t checksum, filecnt;
	union {
		header_t h;
		fileheader_t f;
	} u;

	meterfs_powerCtrl(1, ctx);

	do {
		/* Check if first header is valid */
		err = ctx->read(ctx->devCtx, ctx->offset, &u.h, sizeof(u.h));
		if (err < 0) {
			meterfs_powerCtrl(0, ctx);
			return err;
		}

		if (!(u.h.id.nvalid || memcmp(u.h.magic, magicConst, 4))) {
			id = u.h.id;

			checksum = meterfs_calcChecksum(&u.h, sizeof(u.h));
			filecnt = u.h.filecnt;
			for (i = 0; i < filecnt; ++i) {
				/* We destroy the header (as it's a part of a union)!. But it's ok, we only
				 * need to check if checksum == 0 */
				err = ctx->read(ctx->devCtx, ctx->offset + HGRAIN + (i * HGRAIN), &u.f, sizeof(u.f));
				if (err < 0) {
					meterfs_powerCtrl(0, ctx);
					return err;
				}

				checksum ^= meterfs_calcChecksum(&u.f, sizeof(u.f));
			}

			valid0 = (checksum == 0) ? 1 : 0;
		}

		/* Check next header */
		ctx->h1Addr = HEADER_SIZE(ctx->sectorsz);

		err = ctx->read(ctx->devCtx, ctx->offset + ctx->h1Addr, &u.h, sizeof(u.h));
		if (err < 0) {
			meterfs_powerCtrl(0, ctx);
			return err;
		}

		if (!(u.h.id.nvalid || memcmp(u.h.magic, magicConst, 4))) {
			/* Copy paste from check of header #0 - by design.
			 * This way I can reuse union u and conserve stack
			 * space. I would need new union if this was
			 * a function. Sorry. */
			checksum = meterfs_calcChecksum(&u.h, sizeof(u.h));
			filecnt = u.h.filecnt;
			for (i = 0; i < filecnt; ++i) {
				/* We destroy the header (as it's a part of a union)!. But it's ok, we only
				 * need to check if checksum == 0 */
				err = ctx->read(ctx->devCtx, ctx->offset + ctx->h1Addr + HGRAIN + (i * HGRAIN), &u.f, sizeof(u.f));
				if (err < 0) {
					meterfs_powerCtrl(0, ctx);
					return err;
				}

				checksum ^= meterfs_calcChecksum(&u.f, sizeof(u.f));
			}

			valid1 = (checksum == 0) ? 1 : 0;
		}
	} while (valid0 == 0 && valid1 == 0 && retry++ < 3);

	if (!valid0 && !valid1) {
		LOG_INFO("meterfs: No valid filesystem detected. Formating.");
		err = meterfs_eraseFileTable(0, ctx);
		if (err < 0) {
			meterfs_powerCtrl(0, ctx);
			return err;
		}
		err = meterfs_eraseFileTable(1, ctx);
		if (err < 0) {
			meterfs_powerCtrl(0, ctx);
			return err;
		}
		/* Not essential, ignore errors */
		(void)ctx->eraseSector(ctx->devCtx, ctx->offset + JOURNAL_OFFSET(ctx->sectorsz));

		ctx->filecnt = 0;
		ctx->hcurrAddr = 0;

		u.h.filecnt = 0;
		u.h.id.no = 0;
		u.h.id.nvalid = 0;
		u.h.checksum = 0;
		memcpy(u.h.magic, magicConst, 4);

		checksum = meterfs_calcChecksum(&u.h, sizeof(u.h));
		u.h.checksum = checksum;

		err = meterfs_writeVerify(ctx->offset + 0, &u.h, sizeof(u.h), ctx);
		if (err < 0) {
			meterfs_powerCtrl(0, ctx);
			return err;
		}
		err = meterfs_writeVerify(ctx->offset + ctx->h1Addr, &u.h, sizeof(u.h), ctx);
		if (err < 0) {
			meterfs_powerCtrl(0, ctx);
			return err;
		}

		meterfs_powerCtrl(0, ctx);
		return 0;
	}

	/* Select active header and files table */
	if (valid0 && valid1) {
		err = ctx->read(ctx->devCtx, ctx->offset + ctx->h1Addr, &u.h, sizeof(u.h));
		if (err < 0) {
			meterfs_powerCtrl(0, ctx);
			return err;
		}

		if (id.no + 1 == u.h.id.no)
			ctx->hcurrAddr = ctx->h1Addr;
		else
			ctx->hcurrAddr = 0;

		err = ctx->read(ctx->devCtx, ctx->offset + ctx->hcurrAddr + offsetof(header_t, filecnt), &ctx->filecnt, sizeof(ctx->filecnt));
		if (err < 0) {
			meterfs_powerCtrl(0, ctx);
			return err;
		}
	}
	else {
		/* There should be copy of file table at all times. Fix it if necessary */
		if (!valid0) {
			LOG_INFO("meterfs: Filetable header #0 is damaged - repairing.");
			src = ctx->h1Addr;
			dst = 0;
			err = meterfs_eraseFileTable(0, ctx);
			if (err < 0) {
				meterfs_powerCtrl(0, ctx);
				return err;
			}
			ctx->hcurrAddr = ctx->h1Addr;
		}
		else {
			LOG_INFO("meterfs: Filetable header #1 is damaged - repairing.");
			src = 0;
			dst = ctx->h1Addr;
			err = meterfs_eraseFileTable(1, ctx);
			if (err < 0) {
				meterfs_powerCtrl(0, ctx);
				return err;
			}
			ctx->hcurrAddr = 0;
		}

		err = ctx->read(ctx->devCtx, ctx->offset + ctx->hcurrAddr + offsetof(header_t, filecnt), &ctx->filecnt, sizeof(ctx->filecnt));
		if (err < 0) {
			meterfs_powerCtrl(0, ctx);
			return err;
		}

		err = meterfs_copyData(ctx->offset + dst, ctx->offset + src, HGRAIN * (ctx->filecnt + 1), ctx);
		if (err < 0) {
			meterfs_powerCtrl(0, ctx);
			return err;
		}
	}

	/* Finish pending partial erase operation (if any) */
	err = meterfs_doPartialErase(ctx);

	meterfs_powerCtrl(0, ctx);
	return err;
}


static int meterfs_getFileInfoName(const char *name, fileheader_t *f, meterfs_ctx_t *ctx)
{
	fileheader_t t;
	uint32_t i, filecnt;
	ssize_t err = 0;

	meterfs_powerCtrl(1, ctx);

	err = ctx->read(ctx->devCtx, ctx->offset + ctx->hcurrAddr + offsetof(header_t, filecnt), &filecnt, sizeof(filecnt));
	if (err < 0) {
		meterfs_powerCtrl(0, ctx);
		return err;
	}

	for (i = 0; i < filecnt; ++i) {
		err = ctx->read(ctx->devCtx, ctx->offset + ctx->hcurrAddr + HGRAIN + (i * HGRAIN) + offsetof(fileheader_t, name), &t.name, sizeof(t.name));
		if (err < 0) {
			meterfs_powerCtrl(0, ctx);
			return err;
		}
		if (strncmp(name, t.name, sizeof(t.name)) == 0) {
			if (f != NULL) {
				err = ctx->read(ctx->devCtx, ctx->offset + ctx->hcurrAddr + HGRAIN + (i * HGRAIN), f, sizeof(*f));
				if (err < 0) {
					meterfs_powerCtrl(0, ctx);
					return err;
				}
			}
			meterfs_powerCtrl(0, ctx);

			return i;
		}
	}

	meterfs_powerCtrl(0, ctx);
	return -ENOENT;
}


static int meterfs_updateFileInfo(fileheader_t *f, meterfs_ctx_t *ctx)
{
	unsigned int headerNew, i;
	ssize_t err = 0;
	union {
		header_t h;
		fileheader_t t;
	} u;
	uint32_t checksum = 0;

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

	meterfs_powerCtrl(1, ctx);

	/* Clear file content */
	for (i = 0; i < f->sectorcnt; ++i) {
		err = ctx->eraseSector(ctx->devCtx, ctx->offset + (f->sector + i) * ctx->sectorsz);
		if (err < 0) {
			meterfs_powerCtrl(0, ctx);
			return err;
		}
	}

	headerNew = (ctx->hcurrAddr == ctx->h1Addr) ? 0 : ctx->h1Addr;

	/* Make space for new file table */
	err = meterfs_eraseFileTable((headerNew == 0) ? 0 : 1, ctx);
	if (err < 0) {
		meterfs_powerCtrl(0, ctx);
		return err;
	}

	/* Can't use meterfs_copyData - we need to modify one file in flight */
	for (i = 0; i < ctx->filecnt; ++i) {
		err = ctx->read(ctx->devCtx, ctx->offset + ctx->hcurrAddr + HGRAIN + (i * HGRAIN), &u.t, sizeof(u.t));
		if (err < 0) {
			meterfs_powerCtrl(0, ctx);
			return err;
		}
		if (strncmp(f->name, u.t.name, sizeof(f->name)) == 0) {
			err = meterfs_writeVerify(ctx->offset + headerNew + HGRAIN + (i * HGRAIN), f, sizeof(*f), ctx);
			if (err < 0) {
				meterfs_powerCtrl(0, ctx);
				return err;
			}

			checksum ^= meterfs_calcChecksum(f, sizeof(*f));
		}
		else {
			err = meterfs_writeVerify(ctx->offset + headerNew + HGRAIN + (i * HGRAIN), &u.t, sizeof(u.t), ctx);
			if (err < 0) {
				meterfs_powerCtrl(0, ctx);
				return err;
			}

			checksum ^= meterfs_calcChecksum(&u.t, sizeof(u.t));
		}
	}

	/* Prepare new header */
	err = ctx->read(ctx->devCtx, ctx->offset + ctx->hcurrAddr, &u.h, sizeof(u.h));
	if (err < 0) {
		meterfs_powerCtrl(0, ctx);
		return err;
	}
	++u.h.id.no;
	u.h.checksum = 0; /* 0 is a neutral value for BCC checksum */
	u.h.checksum = checksum ^ meterfs_calcChecksum(&u.h, sizeof(u.h));

	err = meterfs_writeVerify(ctx->offset + headerNew, &u.h, sizeof(u.h), ctx);
	if (err < 0) {
		meterfs_powerCtrl(0, ctx);
		return err;
	}

	meterfs_powerCtrl(0, ctx);

	/* Use new header from now on */
	ctx->hcurrAddr = headerNew;

	return 0;
}


static int meterfs_getFilePos(file_t *f, meterfs_ctx_t *ctx)
{
	unsigned int baddr, eaddr, totalrecord, maxrecord;
	int i, offset, interval, diff, idx;
	index_t id;
	ssize_t err;

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
		err = ctx->read(ctx->devCtx, ctx->offset + baddr + offset + offsetof(entry_t, id), &id, sizeof(id));
		if (err < 0)
			return err;
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
	if (f->lastidx.nvalid)
		return 0;

	/* Find newest record */
	for (interval = totalrecord - 1; interval != 0;) {
		idx = ((f->lastoff / (f->header.recordsz + sizeof(entry_t))) + interval) % totalrecord;
		offset = idx * (f->header.recordsz + sizeof(entry_t));
		err = ctx->read(ctx->devCtx, ctx->offset + baddr + offset + offsetof(entry_t, id), &id, sizeof(id));
		if (err < 0)
			return err;
		if (!id.nvalid && ((f->lastidx.no + interval) % (1U << 31)) == id.no) {
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
	for (interval = diff; interval != 0 && diff != 0;) {
		idx = (int)(f->firstoff / (f->header.recordsz + sizeof(entry_t))) + interval;
		if (idx < 0)
			idx += totalrecord;
		else
			idx %= totalrecord;
		offset = idx * (f->header.recordsz + sizeof(entry_t));
		err = ctx->read(ctx->devCtx, ctx->offset + baddr + offset + offsetof(entry_t, id), &id, sizeof(id));
		if (err < 0)
			return err;
		if (!id.nvalid && ((f->firstidx.no + interval) % (1U << 31)) == id.no) {
			f->firstidx = id;
			f->firstoff = offset;
			diff -= interval;
			if (interval == 1 || interval == -1)
				continue;
		}

		interval /= 2;
	}

	f->recordcnt = f->lastidx.no - f->firstidx.no + 1;

	return 0;
}


static int meterfs_rbTreeFill(meterfs_ctx_t *ctx)
{
	file_t f;
	int err, ret = 0;
	unsigned int i;

	meterfs_powerCtrl(1, ctx);

	for (i = 0; i < ctx->filecnt; ++i) {
		err = ctx->read(ctx->devCtx, ctx->offset + ctx->hcurrAddr + HGRAIN + (i * HGRAIN), &f.header, sizeof(f.header));
		if (err < 0) {
			ret = err;
			break;
		}

		err = meterfs_getFilePos(&f, ctx);
		if (err < 0) {
			ret = err;
			break;
		}

		err = node_add(&f, (id_t)i, &ctx->nodesTree);
		if (err < 0) {
			ret = err;
			break;
		}
	}

	meterfs_powerCtrl(0, ctx);

	return ret;
}


static int meterfs_writeRecord(file_t *f, const void *buff, size_t bufflen, meterfs_ctx_t *ctx)
{
	/* This function assumes that f contains valid lastidx and lastoff */
	entry_t e;
	unsigned int offset;
	ssize_t wrote = 0, stat = 0;

	if (bufflen > f->header.recordsz)
		bufflen = f->header.recordsz;

	offset = f->lastoff;

	if (!f->lastidx.nvalid)
		offset += f->header.recordsz + sizeof(entry_t);

	if (offset + f->header.recordsz + sizeof(entry_t) > f->header.sectorcnt * ctx->sectorsz)
		offset = 0;

	meterfs_powerCtrl(1, ctx);

	/* Check if we have to erase sector to write new data */
	if (offset == 0 || (offset / ctx->sectorsz) != ((offset + f->header.recordsz + sizeof(entry_t)) / ctx->sectorsz)) {
		stat = ctx->eraseSector(ctx->devCtx, ctx->offset + (f->header.sector * ctx->sectorsz) + (offset + f->header.recordsz + sizeof(entry_t)));
		if (stat < 0) {
			meterfs_powerCtrl(0, ctx);
			return stat;
		}
	}

	e.id.no = f->lastidx.no + 1;
	e.id.nvalid = 0;
	e.checksum = meterfs_calcChecksum(buff, bufflen);

	/* Not written part of the record are all 0xff. 0xff ^ 0xff = 0, so only odd number of non-programmed bytes matter */
	if ((f->header.recordsz - bufflen) & 1)
		e.checksum ^= 0xff;

	wrote = meterfs_safeWrite(ctx->offset + f->header.sector * ctx->sectorsz + offset + sizeof(entry_t), (void *)buff, bufflen, ctx);
	if (wrote < 0) {
		meterfs_powerCtrl(0, ctx);
		return wrote;
	}

	e.id.nvalid = 1;
	stat = meterfs_safeWrite(ctx->offset + f->header.sector * ctx->sectorsz + offset, &e, sizeof(entry_t), ctx);
	if (stat < 0) {
		/* Let's try cleaning record we failed to write */
		(void)meterfs_startPartialErase(ctx->offset + f->header.sector * ctx->sectorsz + offset, ctx);
		meterfs_powerCtrl(0, ctx);
		return stat;
	}

	/* Setting nvalid flag is the last, atomic as it can be, operation */
	e.id.nvalid = 0;
	stat = meterfs_safeWrite(ctx->offset + f->header.sector * ctx->sectorsz + offset + offsetof(entry_t, id), &e.id, sizeof(e.id), ctx);
	if (stat < 0) {
		/* Let's try cleaning record we failed to write */
		(void)meterfs_startPartialErase(ctx->offset + f->header.sector * ctx->sectorsz + offset, ctx);
		meterfs_powerCtrl(0, ctx);
		return stat;
	}

	meterfs_powerCtrl(0, ctx);

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
	if ((size_t)wrote == bufflen && (size_t)wrote < f->header.recordsz)
		wrote += f->header.recordsz - bufflen;

	return wrote;
}


static int meterfs_readRecord(file_t *f, void *buff, size_t bufflen, unsigned int idx, size_t offset, meterfs_ctx_t *ctx)
{
	/* This function assumes that f contains valid firstidx and firstoff */
	unsigned int addr, pos, retry = 0;
	entry_t *eptr;
	ssize_t stat, err = -ENOENT;
	unsigned char *tmpptr;

	/* Static buffer, realloced to fit the biggest record */
	static unsigned char *rbuff = NULL;
	static size_t rbuffsz = 0;

	if (f->firstidx.nvalid || idx > f->recordcnt) {
		return -ENOENT;
	}

	/* Calculate record position in a storage */
	pos = (f->firstoff / (f->header.recordsz + sizeof(entry_t))) + idx;
	pos = pos % ((f->header.sectorcnt * ctx->sectorsz) / (f->header.recordsz + sizeof(entry_t)));
	addr = pos * (f->header.recordsz + sizeof(entry_t)) + f->header.sector * ctx->sectorsz;

	if (bufflen > f->header.recordsz - offset) {
		bufflen = f->header.recordsz - offset;
	}

	if (f->header.recordsz + sizeof(entry_t) > rbuffsz) {
		LOG_DEBUG("rbuff realloc, oldsz %zu, newsz %zu\n", rbuffsz, f->header.recordsz + sizeof(entry_t));
		tmpptr = realloc(rbuff, f->header.recordsz + sizeof(entry_t));
		if (tmpptr == NULL) {
			LOG_DEBUG("rbuff realloc fail\n");
			return -ENOMEM;
		}
		rbuff = tmpptr;
		rbuffsz = f->header.recordsz + sizeof(entry_t);
	}

	while (err != 0 && retry++ < 3) {
		if (retry != 0) {
			LOG_DEBUG("Read retry #%u\n", retry);
		}

		stat = ctx->read(ctx->devCtx, ctx->offset + addr, rbuff, f->header.recordsz + sizeof(entry_t));
		if (stat < 0) {
			LOG_DEBUG("Read fail\n");
			err = stat;
			continue; /* Retry */
		}

		eptr = (entry_t *)rbuff;

		if (eptr->id.nvalid || eptr->id.no != f->firstidx.no + idx) {
			LOG_DEBUG("ENOENT\n");
			err = -ENOENT;
			break;
		}

		if (eptr->checksum != meterfs_calcChecksum(rbuff + sizeof(entry_t), f->header.recordsz)) {
			LOG_DEBUG("Read checksum fail\n");
			err = -EIO;
			continue; /* Retry */
		}

		memcpy(buff, rbuff + sizeof(entry_t) + offset, bufflen);

		err = 0;
	}

	return (err == 0) ? bufflen : err;
}


/*
 * Meterfs interface functions
 */


int meterfs_open(id_t id, meterfs_ctx_t *ctx)
{
	if (node_getById(id, &ctx->nodesTree) != NULL)
		return 0;

	return -ENOENT;
}


int meterfs_close(id_t id, meterfs_ctx_t *ctx)
{
	if (node_getById(id, &ctx->nodesTree) != NULL)
		return 0;

	return -ENOENT;
}


int meterfs_lookup(const char *name, id_t *res, meterfs_ctx_t *ctx)
{
	file_t f;
	char bname[sizeof(f.header.name)];
	int i = 0;
	size_t j, len = strnlen(&name[1], sizeof(f.header.name) + 1);

	if (len < 1 || len > sizeof(f.header.name))
		return -EINVAL;

	if (name[0] == '/')
		++i;

	for (j = 0; j < sizeof(bname); ++j, ++i) {
		bname[j] = name[i];

		if (name[i] == '/')
			return -ENOENT;

		if (name[i] == '\0')
			break;
	}

	if (node_getByName(bname, res, &ctx->nodesTree) == NULL)
		return -ENOENT;

	return i;
}


int meterfs_allocateFile(const char *name, size_t sectorcnt, size_t filesz, size_t recordsz, meterfs_ctx_t *ctx)
{
	file_t f;
	header_t h;
	fileheader_t t;
	unsigned int addr, i, headerNew;
	size_t len = strnlen(name, sizeof(f.header.name) + 1);
	int err = 0;
	uint32_t checksum = 0;

	if (len < 1 || len > sizeof(f.header.name))
		return -EINVAL;

	if (meterfs_getFileInfoName(name, &f.header, ctx) >= 0)
		return -EEXIST;

	if (recordsz > filesz || !recordsz)
		return -EINVAL;

	memset(f.header.name, 0, sizeof(f.header.name));
	memcpy(f.header.name, name, len);
	f.header.filesz = filesz;
	f.header.recordsz = recordsz;
	f.header.sector = 0;
	f.header.sectorcnt = sectorcnt;

	/* Check if sectorcnt is valid */
	if (SECTORS(&f.header, ctx->sectorsz) > f.header.sectorcnt || f.header.sectorcnt < 2)
		return -EINVAL;

	meterfs_powerCtrl(1, ctx);

	err = ctx->read(ctx->devCtx, ctx->offset + ctx->hcurrAddr, &h, sizeof(h));
	if (err < 0) {
		meterfs_powerCtrl(0, ctx);
		return err;
	}

	if (h.filecnt >= MAX_FILE_CNT(ctx->sectorsz)) {
		meterfs_powerCtrl(0, ctx);
		return -ENOMEM;
	}

	/* Find free sectors */
	if (h.filecnt != 0) {
		err = ctx->read(ctx->devCtx, ctx->offset + ctx->hcurrAddr + HGRAIN + (h.filecnt - 1) * HGRAIN, &t, sizeof(t));
		if (err < 0) {
			meterfs_powerCtrl(0, ctx);
			return err;
		}

		f.header.sector = t.sector + t.sectorcnt;
		addr = f.header.sector * ctx->sectorsz;

		if (addr + (f.header.sectorcnt * ctx->sectorsz) >= ctx->sz) {
			meterfs_powerCtrl(0, ctx);
			return -ENOMEM;
		}
	}
	else {
		if (sectorcnt * ctx->sectorsz >= (ctx->sz - ctx->sectorsz * 4)) {
			meterfs_powerCtrl(0, ctx);
			return -ENOMEM;
		}
		addr = ctx->h1Addr * 2;
		f.header.sector = addr / ctx->sectorsz;
		/* Reserve two sectors for journal and backup
		 * Needed for partial erase operations */
		f.header.sector += 2;
	}

	/* Prepare data space */
	for (i = 0; i < f.header.sectorcnt; ++i) {
		err = ctx->eraseSector(ctx->devCtx, ctx->offset + (f.header.sector + i) * ctx->sectorsz);
		if (err < 0) {
			meterfs_powerCtrl(0, ctx);
			return err;
		}
	}

	headerNew = (ctx->hcurrAddr == 0) ? ctx->h1Addr : 0;
	err = meterfs_eraseFileTable((headerNew == 0) ? 0 : 1, ctx);
	if (err < 0) {
		meterfs_powerCtrl(0, ctx);
		return err;
	}

	/* Copy data from the old header */
	for (i = 0; i < h.filecnt; ++i) {
		err = ctx->read(ctx->devCtx, ctx->offset + ctx->hcurrAddr + HGRAIN + (i * HGRAIN), &t, sizeof(t));
		if (err < 0) {
			meterfs_powerCtrl(0, ctx);
			return err;
		}
		err = meterfs_writeVerify(ctx->offset + headerNew + HGRAIN + (i * HGRAIN), &t, sizeof(t), ctx);
		if (err < 0) {
			meterfs_powerCtrl(0, ctx);
			return err;
		}

		checksum ^= meterfs_calcChecksum(&t, sizeof(t));
	}

	/* Store new file header */
	err = meterfs_writeVerify(ctx->offset + headerNew + HGRAIN + (h.filecnt * HGRAIN), &f.header, sizeof(f.header), ctx);
	if (err < 0) {
		meterfs_powerCtrl(0, ctx);
		return err;
	}

	checksum ^= meterfs_calcChecksum(&f.header, sizeof(f.header));

	/* Commit new header and update global info */
	h.filecnt += 1;
	h.id.no += 1;
	h.checksum = 0;
	h.checksum = checksum ^ meterfs_calcChecksum(&h, sizeof(h));

	err = meterfs_writeVerify(ctx->offset + headerNew, &h, sizeof(h), ctx);
	if (err < 0) {
		meterfs_powerCtrl(0, ctx);
		return err;
	}

	/* Add new node to data structure. */
	err = meterfs_getFilePos(&f, ctx);
	if (err < 0) {
		meterfs_powerCtrl(0, ctx);
		return err;
	}

	meterfs_powerCtrl(0, ctx);

	err = node_add(&f, (id_t)ctx->filecnt, &ctx->nodesTree);
	if (err < 0)
		return err;

	ctx->filecnt += 1;
	ctx->hcurrAddr = headerNew;

	return 0;
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


int meterfs_readFile(id_t id, off_t off, char *buff, size_t bufflen, meterfs_ctx_t *ctx)
{
	file_t *f;
	unsigned int idx;
	size_t chunk, i = 0, pos = off;

	if (bufflen == 0 || off < 0)
		return -EINVAL;

	f = node_getById(id, &ctx->nodesTree);
	if (f == NULL)
		return -ENOENT;

	if (f->recordcnt == 0)
		return 0;

	if (!f->header.filesz || !f->header.recordsz)
		return 0;

	idx = pos / f->header.recordsz;
	pos %= f->header.recordsz;

	meterfs_powerCtrl(1, ctx);

	while (i < bufflen) {
		chunk = (bufflen - i <= f->header.recordsz) ? bufflen - i : f->header.recordsz;
		if (meterfs_readRecord(f, buff + i, chunk, idx, pos, ctx) <= 0)
			break;

		pos = 0;
		i += chunk;
		++idx;
	}

	meterfs_powerCtrl(0, ctx);

	return i;
}


int meterfs_writeFile(id_t id, const char *buff, size_t bufflen, meterfs_ctx_t *ctx)
{
	file_t *f;
	int err;

	if (bufflen == 0)
		return -EINVAL;

	f = node_getById(id, &ctx->nodesTree);
	if (f == NULL)
		return -ENOENT;

	if (!f->header.filesz || !f->header.recordsz)
		return 0;

	err = meterfs_writeRecord(f, buff, bufflen, ctx);

	return err;
}


static int meterfs_doDevctl(const meterfs_i_devctl_t *i, meterfs_o_devctl_t *o, meterfs_ctx_t *ctx)
{
	fileheader_t h;
	file_t *p;
	int err = 0;

	switch (i->type) {
		case meterfs_allocate:
			if (!i->allocate.filesz || !i->allocate.recordsz) {
				return -EINVAL;
			}

			if (i->allocate.filesz < i->allocate.recordsz) {
				return -EINVAL;
			}

			h.filesz = i->allocate.filesz;
			h.recordsz = i->allocate.recordsz;

			if (SECTORS(&h, ctx->sectorsz) > i->allocate.sectors) {
				return -EINVAL;
			}

			err = meterfs_allocateFile(i->allocate.name, i->allocate.sectors, i->allocate.filesz, i->allocate.recordsz, ctx);
			if (err < 0) {
				return err;
			}

			break;

		case meterfs_resize:
			p = node_getById(i->resize.id, &ctx->nodesTree);
			if (p == NULL) {
				return -ENOENT;
			}

			err = meterfs_resizeFile(p->header.name, i->resize.filesz, i->resize.recordsz, ctx);
			if (err < 0) {
				return err;
			}

			p->header.filesz = i->resize.filesz;
			p->header.recordsz = i->resize.recordsz;

			err = meterfs_getFileInfoName(p->header.name, &p->header, ctx);
			if (err < 0) {
				return err;
			}

			meterfs_powerCtrl(1, ctx);
			err = meterfs_getFilePos(p, ctx);
			if (err < 0) {
				meterfs_powerCtrl(0, ctx);
				return err;
			}
			meterfs_powerCtrl(0, ctx);

			break;

		case meterfs_info:
			p = node_getById(i->id, &ctx->nodesTree);
			if (p == NULL) {
				return -ENOENT;
			}

			o->info.sectors = p->header.sectorcnt;
			o->info.filesz = p->header.filesz;
			o->info.recordsz = p->header.recordsz;
			o->info.recordcnt = p->recordcnt;

			break;

		case meterfs_fsInfo:
			o->fsInfo.sectorsz = ctx->sectorsz;
			o->fsInfo.sz = ctx->sz;
			o->fsInfo.filecnt = ctx->filecnt;
			o->fsInfo.fileLimit = MAX_FILE_CNT(ctx->sectorsz);

			break;

		case meterfs_chiperase:
			meterfs_powerCtrl(1, ctx);
			err = meterfs_eraseFileTable(0, ctx);
			if (err < 0) {
				meterfs_powerCtrl(0, ctx);
				return err;
			}
			err = meterfs_eraseFileTable(1, ctx);
			if (err < 0) {
				meterfs_powerCtrl(0, ctx);
				return err;
			}
			meterfs_powerCtrl(0, ctx);
			node_cleanAll(&ctx->nodesTree);
			err = meterfs_checkfs(ctx);
			if (err < 0) {
				return err;
			}
			break;

		default:
			err = -EINVAL;
			break;
	}

	return err;
}


int meterfs_devctl(const meterfs_i_devctl_t *i, meterfs_o_devctl_t *o, meterfs_ctx_t *ctx)
{
	/*
	 *  	Due to problems with current version of message passing we have to duplicate the error.
	 *
	 *		TODO Remove err from meterfs_o_devctl_t.
	 */
	o->err = meterfs_doDevctl(i, o, ctx);
	return o->err;
}


int meterfs_init(meterfs_ctx_t *ctx)
{
	int err;

#if UNRELIABLE_WRITE
	LOG_INFO("meterfs UNRELIABLE_WRITE is ON. Did you really intend that?");
#endif
	/* clang-format off */
	if ((ctx == NULL) ||
			(ctx->sz < (MIN_PARTITIONS_SECTORS_NB * ctx->sectorsz)) ||
			(ctx->read == NULL) ||
			(ctx->write == NULL) ||
			(ctx->eraseSector == NULL)) {
		return -EINVAL;
	}
	/* clang-format on */

	node_init(&ctx->nodesTree);

	err = meterfs_checkfs(ctx);
	if (err < 0) {
		return err;
	}

	err = meterfs_rbTreeFill(ctx);
	if (err < 0) {
		return err;
	}

	LOG_INFO("meterfs: Filesystem check done. Found %u files.", ctx->filecnt);

	return 0;
}
