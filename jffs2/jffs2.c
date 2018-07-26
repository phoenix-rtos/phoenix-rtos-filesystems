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
#include <sys/file.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>

#include "os-phoenix.h"
#include "os-phoenix/object.h"
#include "os-phoenix/dev.h"
#include "nodelist.h"


jffs2_common_t jffs2_common;


int jffs2_readdir(struct file *file, struct dir_context *ctx);


inline int jffs2_is_device(oid_t *oid)
{
	return jffs2_common.port != oid->port;
}


struct inode *jffs2_srv_get(oid_t *oid)
{
	jffs2_dev_t *dev;

	if (jffs2_is_device(oid)) {
		dev = dev_find_oid(oid, 0, 0);
		return jffs2_iget(jffs2_common.sb, dev->ino);
	}

	return jffs2_iget(jffs2_common.sb, oid->id);
}


static int jffs2_srv_lookup(oid_t *dir, const char *name, oid_t *res, oid_t *dev)
{
	struct dentry *dentry, *dtemp;
	struct inode *inode = NULL;
	int len = 0;
	char *end;

	if (dir->id == 0)
		dir->id = 1;

	if (jffs2_is_device(dir))
		return -EINVAL;

	res->id = 0;

	inode = jffs2_iget(jffs2_common.sb, dir->id);

	if (IS_ERR(inode))
		return -EINVAL;

	if (!S_ISDIR(inode->i_mode)) {
		iput(inode);
		return -ENOTDIR;
	}

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
			iput(inode);
			inode = jffs2_iget(jffs2_common.sb, res->id);
			continue;
		}

		dentry->d_name.len = strlen(dentry->d_name.name);

		if (S_ISDIR(inode->i_mode))
			dtemp = inode->i_op->lookup(inode, dentry, 0);
		else {
			free(dentry->d_name.name);
			free(dentry);
			iput(inode);
			return -ENOTDIR;
		}

		if (dtemp == NULL || PTR_ERR(dtemp) == -ENAMETOOLONG) {
			free(dentry->d_name.name);
			free(dentry);
			iput(inode);
			return -EINVAL;
		} else
			res->id = dtemp->d_inode->i_ino;

		len += dentry->d_name.len;
		free(dentry->d_name.name);
		dentry->d_name.len = 0;

		iput(inode);
		inode = d_inode(dtemp);
	}

	if (S_ISCHR(inode->i_mode) && dev_find_ino(res->id) != NULL)
		memcpy(dev, &(dev_find_ino(res->id)->dev), sizeof(oid_t));
	else
		memcpy(dev, res, sizeof(oid_t));

	free(dentry);
	iput(inode);

	if (res->port == jffs2_common.port && !res->id)
		return -ENOENT;

	return len;
}


static int jffs2_srv_setattr(oid_t *oid, int type, int attr)
{
	struct iattr iattr;
	struct inode *inode;
	struct jffs2_inode_info *f;
	struct dentry dentry;
	int ret;
	struct jffs2_sb_info *c = JFFS2_SB_INFO(jffs2_common.sb);

	if (jffs2_is_readonly(c))
		return -EROFS;

	inode = jffs2_iget(jffs2_common.sb, oid->id);
	if (IS_ERR(inode))
		return -ENOENT;

	f = JFFS2_INODE_INFO(inode);

	mutex_lock(&f->sem);

	switch (type) {

		case (atMode): /* mode */
			iattr.ia_valid = ATTR_MODE;
			iattr.ia_mode = (inode->i_mode & ~0xffff) | (attr & 0xffff);
			break;

		case (atUid): /* uid */
			iattr.ia_valid = ATTR_UID;
			iattr.ia_uid.val = attr;
			break;

		case (atGid): /* gid */
			iattr.ia_valid = ATTR_GID;
			iattr.ia_gid.val = attr;
			break;

		case (atSize): /* size */
			iattr.ia_valid = ATTR_SIZE;
			iattr.ia_size = attr;
			break;

		case (atPort): /* port */
			inode->i_rdev = attr;
			break;
	}

	d_instantiate(&dentry, inode);

	mutex_unlock(&f->sem);

	ret = inode->i_op->setattr(&dentry, &iattr);
	iput(inode);

	return ret;
}


