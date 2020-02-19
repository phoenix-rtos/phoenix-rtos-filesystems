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
#include <fcntl.h>
#include <sys/msg.h>
#include <sys/list.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <stdarg.h>
#include <poll.h>
#include <sys/mount.h>

#include "phoenix-rtos.h"
#include "phoenix-rtos/object.h"
#include "phoenix-rtos/dev.h"
#include "nodelist.h"

#define TRACE(x, ...) // printf("jffs trace: " x "\n", ##__VA_ARGS__)

jffs2_common_t jffs2_common;


inline int jffs2_is_device(jffs2_partition_t *p, oid_t *oid)
{
	return p->port != oid->port;
}


static int jffs2_srv_lookup(jffs2_partition_t *p, id_t *id, const char *name, const size_t len, id_t *resId, mode_t *mode)
{
	struct dentry *dentry, *dtemp = NULL;
	struct inode *inode = NULL;
	int ret = EOK;

	if (!id || *id < 1)
		return -EINVAL;

	inode = jffs2_iget(p->sb, (unsigned long)*id);

	if (IS_ERR(inode))
		return PTR_ERR(inode);

	if (!S_ISDIR(inode->i_mode)) {
		iput(inode);
		return -ENOTDIR;
	}

	dentry = malloc(sizeof(struct dentry));

	dentry->d_name.name = calloc(1, len + 1);
	if (!dentry->d_name.name) {
		free(dentry);
		iput(inode);
		return -ENOMEM;
	}

	memcpy(dentry->d_name.name, name, len);
	dentry->d_name.len = len;

	if (!strcmp(dentry->d_name.name, ".")) {
		*resId = inode->i_ino;
	}
	else if (!strcmp(dentry->d_name.name, "..")) {
		if (S_ISMNT(inode->i_mode)) {
			*resId = inode->i_ino;
		} else {
			*resId = JFFS2_INODE_INFO(inode)->inocache->pino_nlink;
			dentry->d_inode = jffs2_iget(p->sb, (unsigned long)*resId);
			dtemp = dentry;
		}
	}
	else {
		inode_lock_shared(inode);
		dtemp = inode->i_op->lookup(inode, dentry, 0);

		if (dtemp == NULL)
			ret = -ENOENT;
		else if (IS_ERR(dtemp))
			ret = -PTR_ERR(dtemp);
		else
			*resId = dtemp->d_inode->i_ino;
		inode_unlock_shared(inode);
	}

	if (dtemp) {
		iput(inode);
		inode = d_inode(dtemp);
	}

	*mode = inode->i_mode;

	free(dentry->d_name.name);
	free(dentry);
	iput(inode);

	return ret;
}


static int jffs2_srv_setattr(jffs2_partition_t *p, id_t *id, int type, void *data, size_t size)
{
	struct iattr iattr;
	struct inode *inode;
	struct dentry dentry;
	int ret = EOK;
	struct jffs2_sb_info *c = JFFS2_SB_INFO(p->sb);

	if (!id)
		return -EINVAL;

	if (type != atDev && jffs2_is_readonly(c))
		return -EROFS;



	inode = jffs2_iget(p->sb, (unsigned long)*id);
	if (IS_ERR(inode))
		return -ENOENT;

	inode_lock(inode);
	switch (type) {

		case atMode: /* mode */
			iattr.ia_valid = ATTR_MODE;
			iattr.ia_mode = (inode->i_mode & ~0xffff) | (*(int *)data & 0xffff);
			break;

		case atUid: /* uid */
			iattr.ia_valid = ATTR_UID;
			iattr.ia_uid.val = *(int *)data;
			break;

		case atGid: /* gid */
			iattr.ia_valid = ATTR_GID;
			iattr.ia_gid.val = *(int *)data;
			break;

		case atSize: /* size */
			iattr.ia_valid = ATTR_SIZE;
			iattr.ia_size = *(size_t *)data;
			break;

		case atMount:
		case atMountPoint:
			if (S_ISDIR(inode->i_mode)) {
				inode->i_mnt = *(oid_t *)data;
				inode->i_mode = (inode->i_mode & S_IFMT) | S_IFMNT;
				ihold(inode);
			}
			else {
				ret = -ENOTDIR;
			}

			inode_unlock(inode);
			iput(inode);
			return ret;
	}

	d_instantiate(&dentry, inode);

	ret = inode->i_op->setattr(&dentry, &iattr);
	inode_unlock(inode);
	iput(inode);

	return ret;
}


