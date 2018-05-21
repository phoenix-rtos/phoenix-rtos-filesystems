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
#include "nodelist.h"

#define JFFS2_ROOT_DIR &jffs2_common.root


static int jffs2_srv_lookup(oid_t *dir, const char *name, oid_t *res)
{
	struct dentry *dentry, *dtemp;
	jffs2_object_t *o;
	struct inode *inode = NULL;
	int len = 0;
	char *path;
	char *end;

	if (dir->id == 0)
		dir->id = 1;

	res->id = 0;

	inode = jffs2_iget(jffs2_common.sb, dir->id);

	if (IS_ERR(inode))
		return -EINVAL;

	dentry = malloc(sizeof(struct dentry));
	res->port = jffs2_common.port;

	while (name[len] != '\0') {
		while (name[len] == '/') {
			len++;
			continue;
		}

		dentry->d_name.name = strdup(name + len);

		end = strchr(dentry->d_name.name, '/');
		if (end != NULL)
			*end = 0;

		if (!strcmp(dentry->d_name.name, ".")) {
			res->id = inode->i_ino;
			len++;
			free(dentry->d_name.name);
			dentry->d_name.len = 0;
			continue;
		} else if (!strcmp(dentry->d_name.name, "..")) {
			res->id = JFFS2_INODE_INFO(inode)->inocache->pino_nlink;
			len += 2;
			free(dentry->d_name.name);
			dentry->d_name.len = 0;
			continue;
		}

		dentry->d_name.len = strlen(dentry->d_name.name);

		dtemp = jffs2_lookup(inode, dentry, 0);

		if (dtemp == NULL) {
			free(dentry->d_name.name);
			break;
		} else
			res->id = dtemp->d_inode->i_ino;

		len += dentry->d_name.len;
		free(dentry->d_name.name);
		dentry->d_name.len = 0;

		inode = jffs2_iget(jffs2_common.sb, res->id);
	}

	free(dentry);

	if (!res->id)
		return -ENOENT;

	return len;
}


static int jffs2_srv_setattr(oid_t *oid, int type, int attr)
{
	return 0;
}


static int jffs2_srv_getattr(oid_t *oid, int type, int *attr)
{
	struct inode *inode;
	struct jffs2_inode_info *f;

	inode = jffs2_iget(jffs2_common.sb, oid->id);

	if (IS_ERR(inode))
		return -ENOENT;

	f = JFFS2_INODE_INFO(inode);

	mutex_lock(&f->sem);
	switch (type) {

		case (0): /* mode */
			*attr = inode->i_mode;
			break;

		case (1): /* uid */
			*attr = inode->i_uid.val;
			break;

		case (2): /* gid */
			*attr = inode->i_gid.val;
			break;

		case (3): /* size */
			*attr = inode->i_size;
			break;

		case (4): /* type */
			if (S_ISDIR(inode->i_mode))
				*attr = 0;
			else if (S_ISREG(inode->i_mode))
				*attr = 1;
			else if (S_ISCHR(inode->i_mode))
				*attr = 2;
			else
				*attr = 3;
			break;

		case (5): /* port */
			*attr = inode->i_rdev;
			break;
	}

	mutex_unlock(&f->sem);

	return EOK;
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
	return EOK;
}


static int jffs2_srv_destroy(oid_t *oid)
{
	return 0;
}


static int jffs2_srv_readdir(oid_t *dir, offs_t offs, struct dirent *dent, unsigned int size)
{
	struct inode *inode;
	struct file file;
	struct dir_context ctx = {dir_print, offs, dent, -1};
	jffs2_object_t *o;

	inode = jffs2_iget(jffs2_common.sb, dir->id);

	if (IS_ERR(inode))
		return -EINVAL;

	if (!((inode->i_mode >> 14) & 1))
		return -EINVAL;

	file.f_pino = JFFS2_INODE_INFO(inode)->inocache->pino_nlink;
	file.f_inode = inode;

	jffs2_readdir(&file, &ctx);

	return ctx.emit;
}


static void jffs2_srv_open(oid_t *oid)
{
//	printf("jffs2: open\n");
}


static void jffs2_srv_close(oid_t *oid)
{
}


static int jffs2_srv_read(oid_t *oid, offs_t offs, void *data, unsigned long len)
{
	jffs2_object_t *o;
	struct inode *inode;
	struct jffs2_inode_info *f = JFFS2_INODE_INFO(inode);
	struct jffs2_sb_info *c = JFFS2_SB_INFO(inode->i_sb);
	unsigned char *pg_buf;
	int ret;

	inode = jffs2_iget(jffs2_common.sb, oid->id);

	if (IS_ERR(inode))
		return -EINVAL;

	f = JFFS2_INODE_INFO(inode);
	c = JFFS2_SB_INFO(inode->i_sb);

	if (inode->i_size < offs)
		return -EINVAL;

	mutex_lock(&f->sem);
	ret = jffs2_read_inode_range(c, f, data, offs, len);
	mutex_unlock(&f->sem);

	if (!ret)
		return len;

	return ret;
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

	object_init();
	if(init_jffs2_fs() != EOK) {
		printf("jffs2: Error initialising jffs2\n");
		return -1;
	}

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