static int jffs2_srv_getattr(oid_t *oid, int type, int *attr)
{
	struct inode *inode;
	struct jffs2_inode_info *f;

	if (!oid->id)
		return -EINVAL;

	inode = jffs2_iget(jffs2_common.sb, oid->id);

	if (IS_ERR(inode))
		return -ENOENT;

	f = JFFS2_INODE_INFO(inode);

	mutex_lock(&f->sem);
	switch (type) {

		case (atMode): /* mode */
			*attr = inode->i_mode;
			break;

		case (atUid): /* uid */
			*attr = inode->i_uid.val;
			break;

		case (atGid): /* gid */
			*attr = inode->i_gid.val;
			break;

		case (atSize): /* size */
			*attr = inode->i_size;
			break;

		case (atType): /* type */
			if (S_ISDIR(inode->i_mode))
				*attr = otDir;
			else if (S_ISREG(inode->i_mode))
				*attr = otFile;
			else if (S_ISCHR(inode->i_mode))
				*attr = otDev;
			else
				*attr = otUnknown;
			break;

		case (atCTime):
			*attr = inode->i_ctime.tv_sec;
			break;

		case (atMTime):
			*attr = inode->i_mtime.tv_sec;
			break;

		case (atATime):
			*attr = inode->i_atime.tv_sec;
			break;

		case (atLinks):
			*attr = inode->i_nlink;
			break;
	}

	mutex_unlock(&f->sem);
	iput(inode);

	return EOK;
}


static int jffs2_srv_link(oid_t *dir, const char *name, oid_t *oid)
{
	struct inode *idir, *inode;
	struct dentry *old, *new;
	oid_t toid, t;
	int ret;
	struct jffs2_sb_info *c = JFFS2_SB_INFO(jffs2_common.sb);

	if (jffs2_is_readonly(c))
		return -EROFS;

	if (!dir->id || !oid->id)
		return -EINVAL;

	if (name == NULL || !strlen(name))
		return -EINVAL;

	if (jffs2_is_device(dir))
		return -EINVAL;

	idir = jffs2_iget(jffs2_common.sb, dir->id);

	if (IS_ERR(idir))
		return -ENOENT;

	if (jffs2_srv_lookup(dir, name, &t, &toid) > 0) {
		iput(idir);
		return -EEXIST;
	}

	inode = jffs2_srv_get(oid);

	if (IS_ERR(inode)) {
		iput(idir);
		return -ENOENT;
	}

	old = malloc(sizeof(struct dentry));
	new = malloc(sizeof(struct dentry));

	new->d_name.name = strdup(name);
	new->d_name.len = strlen(name);

	d_instantiate(old, inode);

	ret = idir->i_op->link(old, idir, new);

	if (ret && S_ISCHR(inode->i_mode))
		dev_inc(oid);

	iput(idir);
	iput(inode);

	free(old);
	free(new->d_name.name);
	free(new);

	return ret;
}


static int jffs2_srv_unlink(oid_t *dir, const char *name)
{
	struct inode *idir, *inode;
	struct dentry *dentry;
	oid_t oid, t;
	int ret;
	struct jffs2_sb_info *c = JFFS2_SB_INFO(jffs2_common.sb);

	if (jffs2_is_readonly(c))
		return -EROFS;

	if (!dir->id)
		return -EINVAL;

	if (name == NULL || !strlen(name))
		return -EINVAL;

	idir = jffs2_iget(jffs2_common.sb, dir->id);

	if (IS_ERR(idir))
		return -ENOENT;

	if (jffs2_srv_lookup(dir, name, &t, &oid) <= 0) {
		iput(idir);
		return -ENOENT;
	}

	inode = jffs2_srv_get(&oid);

	if (IS_ERR(inode)) {
		iput(idir);
		return -ENOENT;
	}

	dentry = malloc(sizeof(struct dentry));

	dentry->d_name.name = strdup(name);
	dentry->d_name.len = strlen(name);

	d_instantiate(dentry, inode);

	if (S_ISDIR(inode->i_mode))
		ret = idir->i_op->rmdir(idir, dentry);
	else
		ret = idir->i_op->unlink(idir, dentry);

	if (!ret && S_ISCHR(inode->i_mode))
		dev_dec(&oid);

	iput(idir);
	iput(inode);

	free(dentry->d_name.name);
	free(dentry);

	return ret;
}