static int jffs2_srv_getattr(jffs2_partition_t *p, id_t *id, int type, void *data, size_t size)
{
	struct inode *inode;
	struct stat *stat;
	int ret;

	if (!id)
		return -EINVAL;

	if (data == NULL)
		return -EINVAL;

	inode = jffs2_iget(p->sb, (unsigned long)*id);

	if (IS_ERR(inode))
		return -ENOENT;

	ret = sizeof(int);
	inode_lock_shared(inode);
	switch (type) {

		case atMode: /* mode */
			*(int *)data = inode->i_mode;
			break;

		case atStatStruct:
			stat = (struct stat *)data;
			//stat->st_dev = o->port;
			stat->st_ino = inode->i_ino;
			stat->st_mode = inode->i_mode;
			stat->st_nlink = inode->i_nlink;
			stat->st_uid = inode->i_uid.val;
			stat->st_gid = inode->i_gid.val;
			stat->st_size = inode->i_size;
			stat->st_atime = I_SEC(inode->i_atime);
			stat->st_mtime = I_SEC(inode->i_mtime);
			stat->st_ctime = I_SEC(inode->i_ctime);
			ret = sizeof(struct stat);
			break;

		case atSize: /* size */
			*(size_t *)data = inode->i_size;
			break;

		case atEvents:
			// trivial implementation: assume read/write is always possible
			*(int *)data = POLLIN | POLLOUT;
			break;
		case atMount:
		case atMountPoint:
			*(oid_t *)data = inode->i_mnt;
			ret = sizeof(oid_t);
			break;
	}

	inode_unlock_shared(inode);
	iput(inode);

	return ret;
}


static int jffs2_srv_link(jffs2_partition_t *p, id_t *dirId, const char *name, size_t len, id_t *id)
{
	struct inode *idir, *inode, *ivictim = NULL;
	struct jffs2_inode_info *victim_f = NULL;
	struct dentry *old, *new;
	id_t rid;
	int ret;
	mode_t mode;
	struct jffs2_sb_info *c = JFFS2_SB_INFO(p->sb);

	if (jffs2_is_readonly(c))
		return -EROFS;

	if (!dirId || !id)
		return -EINVAL;

	if (name == NULL || !len)
		return -EINVAL;

	idir = jffs2_iget(p->sb, *dirId);

	if (IS_ERR(idir))
		return -ENOENT;

	if (!S_ISDIR(idir->i_mode)) {
		iput(idir);
		return -EINVAL;
	}

	if (jffs2_srv_lookup(p, dirId, name, len, &rid, &mode) == EOK) {
		ivictim = jffs2_iget(p->sb, (unsigned long)rid);

		if (ivictim && (S_ISDIR(ivictim->i_mode) || S_ISMNT(ivictim->i_mode) || (rid == *id))) {
			iput(ivictim);
			iput(idir);
			return -EEXIST;
		}
	}

	inode = jffs2_iget(p->sb, (unsigned long)*id);

	if (IS_ERR(inode)) {
		iput(idir);
		return -ENOENT;
	}

	old = malloc(sizeof(struct dentry));
	new = malloc(sizeof(struct dentry));

	if (!new || !old) {
		free(new);
		free(old);
		iput(inode);
		iput(idir);
		return -ENOMEM;
	}

	new->d_name.name = calloc(1, len + 1);
	if(!new->d_name.name) {
		free(new);
		free(old);
		iput(inode);
		iput(idir);
		return -ENOMEM;
	}

	memcpy(new->d_name.name, name, len);
	new->d_name.len = len;

	d_instantiate(old, inode);

	inode_lock(idir);
	ret = idir->i_op->link(old, idir, new);
	inode_unlock(idir);

//	if (ret && S_ISCHR(inode->i_mode))
//		dev_inc(p->devs, oid);

	iput(idir);
	iput(inode);

	if (!ret && ivictim != NULL) {
		victim_f = JFFS2_INODE_INFO(ivictim);
		mutex_lock(&victim_f->sem);
		if (victim_f->inocache->pino_nlink)
			victim_f->inocache->pino_nlink--;
		mutex_unlock(&victim_f->sem);
		drop_nlink(ivictim);
	}
	iput(ivictim);

	free(old);
	free(new->d_name.name);
	free(new);

	return ret;
}


