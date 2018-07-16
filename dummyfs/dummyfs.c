/*
 * Phoenix-RTOS
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
#include <sys/mman.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>

#include "dummyfs.h"
#include "dir.h"
#include "file.h"
#include "object.h"
#include "dev.h"
#include "usb.h"
#include "../../phoenix-rtos-kernel/include/sysinfo.h"

struct _dummyfs_common_t dummyfs_common;

int dummyfs_destroy(oid_t *oid);


static inline int dummyfs_device(oid_t *oid)
{
	return oid->port != dummyfs_common.port;
}


static inline dummyfs_object_t *dummyfs_get(oid_t *oid)
{
	return dummyfs_device(oid) ? dev_find(oid, 0) : object_get(oid->id);
}


int dummyfs_lookup(oid_t *dir, const char *name, oid_t *res)
{
	dummyfs_object_t *o, *d;
	int len = 0;
	int err = 0;

	if (dir == NULL)
		d = object_get(0);
	else if (dummyfs_device(dir))
		return -EINVAL;
	else if ((d = object_get(dir->id)) == NULL)
		return -ENOENT;

	if (d->type != otDir) {
		object_put(d);
		return -EINVAL;
	}

	object_lock(d);
	while (name[len] != '\0') {
		while (name[len] == '/')
			len++;

		err = dir_find(d, name + len, res);

		if (err <= 0)
			break;

		len += err;
		object_unlock(d);
		object_put(d);

		if (dummyfs_device(res))
			break;

		d = object_get(res->id);
		object_lock(d);
	}

	if (err < 0) {
		object_unlock(d);
		object_put(d);
		return err;
	}

	o = dummyfs_get(res);

	if (o->type == otDev)
		memcpy(res, &o->dev, sizeof(oid_t));

	o->lock = 1;

	object_put(o);
	object_unlock(d);
	object_put(d);

	return len;
}

int dummyfs_setattr(oid_t *oid, int type, int attr)
{
	dummyfs_object_t *o;
	int ret = EOK;


	if ((o = dummyfs_get(oid)) == NULL)
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
			ret = -EINVAL;
			break;
	}
	object_unlock(o);
	object_put(o);

	return ret;
}


int dummyfs_getattr(oid_t *oid, int type, int *attr)
{
	dummyfs_object_t *o;

	if ((o = dummyfs_get(oid)) == NULL)
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

		case (atPort):
			*attr = o->oid.port;
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

	if (dummyfs_device(dir))
		return -EINVAL;

	if ((d = object_get(dir->id)) == NULL)
		return -ENOENT;

	if ((o = dummyfs_get(oid)) == NULL) {
		object_put(d);
		return -ENOENT;
	}

	if (d->type != otDir) {
		object_put(o);
		object_put(d);
		return -EINVAL;
	}

	if (o->type == otDir && o->nlink != 0) {
		object_put(o);
		object_put(d);
		return -EINVAL;
	}

	o->nlink++;

	if (o->type == otDir) {
		object_lock(o);
		dir_add(o, ".", otDir, oid);
		dir_add(o, "..", otDir, dir);
		o->nlink++;
		object_unlock(o);
		object_lock(d);
		d->nlink++;
		object_unlock(d);
	}

	object_lock(d);
	ret = dir_add(d, name, o->type, oid);

	if (ret != EOK) {
		object_unlock(d);
		object_lock(o);
		o->nlink--;
		if (o->type == otDir) {
			o->nlink--;
			dir_destroy(o);
		}
		object_unlock(o);
	}

	object_unlock(d);
	object_put(o);
	object_put(d);

	return ret;
}


int dummyfs_unlink(oid_t *dir, const char *name)
{
	oid_t oid;
	dummyfs_object_t *o, *d;
	int ret;

	if (name == NULL)
		return -EINVAL;

	if (!strcmp(name, ".") || !strcmp(name, ".."))
		return -EINVAL;

	if (dummyfs_device(dir))
		return -EINVAL;

	d = object_get(dir->id);

	if (d == NULL)
		return -EINVAL;

	object_lock(d);

	if (dir_find(d, name, &oid) < 0) {
		object_unlock(d);
		object_put(d);
		return -ENOENT;
	}

	if (oid.id == 0) {
		object_unlock(d);
		object_put(d);
		return -EINVAL;
	}

	o = dummyfs_get(&oid);

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

	if (ret == EOK && o->type == otDir)
		d->nlink--;

	object_unlock(d);
	object_put(d);

	if (ret == EOK) {
		object_lock(o);
		o->nlink--;
		if (o->type == otDir)
			o->nlink--;
		object_unlock(o);
	}

	object_put(o);
	dummyfs_destroy(&o->oid);

	return ret;
}


int dummyfs_create(oid_t *dir, const char *name, oid_t *oid, int type, int mode, oid_t *dev)
{
	dummyfs_object_t *o;
	int ret;

	if (type == otDev)
		o = dev_find(dev, 1);
	else
		o = object_create();

	if (o == NULL)
		return -ENOMEM;

	object_lock(o);
	o->oid.port = dummyfs_common.port;
	o->type = type;
	o->mode = mode;

	if (type == otDev)
		memcpy(oid, dev, sizeof(oid_t));
	else
		memcpy(oid, &o->oid, sizeof(oid_t));

	object_unlock(o);

	if((ret = dummyfs_link(dir, name, &o->oid)) != EOK) {
		object_put(o);
		object_remove(o);
		free(o);
		return ret;
	}

	object_put(o);
	return EOK;
}


int dummyfs_destroy(oid_t *oid)
{
	dummyfs_object_t *o;
	int ret = EOK;

	o = dummyfs_get(oid);

	if (o == NULL)
		return -ENOENT;

	if ((ret = object_remove(o)) == EOK) {
		if (o->type == otFile)
			dummyfs_truncate(oid, 0);
		else if (o->type == otDir)
			dir_destroy(o);
		else if (o->type == otDev)
			dev_destroy(oid);

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

	if (dummyfs_device(dir))
		return -EINVAL;

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


static int dummyfs_open(oid_t *oid)
{
	dummyfs_object_t *o;

	if ((o = dummyfs_get(oid)) == NULL)
		return -ENOENT;

	object_lock(o);
	o->lock = 0;

	object_unlock(o);
	return EOK;
}

static int dummyfs_close(oid_t *oid)
{
	dummyfs_object_t *o;

	if ((o = dummyfs_get(oid)) == NULL)
		return -ENOENT;

	object_lock(o);
	o->lock = 0;

	object_unlock(o);
	object_put(o);
	object_put(o);
	return EOK;
}

#ifdef TARGET_IA32

int fetch_modules(void)
{
	oid_t root = {dummyfs_common.port, 0};
	oid_t toid = { 0 };
	oid_t sysoid = { 0 };
	void *prog_addr;
	syspageprog_t prog;
	int i, progsz;

	progsz = syspageprog(NULL, -1);
	dummyfs_create(&root, "syspage", &sysoid, otDir, 0, NULL);

	for (i = 0; i < progsz; i++) {
		syspageprog(&prog, i);
		prog_addr = (void *)mmap(NULL, (prog.size + 0xfff) & ~0xfff, 0x1 | 0x2, 0, OID_PHYSMEM, prog.addr);
		dummyfs_create(&sysoid, prog.name, &toid, otFile, 0, NULL);
		dummyfs_write(&toid, 0, prog_addr, prog.size);

		munmap(prog_addr, (prog.size + 0xfff) & ~0xfff);
	}
	return EOK;
}

#endif


int main(int argc,char **argv)
{
	oid_t root = { 0 };
	msg_t msg;
	dummyfs_object_t *o;
	unsigned int rid;

#ifndef TARGET_IA32
	oid_t toid = { 0 };
	u32 reserved;
#endif

	dummyfs_common.size = 0;

#ifdef TARGET_IA32
	while(write(0, "", 1) < 0)
		usleep(500000);
#else
	portCreate(&reserved);
	portRegister(reserved, "/reserved", &toid);
#endif

	portCreate(&dummyfs_common.port);

	/* Try to mount fs as root */
	if (portRegister(dummyfs_common.port, "/", &root) < 0) {
		printf("dummyfs: Can't mount on directory %s\n", "/");
		return -1;
	}

#ifndef TARGET_IA32
	portDestroy(reserved);
#endif

	printf("dummyfs: Starting dummyfs server at port %d\n", dummyfs_common.port);

	object_init();
	dev_init();

	mutexCreate(&dummyfs_common.mutex);

	/* Create root directory */
	o = object_create();

	if (o == NULL)
		return -1;

	o->type = otDir;
	o->oid.port = dummyfs_common.port;
	o->mode = 0;

	memcpy(&root, &o->oid, sizeof(oid_t));
	dir_add(o, ".", otDir, &root);
	dir_add(o, "..", otDir, &root);

	fetch_modules();

	for (;;) {
		if (msgRecv(dummyfs_common.port, &msg, &rid) < 0) {
			msgRespond(dummyfs_common.port, &msg, rid);
			continue;
		}

		switch (msg.type) {

			case mtOpen:
				msg.o.io.err = dummyfs_open(&msg.i.openclose.oid);
				break;

			case mtClose:
				msg.o.io.err = dummyfs_close(&msg.i.openclose.oid);
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
				msg.o.create.err = dummyfs_create(&msg.i.create.dir, msg.i.data, &msg.o.create.oid, msg.i.create.type, msg.i.create.mode, &msg.i.create.dev);
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