static int jffs2_srv_create(oid_t *dir, const char *name, oid_t *oid, int type, int mode, oid_t *dev)
{
	oid_t toid = { 0 }, t;
	struct inode *idir, *inode;
	struct dentry *dentry;
	int ret = 0;
	struct jffs2_sb_info *c = JFFS2_SB_INFO(jffs2_common.sb);

	if (jffs2_is_readonly(c))
		return -EROFS;

	if (name == NULL || !strlen(name))
		return -EINVAL;

	if (jffs2_is_device(dir))
		return -EINVAL;

	idir = jffs2_iget(jffs2_common.sb, dir->id);

	if (IS_ERR(idir))
		return -ENOENT;

	if (!S_ISDIR(idir->i_mode)) {
		iput(idir);
		return -ENOTDIR;
	}

	if (jffs2_srv_lookup(dir, name, &toid, &t) > 0) {

		ret = -EEXIST;
		if (!jffs2_is_device(&toid)) {

			inode = jffs2_iget(jffs2_common.sb, toid.id);
			if(S_ISCHR(inode->i_mode)) {
				ret = 0;
				dev_find_oid(dev, inode->i_ino, 1);
				oid->id = inode->i_ino;
			}
			iput(inode);
		}
		iput(idir);
		return ret;
	}

	dentry = malloc(sizeof(struct dentry));
	dentry->d_name.name = strdup(name);
	dentry->d_name.len = strlen(name);

	oid->port = jffs2_common.port;

	switch (type) {
		case otFile:
			mode = S_IFREG | S_IRWXU | S_IRWXG | S_IRWXO;
			ret = idir->i_op->create(idir, dentry, mode, 0);
			break;
		case otDir:
			mode = S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO;
			ret = idir->i_op->mkdir(idir, dentry, mode);
			break;
		case otDev:
			mode = S_IFCHR | S_IRWXU | S_IRWXG | S_IRWXO;
			ret = idir->i_op->mknod(idir, dentry, mode, dev->port);
			dev_find_oid(dev, d_inode(dentry)->i_ino, 1);
			break;
		default:
			ret = -EINVAL;
			break;
	}

	iput(idir);

	if (!ret)
		oid->id = d_inode(dentry)->i_ino;

	return ret;
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

	if (!dir->id)
		return -EINVAL;

	inode = jffs2_iget(jffs2_common.sb, dir->id);

	if (IS_ERR(inode))
		return -EINVAL;

	if (!(S_ISDIR(inode->i_mode))) {
		iput(inode);
		return -ENOTDIR;
	}

	file.f_pino = JFFS2_INODE_INFO(inode)->inocache->pino_nlink;
	file.f_inode = inode;

	inode->i_fop->iterate_shared(&file, &ctx);

	iput(inode);

	dent->d_reclen = ctx.pos - offs;

	return ctx.emit;
}


static void jffs2_srv_open(oid_t *oid)
{
	jffs2_iget(jffs2_common.sb, oid->id);
}


static void jffs2_srv_close(oid_t *oid)
{
	struct inode *inode = ilookup(jffs2_common.sb, oid->id);

	if (inode != NULL)
		iput(inode);
}


