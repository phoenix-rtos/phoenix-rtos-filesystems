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

#include "dummyfs.h"
#include "object.h"


struct {
	handle_t mutex;
} dummyfs_common;


int dummyfs_lookup(oid_t *oid, const char *name, oid_t *res)
{
	dummyfs_object_t *d;
	int err;

	if ((d = object_get(oid->id)) == NULL) {
		mutexUnlock(dummyfs_common.mutex);
		return -ENOENT;
	}

	mutexLock(dummyfs_common.mutex);

	if (d->type != otDir) {
		mutexUnlock(dummyfs_common.mutex);
		object_put(d);
		return -EINVAL;
	}

	err = dir_find(d, name, res);

	mutexUnlock(dummyfs_common.mutex);
	object_put(d);

	return err;
}


int dummyfs_link(oid_t *dir, const char *name, oid_t *oid)
{
	dummyfs_object_t *d, *o;

	if (name == NULL)
		return -EINVAL;

	if ((d = object_get(dir->id)) == NULL)
		return -ENOENT;

	if ((o = object_get(oid->id)) == NULL) {
		object_put(d);
		return -ENOENT;
	}

	mutexLock(dummyfs_common.mutex);

	if (d->type != otDir) {
		mutexUnlock(dummyfs_common.mutex);
		object_put(o);
		object_put(d);
		return -EINVAL;
	}

	if (o->type == otDir) {
		mutexUnlock(dummyfs_common.mutex);
		object_put(o);
		object_put(d);
		return -EINVAL;
	}

	dir_add(d, name, oid);

	mutexUnlock(dummyfs_common.mutex);
	object_put(d);
	
	return EOK;
}


int dummyfs_unlink(oid_t *dir, const char *name)
{
	oid_t oid;
	dummyfs_object_t *o, *d;

	dummyfs_lookup(dir, name, &oid);

	d = object_get(dir->id);
	o = object_get(oid.id);

	if (o->type == otDir)
		return -EINVAL;

	mutexLock(dummyfs_common.mutex);

	dir_remove(d, name);

	if (object_destroy(o) == EOK) {

		/* Release object data */
	}

	mutexUnlock(dummyfs_common.mutex);

	return EOK;
}


int main(void)
{
	u32 port;
	oid_t toid;
	msg_t msg;
	unsigned int rid;
	
	usleep(500000);
	portCreate(&port);
	printf("dummyfs: Starting dummyfs server at port %d\n", port);

	/* Try to mount fs as root */
	if (portRegister(port, "/", &toid) == EOK)
		printf("dummyfs: Mounted as root %s\n", "");

	mutexCreate(&dummyfs_common.mutex);

	/* Create root directory */


	for (;;) {
		msgRecv(port, &msg, &rid);

		switch (msg.type) {
		case mtOpen:
			break;
		case mtWrite:
//			msg.o.io.err = dummyfs_write(&msg.i.io.oid, msg.i.data, msg.i.size);
			break;
		case mtRead:
			msg.o.io.err = 0;
			msg.o.size = 1;
//			msg.o.io.err = dummyfs_read(&msg.i.io.oid, msg.o.data, msg.o.size);
			break;
		case mtClose:
			break;
		}

		msgRespond(port, rid);
	}

	return EOK;
}