static int jffs2_srv_unlink(jffs2_partition_t *p, id_t *id, const char *name, const size_t len)
{
	struct inode *idir, *inode;
	struct dentry *dentry;
	id_t rid;
	mode_t mode;
	int ret;
	struct jffs2_sb_info *c = JFFS2_SB_INFO(p->sb);

	if (jffs2_is_readonly(c))
		return -EROFS;

	if (!id)
		return -EINVAL;

	if (name == NULL || !len)
		return -EINVAL;

	idir = jffs2_iget(p->sb, *id);

	if (IS_ERR(idir))
		return -ENOENT;

	if (jffs2_srv_lookup(p, id, name, len, &rid, &mode) != EOK) {
		iput(idir);
		return -ENOENT;
	}

	inode =  jffs2_iget(p->sb, (unsigned long)*id);

	if (IS_ERR(inode)) {
		iput(idir);
		return -ENOENT;
	}

	if (S_ISDIR(inode->i_mode) && dev_find_ino(p->devs, inode->i_ino) != NULL) {
		iput(inode);
		iput(idir);
		return -EBUSY;
	}

	dentry = malloc(sizeof(struct dentry));
	if (!dentry) {
		iput(inode);
		iput(idir);
		return -ENOMEM;
	}

	dentry->d_name.name = calloc(1, len + 1);
	if (!dentry->d_name.name) {
		free(dentry);
		iput(inode);
		iput(idir);
		return -ENOMEM;
	}

	memcpy(dentry->d_name.name, name, len);
	dentry->d_name.len = len;

	d_instantiate(dentry, inode);

	inode_lock(idir);
	if (S_ISDIR(inode->i_mode))
		ret = idir->i_op->rmdir(idir, dentry);
	else
		ret = idir->i_op->unlink(idir, dentry);

//	if (!ret && S_ISCHR(inode->i_mode))
//		dev_dec(p->devs, &oid);
	inode_unlock(idir);

	iput(idir);
	iput(inode);

	free(dentry->d_name.name);
	free(dentry);

	return ret;
}


static int jffs2_srv_create(jffs2_partition_t *p, id_t *dirId, const char *name, const size_t len, id_t *resId, int mode, oid_t *dev)
{
	struct inode *idir, *inode;
	struct dentry *dentry, *dtemp;
	int ret = 0;
	struct jffs2_sb_info *c = JFFS2_SB_INFO(p->sb);

	if (jffs2_is_readonly(c))
		return -EROFS;

	if (name == NULL || !len)
		return -EINVAL;

	idir = jffs2_iget(p->sb, *dirId);

	if (IS_ERR(idir))
		return -ENOENT;

	if (!S_ISDIR(idir->i_mode)) {
		iput(idir);
		return -ENOTDIR;
	}

	if (((len == 1) && !strncmp(".", name, len)) || (len == 2 && !strncmp("..", name, len))) {
		iput(idir);
		return -EEXIST;
	}

	dentry = malloc(sizeof(struct dentry));
	if (!dentry) {
		iput(idir);
		return -ENOMEM;
	}

	dentry->d_name.name = calloc(1, len + 1);
	if (!dentry->d_name.name) {
		free(dentry);
		iput(idir);
		return -ENOMEM;
	}

	memcpy(dentry->d_name.name, name, len);
	dentry->d_name.len = len;

	/* Check if entry already exists */
	inode_lock(idir);
	dtemp = idir->i_op->lookup(idir, dentry, 0);

	if (dtemp != NULL && PTR_ERR(dtemp) != -ENAMETOOLONG) {

		ret = -EEXIST;
		inode = d_inode(dtemp);

		iput(inode);
		free(dentry->d_name.name);
		free(dentry);
		inode_unlock(idir);
		iput(idir);
		return ret;
	}

	switch (mode & S_IFMT) {
		case S_IFREG:
			ret = idir->i_op->create(idir, dentry, mode, 0);
			break;
		case S_IFDIR:
			ret = idir->i_op->mkdir(idir, dentry, mode);
			break;
		case S_IFCHR:
			ret = idir->i_op->mknod(idir, dentry, mode, dev->port);
		//	if (!ret)
		//		dev_find_oid(p->devs, dev, d_inode(dentry)->i_ino, 1);
			break;
		case S_IFLNK:
			/* empty target check */
			if (dentry->d_name.len >= (len - 1) || !strlen(name + dentry->d_name.len + 1)) {
				ret = -ENOENT;
				break;
			}
			ret = idir->i_op->symlink(idir, dentry, name + dentry->d_name.len + 1);
			break;
		default:
			ret = -EINVAL;
			break;
	}
	inode_unlock(idir);
	iput(idir);

	if (!ret) {
		*resId = d_inode(dentry)->i_ino;
		iput(d_inode(dentry));
	}

	free(dentry->d_name.name);
	free(dentry);
	return ret;
}