static int jffs2_srv_read(oid_t *oid, offs_t offs, void *data, unsigned long len)
{
	struct inode *inode;
	struct jffs2_inode_info *f;
	struct jffs2_sb_info *c;
	int ret;

	if (!oid->id)
		return -EINVAL;

	inode = jffs2_iget(jffs2_common.sb, oid->id);

	if (IS_ERR(inode))
		return -EINVAL;

	if(S_ISDIR(inode->i_mode)) {
		iput(inode);
		return -EISDIR;
	}

	if (S_ISCHR(inode->i_mode)) {
		printf("jffs2: Can't read from device I'm just a filesystem\n");
		iput(inode);
		return -EINVAL;
	}

	f = JFFS2_INODE_INFO(inode);
	c = JFFS2_SB_INFO(inode->i_sb);

	if (inode->i_size < offs) {
		iput(inode);
		return 0;
	}

	mutex_lock(&f->sem);
	ret = jffs2_read_inode_range(c, f, data, offs, len);
	mutex_unlock(&f->sem);

	if (!ret)
		ret = len > inode->i_size - offs ? inode->i_size - offs : len;
	else
		printf("jffs2: Read error %d\n", ret);

	iput(inode);
	return ret;
}



static int jffs2_srv_prepare_write(struct inode *inode, loff_t offs, unsigned long len)
{
	struct jffs2_inode_info *f = JFFS2_INODE_INFO(inode);
	struct jffs2_sb_info *c = JFFS2_SB_INFO(inode->i_sb);
	struct jffs2_raw_inode ri;
	struct jffs2_full_dnode *fn;
	uint32_t alloc_len;
	int ret;

	if (len > inode->i_size) {

		jffs2_dbg(1, "Writing new hole frag 0x%x-0x%x between current EOF and new page\n",
			  (unsigned int)inode->i_size, len);

		ret = jffs2_reserve_space(c, sizeof(ri), &alloc_len,
					  ALLOC_NORMAL, JFFS2_SUMMARY_INODE_SIZE);
		if (ret)
			return ret;

		mutex_lock(&f->sem);
		memset(&ri, 0, sizeof(ri));

		ri.magic = cpu_to_je16(JFFS2_MAGIC_BITMASK);
		ri.nodetype = cpu_to_je16(JFFS2_NODETYPE_INODE);
		ri.totlen = cpu_to_je32(sizeof(ri));
		ri.hdr_crc = cpu_to_je32(crc32(0, &ri, sizeof(struct jffs2_unknown_node) - 4));

		ri.ino = cpu_to_je32(f->inocache->ino);
		ri.version = cpu_to_je32(++f->highest_version);
		ri.mode = cpu_to_jemode(inode->i_mode);
		ri.uid = cpu_to_je16(i_uid_read(inode));
		ri.gid = cpu_to_je16(i_gid_read(inode));
		ri.isize = cpu_to_je32(max((uint32_t)inode->i_size, len));
		ri.atime = ri.ctime = ri.mtime = cpu_to_je32(get_seconds());
		ri.offset = cpu_to_je32(inode->i_size);
		ri.dsize = cpu_to_je32(len - inode->i_size);
		ri.csize = cpu_to_je32(0);
		ri.compr = JFFS2_COMPR_ZERO;
		ri.node_crc = cpu_to_je32(crc32(0, &ri, sizeof(ri)-8));
		ri.data_crc = cpu_to_je32(0);

		fn = jffs2_write_dnode(c, f, &ri, NULL, 0, ALLOC_NORMAL);

		if (IS_ERR(fn)) {
			ret = PTR_ERR(fn);
			jffs2_complete_reservation(c);
			mutex_unlock(&f->sem);
			return ret;
		}

		ret = jffs2_add_full_dnode_to_inode(c, f, fn);

		if (f->metadata) {
			jffs2_mark_node_obsolete(c, f->metadata->raw);
			jffs2_free_full_dnode(f->metadata);
			f->metadata = NULL;
		}

		if (ret) {
			jffs2_dbg(1, "Eep. add_full_dnode_to_inode() failed in write_begin, returned %d\n",
				  ret);
			jffs2_mark_node_obsolete(c, fn->raw);
			jffs2_free_full_dnode(fn);
			jffs2_complete_reservation(c);
			mutex_unlock(&f->sem);

			return ret;
		}
		jffs2_complete_reservation(c);
		inode->i_size = len;
		mutex_unlock(&f->sem);
	}

	return 0;
}

