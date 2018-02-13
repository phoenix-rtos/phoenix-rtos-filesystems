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
	while (start < len)
	{
		split_path(name, &start, &end, len);
		if (start == end)
			continue;

		err = dir_find(d, name + start, end - start, res);

		mutexUnlock(d->lock);
		object_put(d);

		if (err < 0)
			return err;

		o = object_get(res->id);

		if (o == NULL) {
			object_put(o);
			return -ENOENT;
		}

		res->port = ext2->port;

		d = o;
		start = end;
		mutexLock(d->lock);
	}

	mutexUnlock(d->lock);
	object_put(d);
	return end;
}


static int ext2_setattr(oid_t *oid, int type, int attr)
{
	ext2_object_t *o = object_get(oid->id);
	int res = EOK;

	if (o == NULL)
		return -EINVAL;

	mutexLock(o->lock);

	switch(type) {

	case 0:
		o->inode->mode |= (attr & 0x1FF);
		break;
	case 1:
		o->inode->uid = attr;
		break;
	case 2:
		o->inode->gid = attr;
		break;
	case 3:
		mutexUnlock(o->lock);
		ext2_truncate(oid, attr);
		mutexLock(o->lock);
		break;
	case 4:
		res = -EINVAL;
		break;
	case 5:
		o->oid.port = attr;
		break;
	}

	mutexUnlock(o->lock);
	object_put(o);
	return res;
}


static int ext2_getattr(oid_t *oid, int type, int *attr)
{
	ext2_object_t *o = object_get(oid->id);

	if (o == NULL)
		return -EINVAL;

	mutexLock(o->lock);

	switch(type) {

	case 0:
		*attr = o->inode->mode;
		break;
	case 1:
		*attr = o->inode->uid;
		break;
	case 2:
		*attr = o->inode->gid;
		break;
	case 3:
		*attr = o->inode->size;
		break;
	case 4:
		*attr = o->type;
		break;
	case 5:
		*attr = o->oid.port;
		break;
	}

	mutexUnlock(o->lock);
	object_put(o);
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
	o->type = type;

	object_put(o);

	return EOK;
}


static int ext2_destroy(oid_t *oid)
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

	mutexLock(d->lock);
	if ((res = dir_add(d, name, o->inode->mode, oid)) == EOK) {
		o->inode->links_count++;
		o->inode->uid = 0;
		o->inode->gid = 0;
		if(o->inode->mode & EXT2_S_IFDIR) {
			dir_add(o, ".", EXT2_S_IFDIR, oid);
			o->inode->links_count++;
			dir_add(o, "..", EXT2_S_IFDIR, dir);
			d->inode->links_count++;
		}
		inode_set(o->oid.id, o->inode);
		inode_set(d->oid.id, d->inode);
	}
	mutexUnlock(d->lock);
	object_put(o);
	object_put(d);
	return res;
}


static int ext2_unlink(oid_t *dir, const char *name)
{
	ext2_object_t *d, *o;
	oid_t toid;

	d = object_get(dir->id);

	if (d == NULL)
		return -EINVAL;

	if (!(d->inode->mode & EXT2_S_IFDIR)) {
		object_put(d);
		return -ENOTDIR;
	}

	mutexLock(d->lock);

	if (dir_find(d, name, strlen(name), &toid) != EOK) {
		mutexUnlock(d->lock);
		object_put(d);
		return -ENOENT;
	}

	o = object_get(toid.id);

	dir_remove(d, name);
	o->inode->links_count--;

	mutexUnlock(d->lock);
	object_put(d);

	if (o->inode->mode & EXT2_S_IFDIR) {
		o->inode->links_count--;
		ext2_destroy(&o->oid);
		return EOK;
	}

	if (!o->inode->links_count)
		ext2_destroy(&o->oid);

	return EOK;
}



static int ext2_readdir(oid_t *dir, offs_t offs, struct dirent *dent, unsigned int size)
{
	ext2_object_t *d;
	ext2_dir_entry_t *dentry;

	d = object_get(dir->id);

	if (d == NULL)
		return -EINVAL;

	if (!(d->inode->mode & EXT2_S_IFDIR)) {
		object_put(d);
		return -ENOTDIR;
	}

	if (!d->inode->links_count) {
		object_put(d);
		return -ENOENT;
	}

	dentry = malloc(size);

	mutexLock(d->lock);
	if (offs < d->inode->size) {
		mutexUnlock(d->lock);
		ext2_read_locked(dir, offs, (void *)dentry, size);

		dent->d_ino = dentry->inode;
		dent->d_reclen = dentry->rec_len;
		dent->d_namlen = dentry->name_len;
		dent->d_type = dentry->file_type & EXT2_FT_DIR ? 0 : 1;
		memcpy(&(dent->d_name[0]), dentry->name, dentry->name_len);
		dent->d_name[dentry->name_len] = '\0';

		object_put(d);
		return 	EOK;
	}
	mutexUnlock(d->lock);

	free(dentry);
	return -ENOENT;
}


static void ext2_open(oid_t *oid)
{
	object_get(oid->id);
}


static void ext2_close(oid_t *oid)
{
	ext2_object_t *o = object_get(oid->id);
	object_put(o);
	object_put(o);
}

int main(void)
{
	u32 port;
	oid_t toid;
	msg_t msg;
	unsigned int rid;
	ext2 = malloc(sizeof(ext2_info_t));

	ata_init();

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

	printf("ext2: initialized %s\n", "");

	for (;;) {
		/* message handling loop */
		msgRecv(port, &msg, &rid);
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