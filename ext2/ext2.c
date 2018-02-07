/*
 * Phoenix-RTOS
 *
 * ext2
 *
 * ext2.c
 *
 * Copyright 2017 Phoenix Systems
 * Author: Kamil Amanowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/msg.h>

#include "ext2.h"
#include "inode.h"
#include "mbr.h"
#include "file.h"
#include "sb.h"
#include "dir.h"
#include "object.h"
#include "pc-ata.h"

#define READ_MAX 2048

static int ext2_lookup(oid_t *dir, const char *name, oid_t *res)
{
	u32 start = 0, end = 0;
    u32 ino = dir ? dir->id : ROOTNODE_NO;
	int err;
	u32 len = strlen(name);
	ext2_object_t *d, *o;

	if (ino < 2)
		return -EINVAL;

	d = object_get(ino);

	if (d == NULL)
		return -ENOENT;

	mutexLock(d->lock);
    while (start != len)
    {
        split_path(name, &start, &end, len);
        if (start == end)
            continue;

        err = dir_find(d, name + start, end - start, res);

		mutexUnlock(d->lock);
		object_put(d);

		if (err <= 0)
			return err;

		o = object_get(res->id);

		/* this should not happen */
		if (o == NULL)
			return -ENOENT;

		if (o->inode->mode & (EXT2_S_IFBLK | EXT2_S_IFSOCK | EXT2_S_IFCHR))
			res->port = o->oid.port;
		else
			res->port = ext2->port;

		d = o;
        start = end;
    }

	object_put(d);
	return end;
}


static int ext2_setattr(oid_t *oid, int type, int attr)
{
	return EOK;
}


static int ext2_getattr(oid_t *oid, int type, int *attr)
{
	return EOK;
}


static int ext2_link(oid_t *dir, const char *name, oid_t *oid)
{
	//ext2_object_t *d = object_get(dir->id);
	//ext2_object_t *o = object_get(oid->id);
	return EOK;
}


static int ext2_unlink(oid_t *dir, const char *name)
{
	return EOK;
}


static int ext2_create(oid_t *oid, int type, int mode, u32 port)
{



	return -EINVAL;
}

static int ext2_destroy(oid_t *oid)
{
	return EOK;
}

static int ext2_readdir(oid_t *dir, offs_t offs, struct dirent *dent, unsigned int size)
{
	return EOK;
}


static void ext2_open(oid_t *oid)
{
}


static void ext2_close(oid_t *oid)
{
}

int main(void)
{
    u32 port;
	oid_t toid;
	msg_t msg;
	unsigned int rid;
    ext2 = malloc(sizeof(ext2_info_t));

    ata_init();

    printf("ext2: initialization %s\n", "");
    if (ext2_init()) {
        printf("ext2: init error %s\n", "");
        return 0;
    }

    if (portCreate(&port) < 0) {
        printf("ext2: creating port failed %s\n", "");
        return 0;
    }

    if(portRegister(port, "/", &toid) < 0) {
        printf("ext2: registering port failed %s\n", "");
        return 0;
    }
	ext2->port = port;


  	for (;;) {
        /* message handling loop */

		msgRecv(port, &msg, &rid);
		printf("ext2: msg type %d\n", msg.type);
		switch (msg.type) {

		case mtOpen:
			ext2_open(&msg.i.openclose.oid);
			break;

		case mtClose:
			ext2_close(&msg.i.openclose.oid);
			break;

		case mtRead:
			msg.o.io.err = ext2_read(&msg.i.io.oid, msg.i.io.offs, msg.o.data, msg.o.size);
			break;

		case mtWrite:
			msg.o.io.err = ext2_write(&msg.i.io.oid, msg.i.io.offs, msg.i.data, msg.i.size);
			break;

		case mtTruncate:
			msg.o.io.err = ext2_truncate(&msg.i.io.oid, msg.i.io.len);
			break;

		case mtDevCtl:
			msg.o.io.err = -EINVAL;
			break;

		case mtCreate:
			ext2_create(&msg.o.create.oid, msg.i.create.type, msg.i.create.mode, msg.i.create.port);
			break;

		case mtDestroy:
			msg.o.io.err = ext2_destroy(&msg.i.destroy.oid);
			break;

		case mtSetAttr:
			ext2_setattr(&msg.i.attr.oid, msg.i.attr.type, msg.i.attr.val);
			break;

		case mtGetAttr:
			ext2_getattr(&msg.i.attr.oid, msg.i.attr.type, &msg.o.attr.val);
			break;

		case mtLookup:
			msg.o.lookup.err = ext2_lookup(&msg.i.lookup.dir, msg.i.data, &msg.o.lookup.res);
			break;

		case mtLink:
			msg.o.io.err = ext2_link(&msg.i.ln.dir, msg.i.data, &msg.i.ln.oid);
			break;

		case mtUnlink:
			msg.o.io.err = ext2_unlink(&msg.i.ln.dir, msg.i.data);
			break;

		case mtReaddir:
			msg.o.io.err = ext2_readdir(&msg.i.readdir.dir, msg.i.readdir.offs,
										msg.o.data, msg.o.size);
			break;
		}
		msgRespond(port, &msg, rid);

	}

    if(ext2) {
        free(ext2->mbr);
        free(ext2->sb);
        free(ext2->gdt);
        free(ext2);
    }
    return 0;
}
