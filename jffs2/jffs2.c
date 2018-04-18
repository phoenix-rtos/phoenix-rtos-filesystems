/*
 * Phoenix-RTOS
 *
 * Phoenix file server
 *
 * jffs2
 *
 * Copyright 2012, 2016, 2018 Phoenix Systems
 * Copyright 2007 Pawel Pisarczyk
 * Author: Kamil Amanowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/msg.h>
#include <sys/list.h>
#include <sys/mman.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>

struct {
	u32 port;
} jffs2_common;


static int jffs2_lookup(oid_t *dir, const char *name, oid_t *res)
{
	return 0;
}


static int jffs2_setattr(oid_t *oid, int type, int attr)
{
	return 0;
}


static int jffs2_getattr(oid_t *oid, int type, int *attr)
{
	return 0;
}


static int jffs2_link(oid_t *dir, const char *name, oid_t *oid)
{
	return 0;
}


static int jffs2_unlink(oid_t *dir, const char *name)
{
	return 0;
}


static int jffs2_create(oid_t *oid, int type, int mode, u32 port)
{
	return 0;
}


static int jffs2_destroy(oid_t *oid)
{
	return 0;
}


static int jffs2_readdir(oid_t *dir, offs_t offs, struct dirent *dent, unsigned int size)
{
	return 0;
}


static void jffs2_open(oid_t *oid)
{
}


static void jffs2_close(oid_t *oid)
{
}


static int jffs2_read(oid_t *oid, offs_t offs, void *data, unsigned long len)
{
	return 0;
}


static int jffs2_write(oid_t *oid, offs_t offs, void *data, unsigned long len)
{
	return 0;
}


static int jffs2_truncate(oid_t *oid, unsigned long len)
{
	return 0;
}


int main(void)
{
	oid_t toid = { 0 };
	msg_t msg;
	unsigned int rid;

	portCreate(&jffs2_common.port);

	/* Try to mount fs as root */
	if (portRegister(jffs2_common.port, "/", &toid) < 0) {
		printf("jffs2: Can't mount on directory %s\n", "/");
		return -1;
	}
	printf("jffs2: Starting jffs2 server at port %d\n", jffs2_common.port);

	//object_init();

	for (;;) {
		if (msgRecv(jffs2_common.port, &msg, &rid) < 0) {
			msgRespond(jffs2_common.port, &msg, rid);
			continue;
		}

		switch (msg.type) {

			case mtOpen:
				jffs2_open(&msg.i.openclose.oid);
				break;

			case mtClose:
				jffs2_close(&msg.i.openclose.oid);
				break;

			case mtRead:
				msg.o.io.err = jffs2_read(&msg.i.io.oid, msg.i.io.offs, msg.o.data, msg.o.size);
				break;

			case mtWrite:
				msg.o.io.err = jffs2_write(&msg.i.io.oid, msg.i.io.offs, msg.i.data, msg.i.size);
				break;

			case mtTruncate:
				msg.o.io.err = jffs2_truncate(&msg.i.io.oid, msg.i.io.len);
				break;

			case mtDevCtl:
				msg.o.io.err = -EINVAL;
				break;

			case mtCreate:
				jffs2_create(&msg.o.create.oid, msg.i.create.type, msg.i.create.mode, msg.i.create.port);
				break;

			case mtDestroy:
				msg.o.io.err = jffs2_destroy(&msg.i.destroy.oid);
				break;

			case mtSetAttr:
				jffs2_setattr(&msg.i.attr.oid, msg.i.attr.type, msg.i.attr.val);
				break;

			case mtGetAttr:
				jffs2_getattr(&msg.i.attr.oid, msg.i.attr.type, &msg.o.attr.val);
				break;

			case mtLookup:
				msg.o.lookup.err = jffs2_lookup(&msg.i.lookup.dir, msg.i.data, &msg.o.lookup.res);
				break;

			case mtLink:
				msg.o.io.err = jffs2_link(&msg.i.ln.dir, msg.i.data, &msg.i.ln.oid);
				break;

			case mtUnlink:
				msg.o.io.err = jffs2_unlink(&msg.i.ln.dir, msg.i.data);
				break;

			case mtReaddir:
				msg.o.io.err = jffs2_readdir(&msg.i.readdir.dir, msg.i.readdir.offs,
						msg.o.data, msg.o.size);
				break;
		}
		msgRespond(jffs2_common.port, &msg, rid);
	}

	return EOK;
}
