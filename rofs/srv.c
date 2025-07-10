/*
 * Phoenix-RTOS
 *
 * ROFS - Read Only File System in AHB address space
 *
 * Copyright 2024 Phoenix Systems
 * Author: Gerard Swiderski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <unistd.h>
#include <string.h>

#include "rofs.h"

#define LOG_PREFIX    "rofs: "
#define LOG(fmt, ...) printf(LOG_PREFIX fmt "\n", ##__VA_ARGS__)


static int rofs_ahbRead(struct rofs_ctx *ctx, void *buf, size_t len, size_t offset)
{
	void *ptr = rofs_getImgPtr(ctx);
	if (ptr == NULL) {
		return -EINVAL;
	}
	memcpy(buf, (uint8_t *)ptr + offset, len);
	return len;
}


static int mount_oid(const char *mntPoint, oid_t *oid)
{
	msg_t msg = { 0 };
	struct stat stbuf;
	oid_t target;
	int err;

	while (lookup("/", NULL, &target) < 0) {
		usleep(10000);
	}

	if (mkdir(mntPoint, ACCESSPERMS) != 0) {
		return -errno;
	}

	if (lookup(mntPoint, &target, NULL) < EOK) {
		return -ENOENT;
	}

	if (stat(mntPoint, &stbuf) != 0) {
		return -errno;
	}

	if (!S_ISDIR(stbuf.st_mode)) {
		return -ENOTDIR;
	}

	msg.type = mtSetAttr;
	msg.oid = target;
	msg.i.data = oid;
	msg.i.size = sizeof(oid_t);
	msg.i.attr.type = atDev;

	err = msgSend(target.port, &msg);

	return (err < 0) ? err : msg.o.err;
}


static int getArgMountPoint(const char *arg, unsigned long *imgAddr, const char **mntPoint)
{
	char *end;
	errno = 0;
	*imgAddr = strtoul(arg, &end, 0);
	if ((errno != 0) || (end[0] != ':') || (end[1] != '/') || (end[2] == '\0')) {
		return -1;
	}

	*mntPoint = ++end;

	return 0;
}


int main(int argc, char **argv)
{
	msg_rid_t rid;
	struct rofs_ctx ctx = { 0 };
	oid_t target = { 0 };
	msg_t msg;
	int res = 0;
	unsigned long imgAddr;
	const char *mntPoint;

	if ((argc != 2) || (getArgMountPoint(argv[1], &imgAddr, &mntPoint) != 0)) {
		fprintf(stderr,
			"Usage: %s address:path\n"
			"address - physical address of ROFS image in AHB space of flash device\n"
			"path    - mount point path\n",
			argv[0]);
		return EXIT_FAILURE;
	}


	/* address in AHB memory where whole ROFS image is loaded by other process */
	if (rofs_init(&ctx, rofs_ahbRead, imgAddr) < 0) {
		LOG("error");
		return EXIT_FAILURE;
	}

	portCreate(&target.port);
	rofs_setdev(&ctx, &target);

#if 0
	if (portRegister(port, "/", &target) < 0) {
		LOG("Can't mount as rootfs");
		return 1;
	}
#else
	if (mount_oid(mntPoint, &target) < 0) {
		LOG("Unable to mount at %s", mntPoint);
		return EXIT_FAILURE;
	}
	LOG("mounted at %s", mntPoint);
#endif

	for (;;) {
		res = msgRecv(target.port, &msg, &rid);
		if (res < 0) {
			if (res != -EINTR) {
				LOG("fatal error %d", res);
				break;
			}
			continue;
		}

		switch (msg.type) {
			case mtOpen:
				msg.o.err = rofs_open(&ctx, &msg.oid);
				break;

			case mtClose:
				msg.o.err = rofs_close(&ctx, &msg.oid);
				break;

			case mtRead:
				msg.o.err = rofs_read(&ctx, &msg.oid, msg.i.io.offs, msg.o.data, msg.o.size);
				break;

			case mtWrite:
				msg.o.err = rofs_write(&ctx, &msg.oid, msg.i.io.offs, msg.i.data, msg.i.size);
				break;

			case mtTruncate:
				msg.o.err = rofs_truncate(&ctx, &msg.oid, msg.i.io.len);
				break;

			case mtDevCtl:
				msg.o.err = rofs_devctl(&ctx, &msg);
				break;

			case mtCreate:
				msg.o.err = rofs_create(&ctx, &msg.oid, msg.i.data, &msg.o.create.oid, msg.i.create.mode, msg.i.create.type, &msg.i.create.dev);
				break;

			case mtDestroy:
				msg.o.err = rofs_destroy(&ctx, &msg.oid);
				break;

			case mtSetAttr:
				msg.o.err = rofs_setattr(&ctx, &msg.oid, msg.i.attr.type, msg.i.attr.val, msg.i.data, msg.i.size);
				break;

			case mtGetAttr:
				msg.o.err = rofs_getattr(&ctx, &msg.oid, msg.i.attr.type, &msg.o.attr.val);
				break;

			case mtGetAttrAll:
				msg.o.err = rofs_getattrall(&ctx, &msg.oid, msg.o.data, msg.o.size);
				break;

			case mtLookup:
				msg.o.err = rofs_lookup(&ctx, &msg.oid, msg.i.data, &msg.o.lookup.fil, &msg.o.lookup.dev);
				break;

			case mtLink:
				msg.o.err = rofs_link(&ctx, &msg.oid, msg.i.data, &msg.i.ln.oid);
				break;

			case mtUnlink:
				msg.o.err = rofs_unlink(&ctx, &msg.oid, msg.i.data);
				break;

			case mtReaddir:
				msg.o.err = rofs_readdir(&ctx, &msg.oid, msg.i.readdir.offs, msg.o.data, msg.o.size);
				break;

			case mtStat:
				msg.o.err = rofs_statfs(&ctx, msg.o.data, msg.o.size);
				break;

			case mtMount:
				msg.o.err = rofs_mount(&ctx, &msg.oid, (mount_i_msg_t *)msg.i.raw, (mount_o_msg_t *)msg.o.raw);
				break;

			default:
				LOG("unknown msg.type=%d", msg.type);
				msg.o.err = -ENOSYS;
				break;
		}

		msgRespond(target.port, &msg, rid);
	}

	return EXIT_FAILURE;
}