static int jffs2_srv_readdir(jffs2_partition_t *p, id_t *id, offs_t offs, struct dirent *dent, unsigned int size)
{
	struct inode *inode;
	struct file file;
	struct dir_context ctx = {dir_print, offs, dent, -1};

	if (!id)
		return -EINVAL;

	inode = jffs2_iget(p->sb, *id);

	if (IS_ERR(inode))
		return -EINVAL;

	inode_lock_shared(inode);
	if (!(S_ISDIR(inode->i_mode)) && !(S_ISMNT(inode->i_mode))) {
		inode_unlock_shared(inode);
		iput(inode);
		return -ENOTDIR;
	}

	mutex_lock(&JFFS2_INODE_INFO(inode)->sem);
	file.f_pino = JFFS2_INODE_INFO(inode)->inocache->pino_nlink;
	file.f_inode = inode;
	mutex_unlock(&JFFS2_INODE_INFO(inode)->sem);

	inode->i_fop->iterate_shared(&file, &ctx);
	inode_unlock_shared(inode);

	iput(inode);

	dent->d_reclen = ctx.pos - offs;

	return dent->d_reclen;
}


static int jffs2_srv_open(jffs2_partition_t *p, id_t *id)
{
	struct inode *inode;

	if (id)
		inode = jffs2_iget(p->sb, (unsigned long)*id);

	if (IS_ERR(inode))
		return PTR_ERR(inode);
	return EOK;
}


static int jffs2_srv_close(jffs2_partition_t *p, id_t *id)
{
	struct inode *inode;

	if(!id)
		return -EINVAL;

	inode = ilookup(p->sb, (unsigned long)*id);

	if (inode != NULL) {
		iput(inode);
		iput(inode);
		return EOK;
	}

	return -EINVAL;
}