static int jffs2_srv_write(oid_t *oid, offs_t offs, void *data, unsigned long len)
{
	struct inode *inode;
	struct jffs2_inode_info *f;
	struct jffs2_sb_info *c;
	struct jffs2_raw_inode *ri;
	uint32_t writelen = 0;
	int ret;

	c = JFFS2_SB_INFO(jffs2_common.sb);
	if (jffs2_is_readonly(c))
		return -EROFS;

	if (!oid->id)
		return -EINVAL;

	ri = jffs2_alloc_raw_inode();

	if (ri == NULL)
		return -ENOMEM;

	inode = jffs2_iget(jffs2_common.sb, oid->id);

	if (IS_ERR(inode)) {
		jffs2_free_raw_inode(ri);
		return -EINVAL;
	}

	if(S_ISDIR(inode->i_mode)) {
		jffs2_free_raw_inode(ri);
		iput(inode);
		return -EISDIR;
	}

	if (S_ISCHR(inode->i_mode)) {
		printf("jffs2: What are you doing? You shouldn't even be here!\n");
		iput(inode);
		return -EINVAL;
	}

	f = JFFS2_INODE_INFO(inode);
	c = JFFS2_SB_INFO(inode->i_sb);

	if ((ret = jffs2_srv_prepare_write(inode, offs, len))) {
		jffs2_free_raw_inode(ri);
		iput(inode);
		return ret;
	}

	f = JFFS2_INODE_INFO(inode);
	c = JFFS2_SB_INFO(inode->i_sb);

	ri->ino = cpu_to_je32(inode->i_ino);
	ri->mode = cpu_to_jemode(inode->i_mode);
	ri->uid = cpu_to_je16(i_uid_read(inode));
	ri->gid = cpu_to_je16(i_gid_read(inode));
	ri->isize = cpu_to_je32((uint32_t)inode->i_size);
	ri->atime = ri->ctime = ri->mtime = cpu_to_je32(get_seconds());

	ret = jffs2_write_inode_range(c, f, ri, data, offs, len, &writelen);

	if (!ret) {
		if (offs + writelen > inode->i_size) {
			inode->i_size = offs + writelen;
			inode->i_blocks = (inode->i_size + 511) >> 9;
			inode->i_ctime = inode->i_mtime = ITIME(je32_to_cpu(ri->ctime));
		}
	} else
		printf("jffs2: Write error %d\n", ret);

	jffs2_free_raw_inode(ri);
	iput(inode);
	return ret ? ret : writelen;
}


static int jffs2_srv_truncate(oid_t *oid, unsigned long len)
{
	struct jffs2_sb_info *c = JFFS2_SB_INFO(jffs2_common.sb);
	if (jffs2_is_readonly(c))
		return -EROFS;

	return jffs2_srv_setattr(oid, 3, len);
}


int main(int argc, char **argv)
{
	oid_t toid = { 0 };
	msg_t msg;
	unsigned int rid;

	while(write(0, "", 1) < 0)
		usleep(500000);

	portCreate(&jffs2_common.port);

	printf("jffs2: Starting jffs2 server at port %d\n", jffs2_common.port);

	if (argc >= 4) {
		jffs2_common.start_block = atoi(argv[1]);
		jffs2_common.size = atoi(argv[2]);
		jffs2_common.mount_path = argv[3];
	} else {
		jffs2_common.start_block = 0;
		jffs2_common.size = 5;
		jffs2_common.mount_path = "/";
	}

	object_init();
	dev_init();
	if(init_jffs2_fs() != EOK) {
		printf("jffs2: Error initialising jffs2\n");
		return -1;
	}

	toid.id = 1;
	if (portRegister(jffs2_common.port, jffs2_common.mount_path, &toid) < 0) {
		printf("jffs2: Can't mount on directory %s\n", jffs2_common.mount_path);
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
				msg.o.create.err = jffs2_srv_create(&msg.i.create.dir, msg.i.data, &msg.o.create.oid, msg.i.create.type, msg.i.create.mode, &msg.i.create.dev);
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
				msg.o.lookup.err = jffs2_srv_lookup(&msg.i.lookup.dir, msg.i.data, &msg.o.lookup.fil, &msg.o.lookup.dev);
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
