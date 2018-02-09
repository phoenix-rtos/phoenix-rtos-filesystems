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
#include <dirent.h>

#include "ext2.h"
#include "inode.h"
#include "mbr.h"
#include "file.h"
#include "sb.h"
#include "dir.h"
#include "inode.h"
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
	ext2_object_t *d, *o;
	oid_t toid;
	int res;

	if (dir == NULL || oid == NULL)
		return -EINVAL;

	if (dir->id < 2 || oid->id < 2)
		return -EINVAL;

	d = object_get(dir->id);
	o = object_get(oid->id);

	if (o == NULL || d == NULL)
		return -EINVAL;

	if (!(d->inode->mode & EXT2_S_IFDIR)) {
		object_put(o);
		object_put(d);
		return -ENOTDIR;
	}

	if (dir_find(d, name, strlen(name), &toid) == EOK) {
		object_put(o);
		object_put(d);
		return -EEXIST;
	}

	if((o->inode->mode & EXT2_S_IFDIR) && o->inode->links_count) {
		object_put(o);
		object_put(d);
		return -EMLINK;
	}

	if ((res = dir_add(d, name, o->inode->mode, oid)) == EOK) {
		o->inode->links_count++;
		o->inode->uid = 0;
		o->inode->gid = 0;
		if(o->inode->mode & EXT2_S_IFDIR) {
			dir_add(o, ".", EXT2_FT_DIR, oid);
			o->inode->links_count++;
			dir_add(o, "..", EXT2_FT_DIR, dir);
			d->inode->links_count++;
		}
		inode_set(o->oid.id, o->inode);
		inode_set(d->oid.id, d->inode);
	}

	object_put(o);
	object_put(d);
	return res;
}


static int ext2_unlink(oid_t *dir, const char *name)
{
	return EOK;
}


static int ext2_create(oid_t *oid, int type, int mode, u32 port)
{

	ext2_object_t *o;
	ext2_inode_t *inode;
	u32 ino;

	inode = malloc(ext2->inode_size);

	switch (type) {
	case 0: /* dir */
		mode = EXT2_S_IFDIR;
		break;
	case 1: /* file */
		mode = EXT2_S_IFREG;
		break;
	case 2: /* dev */
		mode = EXT2_S_IFCHR;
		break;
	}

	/* this section should be locked */
	ino = inode_create(inode, mode);

	/*TODO: this should be cached not written to drive */
	if (type & 3) /* Device */
		inode->block[0] = port;

	o = object_create(ino, inode);

	*oid = o->oid;

	object_put(o);

	return EOK;
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

	toid.id = 2;
	if(portRegister(port, "/", &toid) < 0) {
		printf("ext2: registering port failed %s\n", "");
		return 0;
	}
	ext2->port = port;

	object_init();
/*
	ext2_inode_t *inode = inode_get(11);
		printf("ph inode\nmode: 0x%x\nuid: %u\nsize: %u\natime: %u\nctime: %u\nmtime: %u\ndtime: %u\ngid: %u\nlinks_count: %u\nblocks: %u\nflags: 0x%x\n", inode->mode, inode->uid, inode->size, inode->atime, 
		inode->ctime, inode->mtime, inode->dtime, inode->gid, inode->links_count,
		inode->blocks, inode->flags);
		inode = inode_get(12);
		printf("ph inode\nmode: 0x%x\nuid: %u\nsize: %u\natime: %u\nctime: %u\nmtime: %u\ndtime: %u\ngid: %u\nlinks_count: %u\nblocks: %u\nflags: 0x%x\n", inode->mode, inode->uid, inode->size, inode->atime, 
		inode->ctime, inode->mtime, inode->dtime, inode->gid, inode->links_count,
		inode->blocks, inode->flags);
*/

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
				printf("link result %d\n", msg.o.io.err);
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