static int jffs2_srv_read(jffs2_partition_t *p, id_t *id, offs_t offs, void *data, unsigned long len, int *err)
{
	struct inode *inode;
	struct jffs2_inode_info *f;
	struct jffs2_sb_info *c;
	int ret = 0;

	*err = EOK;

	if (!id)
		return -EINVAL;

	inode = jffs2_iget(p->sb, (unsigned long)*id);

	if (IS_ERR(inode))
		return -EINVAL;

	inode_lock_shared(inode);

	if(S_ISDIR(inode->i_mode) || S_ISMNT(inode->i_mode)) {
		ret = jffs2_srv_readdir(p, id, offs, data, len);
	}
/*	else if (S_ISMNT(inode->i_mode)){
		memcpy(data, &inode->i_mnt, sizeof(oid_t));
		ret = sizeof(oid_t);
	} */
	else if (S_ISCHR(inode->i_mode)) {
		memcpy(data, &inode->i_dev, sizeof(oid_t));
		ret = sizeof(oid_t);
	}
	else if (S_ISLNK(inode->i_mode)) {
		ret = strlen(inode->i_link);

		if (len < ret)
			ret = len;

		memcpy(data, inode->i_link, ret);
		inode_unlock_shared(inode);
		iput(inode);
		return ret;
	}
	else if (S_ISREG(inode->i_mode)) {

		f = JFFS2_INODE_INFO(inode);
		c = JFFS2_SB_INFO(inode->i_sb);

		if (inode->i_size < offs) {
			inode_unlock_shared(inode);
			iput(inode);
			return 0;
		}

		mutex_lock(&f->sem);
		ret = jffs2_read_inode_range(c, f, data, offs, len);
		mutex_unlock(&f->sem);
	}

	if (!ret) {
		ret = len > inode->i_size - offs ? inode->i_size - offs : len;
	}
	else if (ret < 0) {
		*err = ret;
		ret = 0;
	}

	inode_unlock_shared(inode);
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

static int jffs2_srv_write(jffs2_partition_t *p, id_t *id, offs_t offs, void *data, unsigned long len, int *err)
{
	struct inode *inode;
	struct jffs2_inode_info *f;
	struct jffs2_sb_info *c;
	struct jffs2_raw_inode *ri;
	uint32_t writelen = 0;
	int ret;

	c = JFFS2_SB_INFO(p->sb);
	if (jffs2_is_readonly(c))
		return -EROFS;

	if (!id)
		return -EINVAL;

	inode = jffs2_iget(p->sb, *id);

	if (IS_ERR(inode)) {
		return -EINVAL;
	}

	inode_lock(inode);

	if(S_ISDIR(inode->i_mode) || S_ISMNT(inode->i_mode)) {
		inode_unlock(inode);
		iput(inode);
		return -EISDIR;
	} else if (S_ISCHR(inode->i_mode)) {
		printf("jffs2: Used wrong oid to write to device\n");
		inode_unlock(inode);
		iput(inode);
		return -EINVAL;
	} else if (S_ISLNK(inode->i_mode)) {
		inode_unlock(inode);
		iput(inode);
		return -EINVAL;
	}

	ri = jffs2_alloc_raw_inode();

	if (ri == NULL) {
		inode_unlock(inode);
		iput(inode);
		return -ENOMEM;
	}

	if ((ret = jffs2_srv_prepare_write(inode, offs, len))) {
		jffs2_free_raw_inode(ri);
		inode_unlock(inode);
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
		*err = EOK;
		ret = writelen;

		if (offs + writelen > inode->i_size) {
			inode->i_size = offs + writelen;
			inode->i_blocks = (inode->i_size + 511) >> 9;
			inode->i_ctime = inode->i_mtime = ITIME(je32_to_cpu(ri->ctime));
		}
	}
	else {
		*err = ret;
		ret = 0;
	}

	jffs2_free_raw_inode(ri);

	inode_unlock(inode);
	iput(inode);
	return ret;
}


int jffs2lib_message_handler(void *partition, msg_t *msg)
{
	int err = EOK;
	jffs2_partition_t *p = partition;

	switch (msg->type) {
	case mtOpen:
		err = jffs2_srv_open(p, &msg->object);
		if (!err)
			msg->o.open = msg->object;
		break;

	case mtClose:
		err = jffs2_srv_close(p, &msg->object);
		break;

	case mtRead:
		msg->o.io = jffs2_srv_read(p, &msg->object, msg->i.io.offs, msg->o.data, msg->o.size, &err);
		break;

	case mtWrite:
		msg->o.io = jffs2_srv_write(p, &msg->object, msg->i.io.offs, msg->i.data, msg->i.size, &err);
		break;

	case mtSetAttr:
		err = jffs2_srv_setattr(p, &msg->object, msg->i.attr, msg->i.data, msg->i.size);
		break;

	case mtGetAttr:
		err = jffs2_srv_getattr(p, &msg->object, msg->i.attr, msg->o.data, msg->o.size);
		break;

	case mtLookup:
		err = jffs2_srv_lookup(p, &msg->object, msg->i.data, msg->i.size, &msg->o.lookup.id, &msg->o.lookup.mode);
		if ((err == -ENOENT) && (msg->i.lookup.flags & O_CREAT)) {
			err = jffs2_srv_create(p, &msg->object, msg->i.data, msg->i.size, &msg->o.lookup.id, msg->i.lookup.mode, &msg->i.lookup.dev);
			if (!err)
				msg->o.lookup.mode = msg->i.lookup.mode;
			if (!err)
				jffs2_srv_open(p, &msg->o.lookup.id);
		}
		break;

	case mtLink:
		err = jffs2_srv_link(p, &msg->object, msg->i.data, msg->i.size, &msg->i.link.id);
		break;

	case mtUnlink:
		err = jffs2_srv_unlink(p, &msg->object, msg->i.data, msg->i.size);
		break;

	case (mtSync & 0xff):
		p->sb->s_op->sync_fs(p->sb, 0);
		break;
	}

	return err;
}


void *jffs2lib_create_partition(size_t start, size_t end, unsigned mode, unsigned port, long *rootid)
{
	jffs2_partition_t *p;

	if (jffs2_common.fs == NULL) {
		init_jffs2_fs();
		beginthread(delayed_work_starter, 4, malloc(0x2000), 0x2000, system_long_wq);
	}

	if ((p = malloc(sizeof(jffs2_partition_t))) != NULL) {
		p->start = start;
		p->size = end - start;
		p->flags = mode;
		*rootid = 1;
		p->port = port;
	}

	return p;
}


int jffs2lib_mount_partition(void *partition)
{
	jffs2_partition_t *p = (jffs2_partition_t *)partition;

	object_init(p);
	dev_init(&p->devs);

	if (jffs2_common.fs->mount(jffs2_common.fs, 0, "jffs2", p) == NULL)
		return -EIO;

	return EOK;
}