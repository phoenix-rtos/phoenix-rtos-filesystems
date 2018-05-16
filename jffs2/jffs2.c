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


#include "os-phoenix.h"
#include "os-phoenix/object.h"

struct {
	u32 port;
	oid_t root;
	struct super_block sb;
} jffs2_common;


#define JFFS2_ROOT_DIR &jffs2_common.root


static int jffs2_srv_lookup(oid_t *dir, const char *name, oid_t *res)
{
	printf("jffs2: loookup\n");
	return 0;
	struct inode *inode = NULL;
	struct dentry dentry;
	u8 path_end = 0;

	if (dir == NULL)
		dir = JFFS2_ROOT_DIR;

	inode = jffs2_iget(&jffs2_common.sb, dir->id);

	while (inode != NULL) {
	//	jffs2_lookup(inode, &dentry, 0);
		//if (ret != NULL)
			inode = dentry.d_inode;

		//if (path_forward(name))
			break;
	}

	if (inode == NULL)
		return -ENOENT;

	return 0;
}


static int jffs2_srv_setattr(oid_t *oid, int type, int attr)
{
	return 0;
}


static int jffs2_srv_getattr(oid_t *oid, int type, int *attr)
{
	return -ENOTSUP;
}


static int jffs2_srv_link(oid_t *dir, const char *name, oid_t *oid)
{
	return 0;
}


static int jffs2_srv_unlink(oid_t *dir, const char *name)
{
	return 0;
}


static int jffs2_srv_create(oid_t *oid, int type, int mode, u32 port)
{
	jffs2_object_t *o;

//	o = object_create(type);

	if (o == NULL)
		return -EINVAL;

	oid->id = 0;
	oid->port = o->oid.port;

	return EOK;
}


static int jffs2_srv_destroy(oid_t *oid)
{
	return 0;
}


static int jffs2_srv_readdir(oid_t *dir, offs_t offs, struct dirent *dent, unsigned int size)
{
	return 0;
}


static void jffs2_srv_open(oid_t *oid)
{
}


static void jffs2_srv_close(oid_t *oid)
{
}


static int jffs2_srv_read(oid_t *oid, offs_t offs, void *data, unsigned long len)
{
	return 0;
}


static int jffs2_srv_write(oid_t *oid, offs_t offs, void *data, unsigned long len)
{
	return 0;
}


static int jffs2_srv_truncate(oid_t *oid, unsigned long len)
{
	return 0;
}


int main(void)
{
	oid_t toid = { 0 };
	msg_t msg;
	unsigned int rid;

	portCreate(&jffs2_common.port);

	printf("jffs2: Starting jffs2 server at port %d\n", jffs2_common.port);

	if(init_jffs2_fs() != EOK) {
		printf("jffs2: Error initialising jffs2\n");
		return -1;
	}
//	while(1) usleep(100000);
	printf("object_init\n");
	object_init();
	printf("object_init done\n");

	/* Try to mount fs as root */
	if (portRegister(jffs2_common.port, "/", &toid) < 0) {
		printf("jffs2: Can't mount on directory %s\n", "/");
		return -1;
	}

	for (;;) {
		if (msgRecv(jffs2_common.port, &msg, &rid) < 0) {
			msgRespond(jffs2_common.port, &msg, rid);
			continue;
		}

		switch (msg.type) {

			case mtOpen:
				jffs2_srv_open(&msg.i.openclose.oid);
				break;

			case mtClose:
				jffs2_srv_close(&msg.i.openclose.oid);
				break;

			case mtRead:
				msg.o.io.err = jffs2_srv_read(&msg.i.io.oid, msg.i.io.offs, msg.o.data, msg.o.size);
				break;

			case mtWrite:
				msg.o.io.err = jffs2_srv_write(&msg.i.io.oid, msg.i.io.offs, msg.i.data, msg.i.size);
				break;

			case mtTruncate:
				msg.o.io.err = jffs2_srv_truncate(&msg.i.io.oid, msg.i.io.len);
				break;

			case mtDevCtl:
				msg.o.io.err = -EINVAL;
				break;

			case mtCreate:
				jffs2_srv_create(&msg.o.create.oid, msg.i.create.type, msg.i.create.mode, msg.i.create.port);
				break;

			case mtDestroy:
				msg.o.io.err = jffs2_srv_destroy(&msg.i.destroy.oid);
				break;

			case mtSetAttr:
				jffs2_srv_setattr(&msg.i.attr.oid, msg.i.attr.type, msg.i.attr.val);
				break;

			case mtGetAttr:
				jffs2_srv_getattr(&msg.i.attr.oid, msg.i.attr.type, &msg.o.attr.val);
				break;

			case mtLookup:
				msg.o.lookup.err = jffs2_srv_lookup(&msg.i.lookup.dir, msg.i.data, &msg.o.lookup.res);
				break;

			case mtLink:
				msg.o.io.err = jffs2_srv_link(&msg.i.ln.dir, msg.i.data, &msg.i.ln.oid);
				break;

			case mtUnlink:
				msg.o.io.err = jffs2_srv_unlink(&msg.i.ln.dir, msg.i.data);
				break;

			case mtReaddir:
				msg.o.io.err = jffs2_srv_readdir(&msg.i.readdir.dir, msg.i.readdir.offs,
						msg.o.data, msg.o.size);
				break;
		}

		msgRespond(jffs2_common.port, &msg, rid);
	}

	return EOK;
}
