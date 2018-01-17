/*
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * dummyfs
 *
 * Copyright 2012, 2016, 2018 Phoenix Systems
 * Copyright 2007 Pawel Pisarczyk
 * Author: Jacek Popko, Katarzyna Baranowska, Pawel Pisarczyk
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/threads.h>
#include <sys/msg.h>
#include <sys/list.h>
#include <unistd.h>
#include <dirent.h>

#include "dummyfs.h"
#include "dir.h"
#include "file.h"
#include "object.h"


struct _dummyfs_common_t dummyfs_common;

int dummyfs_lookup(oid_t *dir, const char *name, oid_t *res)
{
	dummyfs_object_t *d;
	int err;

	mutexLock(dummyfs_common.mutex);

	if(dir == NULL)
		d = object_get(0);
	else if ((d = object_get(dir->id)) == NULL) {
		mutexUnlock(dummyfs_common.mutex);
		return -ENOENT;
	}

	if (d->type != otDir) {
		object_put(d);
		mutexUnlock(dummyfs_common.mutex);
		return -EINVAL;
	}

	err = dir_find(d, name, res);
	object_get(res->id);
	object_put(d);
	mutexUnlock(dummyfs_common.mutex);

	return err;
}

int dummyfs_setattr(oid_t *oid, int type, int attr)
{
	dummyfs_object_t *o;
	int ret = EOK;

	mutexLock(dummyfs_common.mutex);

	if ((o = object_get(oid->id)) == NULL)
		return -ENOENT;

	switch (type) {

		case (atUid):
			o->uid = attr;
			break;

		case (atGid):
			o->gid = attr;
			break;

		case (atMode):
			o->mode = attr;
			break;

		case (atSize):
			ret = dummyfs_truncate(oid, attr);
			break;
		case (atPort):
			if (o->type == otDir)
				o->oid.port = attr;
			break;
	}

	object_put(o);
	mutexUnlock(dummyfs_common.mutex);

	return ret;
}

int dummyfs_getattr(oid_t *oid, int type, int *attr)
{
	dummyfs_object_t *o;

	mutexLock(dummyfs_common.mutex);

	if ((o = object_get(oid->id)) == NULL)
		return -ENOENT;

	switch (type) {

		case (atUid):
			*attr = o->uid;
			break;

		case (atGid):
			*attr = o->gid;
			break;

		case (atMode):
			*attr = o->mode;
			break;

		case (atSize):
			*attr = o->size;
			break;

		case (atType):
			*attr = o->type;
			break;
	}

	object_put(o);
	mutexUnlock(dummyfs_common.mutex);

	return EOK;
}

int dummyfs_link(oid_t *dir, const char *name, oid_t *oid)
{
	dummyfs_object_t *d, *o;
	int ret;

	if (name == NULL)
		return -EINVAL;

	mutexLock(dummyfs_common.mutex);

	if ((d = object_get(dir->id)) == NULL) {
		mutexUnlock(dummyfs_common.mutex);
		return -ENOENT;
	}

	if ((o = object_get(oid->id)) == NULL) {
		object_put(d);
		mutexUnlock(dummyfs_common.mutex);
		return -ENOENT;
	}

	if (d->type != otDir) {
		object_put(o);
		object_put(d);
		mutexUnlock(dummyfs_common.mutex);
		return -EINVAL;
	}

	if (o->type == otDir && o->refs > 1) {
		object_put(o);
		object_put(d);
		mutexUnlock(dummyfs_common.mutex);
		return -EINVAL;
	}

	if (o->type == otDir) {
		dir_add(o, ".", oid);
		dir_add(o, "..", dir);
	}
	ret = dir_add(d, name, oid);

	object_put(d);
	mutexUnlock(dummyfs_common.mutex);

	return ret;
}

int dummyfs_unlink(oid_t *dir, const char *name)
{
	oid_t oid;
	dummyfs_object_t *o, *d;
	int ret;

	mutexLock(dummyfs_common.mutex);

	d = object_get(dir->id);

	if (d == NULL) {
		mutexUnlock(dummyfs_common.mutex);
		return -EINVAL;
	}

	if ((ret = dir_find(d, name, &oid)) < 0) {
		object_put(d);
		mutexUnlock(dummyfs_common.mutex);
		return ret;
	}

	o = object_get(oid.id);

	if (o == NULL) {
		object_put(d);
		mutexUnlock(dummyfs_common.mutex);
		return -ENOENT;
	}

	if (o->type == otDir && dir_empty(o) == EOK) {
		object_put(d);
		object_put(o);
		mutexUnlock(dummyfs_common.mutex);
		return -EINVAL;
	}

	ret = dir_remove(d, name);
	object_put(o);
	object_put(o);
	object_put(d);
	mutexUnlock(dummyfs_common.mutex);

	return ret;
}

int dummyfs_create(oid_t *oid, int type, int mode)
{
	dummyfs_object_t *o;
	unsigned int id;

	mutexLock(dummyfs_common.mutex);

	if (dummyfs_cksz(sizeof(dummyfs_object_t)) != EOK) {
		mutexUnlock(dummyfs_common.mutex);
		return -ENOMEM;
	}

	o = object_create(NULL, &id);

	if (o == NULL)
		return -EINVAL;

	dummyfs_incsz(sizeof(dummyfs_object_t));
	o->oid.port = dummyfs_common.port;
	o->type = type;
	o->mode = mode;
	memcpy(oid, &o->oid, sizeof(oid_t));
	mutexUnlock(dummyfs_common.mutex);

	return EOK;
}

int dummyfs_destroy(oid_t *oid)
{
	dummyfs_object_t *o;
	int ret = EOK;

	mutexLock(dummyfs_common.mutex);

	o = object_get(oid->id);

	if (o == NULL) {
		mutexUnlock(dummyfs_common.mutex);
		return -ENOENT;
	}

	if (object_destroy(o) == EOK) {
		if (o->type == otFile)
			dummyfs_truncate(oid, 0);
		else if (o->type == otDir) {
			dir_destroy(o);
		}
		free(o);
	}
	dummyfs_decsz(sizeof(dummyfs_object_t));

	mutexUnlock(dummyfs_common.mutex);
	return ret;
}

int dummyfs_readdir(oid_t *dir, offs_t offs, struct dirent *dent, unsigned int size)
{
	dummyfs_object_t *d;
	dummyfs_dirent_t *ei;
	offs_t diroffs = 0;
	int ret = -ENOENT;

	mutexLock(dummyfs_common.mutex);

	d = object_get(dir->id);

	if (d == NULL) {
		mutexUnlock(dummyfs_common.mutex);
		return -ENOENT;
	}

	if (d->type != otDir) {
		object_put(d);
		mutexUnlock(dummyfs_common.mutex);
		return -EINVAL;
	}

	if ((ei = d->entries) == NULL) {
		object_put(d);
		mutexUnlock(dummyfs_common.mutex);
		return -EINVAL;
	}

	do {
		if(diroffs >= offs) {
			if ((sizeof(struct dirent) + ei->len) > size) {
				ret = -EINVAL;
				goto out;
			}
			dent->d_ino = ei->oid.id;
			dent->d_reclen = sizeof(struct dirent) + ei->len;
			dent->d_namlen = ei->len;
			memcpy(&(dent->d_name[0]), ei->name, ei->len);
			ret = EOK;
			goto out;
		}
		diroffs += sizeof(struct dirent) + ei->len;
		ei = ei->next;
	} while (ei != d->entries);

out:
	object_put(d);
	mutexUnlock(dummyfs_common.mutex);
	return 	ret;
}

int main(void)
{
	oid_t toid = { 0 };
	oid_t root = { 0 };
	msg_t msg;
	dummyfs_object_t *o;
	unsigned int rid;

	dummyfs_common.size = 0;

	usleep(500000);
	portCreate(&dummyfs_common.port);
	printf("dummyfs: Starting dummyfs server at port %d\n", dummyfs_common.port);

	/* Try to mount fs as root */
	if (portRegister(dummyfs_common.port, "/", &toid) == EOK)
		printf("dummyfs: Mounted as root %s\n", "");

	object_init();

	mutexCreate(&dummyfs_common.mutex);

	/* Create root directory */
	if (dummyfs_create(&root, otDir, 0) != EOK)
		return -1;

	o = object_get(root.id);
	dir_add(o, ".", &root);
	dir_add(o, "..", &root);

	for (;;) {
		msgRecv(dummyfs_common.port, &msg, &rid);

		switch (msg.type) {

		case mtOpen:
			//mutexLock(dummyfs_common.mutex);
			//o = object_get(msg.i.open.oid.id);
			//mutexUnlock(dummyfs_common.mutex);
			break;

		case mtClose:
			mutexLock(dummyfs_common.mutex);
			o = object_get(msg.i.openclose.oid.id);
			object_put(o);
			object_put(o);
			mutexUnlock(dummyfs_common.mutex);
			break;

		case mtWrite:
			msg.o.io.err = dummyfs_write(&msg.i.io.oid, msg.i.io.offs, msg.i.data, msg.i.size);
			break;

		case mtRead:
			msg.o.io.err = dummyfs_read(&msg.i.io.oid, msg.i.io.offs, msg.o.data, msg.o.size);
			break;

		case mtTruncate:
			msg.o.io.err = dummyfs_truncate(&msg.i.io.oid, msg.i.io.len);
			break;

		case mtCreate:
			dummyfs_create(&msg.o.create.oid, msg.i.create.type, msg.i.create.mode);
			if (msg.i.create.type == otDev) {
				mutexLock(dummyfs_common.mutex);
				o = object_get(msg.o.create.oid.id);
				if (o != NULL)
					o->oid.port = msg.i.create.port;
				object_put(o);
				mutexUnlock(dummyfs_common.mutex);
			}
			break;

		case mtDestroy:
			msg.o.io.err = dummyfs_destroy(&msg.i.destroy.oid);
			break;

		case mtSetAttr:
			dummyfs_setattr(&msg.i.attr.oid, msg.i.attr.type, msg.i.attr.val);
			break;

		case mtGetAttr:
			dummyfs_getattr(&msg.i.attr.oid, msg.i.attr.type, &msg.o.attr.val);
			break;

		case mtLink:
			msg.o.io.err = dummyfs_link(&msg.i.ln.dir, msg.i.data, &msg.i.ln.oid);
			break;

		case mtUnlink:
			msg.o.io.err = dummyfs_unlink(&msg.i.ln.dir, msg.i.data);
			break;

		case mtLookup:
			msg.o.lookup.err = dummyfs_lookup(&msg.i.lookup.dir, msg.i.data, &msg.o.lookup.res);
			break;

		case mtDevCtl:
			msg.o.io.err = -EINVAL;
			break;

		case mtReaddir:
			msg.o.io.err = dummyfs_readdir(&msg.i.readdir.dir, msg.i.readdir.offs,
										msg.o.data, msg.o.size);
			break;
		}
		msgRespond(dummyfs_common.port, &msg, rid);
	}

	return EOK;
}
