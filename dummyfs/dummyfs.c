/*
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * dummyfs
 *
 * Copyright 2012, 2016, 2018 Phoenix Systems
 * Copyright 2007 Pawel Pisarczyk
 * Author: Jacek Popko, Katarzyna Baranowska, Pawel Pisarczyk, Kamil Amanowicz
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
	dummyfs_object_t *o, *d;
	int err;

	if (dir == NULL)
		d = object_get(0);
	else if ((d = object_get(dir->id)) == NULL)
		return -ENOENT;

	if (d->type != otDir) {
		object_put(d);
		return -EINVAL;
	}

	object_lock(d);

	err = dir_find(d, name, res);
	o = object_get(res->id);

	o->desc++;

	object_put(o);
	object_unlock(d);
	object_put(d);

	return err;
}

int dummyfs_setattr(oid_t *oid, int type, int attr)
{
	dummyfs_object_t *o;
	int ret = EOK;


	if ((o = object_get(oid->id)) == NULL)
		return -ENOENT;
	object_lock(o);
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
			object_unlock(o);
			ret = dummyfs_truncate(oid, attr);
			object_lock(o);
			break;
		case (atPort):
			if (o->type == otDir)
				o->oid.port = attr;
			break;
	}
	object_unlock(o);
	object_put(o);

	return ret;
}


int dummyfs_getattr(oid_t *oid, int type, int *attr)
{
	dummyfs_object_t *o;

	if ((o = object_get(oid->id)) == NULL)
		return -ENOENT;

	object_lock(o);
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
	object_unlock(o);
	object_put(o);

	return EOK;
}


int dummyfs_link(oid_t *dir, const char *name, oid_t *oid)
{
	dummyfs_object_t *d, *o;
	int ret;

	if (name == NULL)
		return -EINVAL;

	if ((d = object_get(dir->id)) == NULL)
		return -ENOENT;

	if ((o = object_get(oid->id)) == NULL) {
		object_put(d);
		return -ENOENT;
	}

	if (d->type != otDir) {
		object_put(o);
		object_put(d);
		return -EINVAL;
	}

	if (o->type == otDir && o->refs > 1) {
		object_put(o);
		object_put(d);
		return -EINVAL;
	}


	if (o->type == otDir) {
		object_lock(o);
		dir_add(o, ".", otDir, oid);
		dir_add(o, "..", otDir, dir);
		object_unlock(o);
	}

	object_lock(d);

	ret = dir_add(d, name, o->type, oid);

	if (ret != EOK) {
		object_unlock(d);
		if (o->type != otDir) {
			object_lock(o);
			dir_destroy(o);
			object_unlock(o);
		}
		object_put(o);
	}

	object_unlock(d);
	object_put(d);

	return ret;
}


int dummyfs_unlink(oid_t *dir, const char *name)
{
	oid_t oid;
	dummyfs_object_t *o, *d;
	int ret;

	d = object_get(dir->id);

	if (d == NULL)
		return -EINVAL;

	object_lock(d);

	if (dir_find(d, name, &oid) < 0)
		return -ENOENT;

	o = object_get(oid.id);

	if (o == NULL) {
		object_unlock(d);
		object_put(d);
		return -ENOENT;
	}

	if (o->type == otDir && dir_empty(o) != EOK) {
		object_unlock(d);
		object_put(d);
		object_put(o);
		return -EINVAL;
	}

	ret = dir_remove(d, name);

	object_put(o);
	object_put(o);

	object_unlock(d);
	object_put(d);

	return ret;
}


int dummyfs_create(oid_t *oid, int type, int mode, u32 port)
{
	dummyfs_object_t *o;

	o = object_create();

	if (o == NULL)
		return -ENOMEM;

	object_lock(o);
	o->oid.port = type == otDev ? port : dummyfs_common.port;
	o->type = type;
	o->mode = mode;
	memcpy(oid, &o->oid, sizeof(oid_t));

	object_unlock(o);
	object_put(o);

	return EOK;
}


int dummyfs_destroy(oid_t *oid)
{
	dummyfs_object_t *o;
	int ret = EOK;


	o = object_get(oid->id);

	if (o == NULL)
		return -ENOENT;

	if ((ret = object_remove(o)) == EOK) {
		if (o->type == otFile)
			dummyfs_truncate(oid, 0);
		else if (o->type == otDir)
			dir_destroy(o);

		dummyfs_decsz(sizeof(dummyfs_object_t));
		free(o);
	}

	return ret;
}


int dummyfs_readdir(oid_t *dir, offs_t offs, struct dirent *dent, unsigned int size)
{
	dummyfs_object_t *d;
	dummyfs_dirent_t *ei;
	offs_t diroffs = 0;
	int ret = -ENOENT;

	d = object_get(dir->id);

	if (d == NULL)
		return -ENOENT;

	if (d->type != otDir) {
		object_put(d);
		return -EINVAL;
	}

	object_lock(d);

	if ((ei = d->entries) == NULL) {
		object_unlock(d);
		object_put(d);
		return -EINVAL;
	}

	do {
		if (diroffs >= offs) {
			if ((sizeof(struct dirent) + ei->len) > size) {
				object_unlock(d);
				object_put(d);
				return 	-EINVAL;
			}

			dent->d_ino = ei->oid.id;
			dent->d_reclen = sizeof(struct dirent) + ei->len;
			dent->d_namlen = ei->len;
			dent->d_type = ei->type;
			memcpy(&(dent->d_name[0]), ei->name, ei->len);

			object_unlock(d);
			object_put(d);
			return 	EOK;
		}
		diroffs += sizeof(struct dirent) + ei->len;
		ei = ei->next;
	} while (ei != d->entries);

	object_unlock(d);
	object_put(d);
	return 	ret;
}


static void dummyfs_open(oid_t *oid)
{
	dummyfs_object_t *o;

	o = object_get(oid->id);

	object_lock(o);
	if (o->desc == 0)
		o->desc++;

	object_unlock(o);
	object_put(o);
}

static void dummyfs_close(oid_t *oid)
{
	dummyfs_object_t *o;

	o = object_get(oid->id);

	object_lock(o);
	if (o->desc > 0)
		o->desc--;

	object_unlock(o);
	object_put(o);
}


int main(void)
{
	oid_t toid = { 0 };
	oid_t root = { 0 };
	msg_t msg;
	dummyfs_object_t *o;
	unsigned int rid;

	dummyfs_common.size = 0;

	/* Wait for console to start */
	while (write(0, "", 1) < 0)
		usleep(5000);

	portCreate(&dummyfs_common.port);
	printf("dummyfs: Starting dummyfs server at port %d\n", dummyfs_common.port);

	/* Try to mount fs as root */
	if (portRegister(dummyfs_common.port, "/", &toid) < 0) {
		printf("dummyfs: Can't mount on directory %s\n", "/");
		return -1;
	}

	object_init();

	mutexCreate(&dummyfs_common.mutex);

	/* Create root directory */
	if (dummyfs_create(&root, otDir, 0, 0) != EOK)
		return -1;

	o = object_get(root.id);
	dir_add(o, ".", otDir, &root);
	dir_add(o, "..", otDir, &root);

	for (;;) {
		msgRecv(dummyfs_common.port, &msg, &rid);

		switch (msg.type) {

		case mtOpen:
			dummyfs_open(&msg.i.openclose.oid);
			break;

		case mtClose:
			dummyfs_close(&msg.i.openclose.oid);
			break;

		case mtRead:
			msg.o.io.err = dummyfs_read(&msg.i.io.oid, msg.i.io.offs, msg.o.data, msg.o.size);
			break;

		case mtWrite:
			msg.o.io.err = dummyfs_write(&msg.i.io.oid, msg.i.io.offs, msg.i.data, msg.i.size);
			break;

		case mtTruncate:
			msg.o.io.err = dummyfs_truncate(&msg.i.io.oid, msg.i.io.len);
			break;

		case mtDevCtl:
			msg.o.io.err = -EINVAL;
			break;

		case mtCreate:
			dummyfs_create(&msg.o.create.oid, msg.i.create.type, msg.i.create.mode, msg.i.create.port);
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

		case mtLookup:
			msg.o.lookup.err = dummyfs_lookup(&msg.i.lookup.dir, msg.i.data, &msg.o.lookup.res);
			break;

		case mtLink:
			msg.o.io.err = dummyfs_link(&msg.i.ln.dir, msg.i.data, &msg.i.ln.oid);
			break;

		case mtUnlink:
			msg.o.io.err = dummyfs_unlink(&msg.i.ln.dir, msg.i.data);
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
