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


struct inode *jffs2_srv_get(jffs2_partition_t *p, oid_t *oid)
{
	jffs2_dev_t *dev;

	if (jffs2_is_device(p, oid)) {
		dev = dev_find_oid(p->devs, oid, 0, 0);
		return jffs2_iget(p->sb, dev->ino);
	}

	return jffs2_iget(p->sb, oid->id);
}


static int jffs2_srv_lookup(jffs2_partition_t *p, oid_t *dir, const char *name, oid_t *res, oid_t *dev, char *lnk, int lnksz)
{
	struct dentry *dentry, *dtemp;
	struct inode *inode = NULL;
	int len = 0;
	char *end;

	if (dir->id == 0)
		dir->id = 1;

	if (jffs2_is_device(p, dir)) {
		TRACE("is device");
		return -EINVAL;
	}

	res->id = 0;

	inode = jffs2_iget(p->sb, dir->id);

	if (IS_ERR(inode)) {
		TRACE("inode is_err");
		return -EINVAL;
	}

	if (!S_ISDIR(inode->i_mode)) {
		iput(inode);
		TRACE("notdir");
		return -ENOTDIR;
	}

	dentry = malloc(sizeof(struct dentry));
	res->port = p->port;

	while (name[len] != '\0') {
		while (name[len] == '/') {
			len++;
			continue;
		}
		/* check again for path ending */
		if (name[len] == '\0')
			break;

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
			inode = jffs2_iget(p->sb, res->id);
			continue;
		}

		dentry->d_name.len = strlen(dentry->d_name.name);

		if (S_ISDIR(inode->i_mode)) {
			if (dev_find_ino(p->devs, inode->i_ino) == NULL) {
				dtemp = inode->i_op->lookup(inode, dentry, 0);
			} else {
				res->id = inode->i_ino;
				res->port = p->port;
				free(dentry->d_name.name);
				len--;
				break;
			}
		} else if (S_ISLNK(inode->i_mode)) {
			res->id = inode->i_ino;
			res->port = p->port;
			free(dentry->d_name.name);
			break;
		} else {
			free(dentry->d_name.name);
			free(dentry);
			iput(inode);
			return -ENOTDIR;
		}

		if (dtemp == NULL || PTR_ERR(dtemp) == -ENAMETOOLONG) {
			free(dentry->d_name.name);
			free(dentry);
			iput(inode);
			return dtemp ? -ENAMETOOLONG : -ENOENT;
		} else
			res->id = dtemp->d_inode->i_ino;

		len += dentry->d_name.len;
		free(dentry->d_name.name);
		dentry->d_name.len = 0;

		iput(inode);
		inode = d_inode(dtemp);
	}

	if (dev_find_ino(p->devs, res->id) != NULL)
		memcpy(dev, &(dev_find_ino(p->devs, res->id)->dev), sizeof(oid_t));
	else {
		if (S_ISCHR(inode->i_mode))
			len = -ENOENT;
		else
			memcpy(dev, res, sizeof(oid_t));
	}

	if (lnk != NULL && S_ISLNK(inode->i_mode)) {
		if (strlen(inode->i_link) < lnksz)
			lnksz = strlen(inode->i_link);
		strncpy(lnk, inode->i_link, lnksz);
	}

	free(dentry);
	iput(inode);

	if (res->port == p->port && !res->id)
		return -ENOENT;

	return len;
}


static int jffs2_srv_setattr(jffs2_partition_t *p, oid_t *oid, int type, int attr, void *data, ssize_t size)
{
	struct iattr iattr;
	struct inode *inode;
	struct jffs2_inode_info *f;
	struct dentry dentry;
	int ret;
	struct jffs2_sb_info *c = JFFS2_SB_INFO(p->sb);

	if (!oid->id)
		return -EINVAL;

	if (type != atDev && jffs2_is_readonly(c))
		return -EROFS;

	inode = jffs2_iget(p->sb, oid->id);
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

		case (atDev):
			if (data != NULL && size == sizeof(oid_t))
				dev_find_oid(p->devs, data, inode->i_ino, 1);
			mutex_unlock(&f->sem);
			iput(inode);
			return 0;
	}

	d_instantiate(&dentry, inode);

	mutex_unlock(&f->sem);

	ret = inode->i_op->setattr(&dentry, &iattr);
	iput(inode);

	return ret;
}


static int jffs2_srv_getattr(jffs2_partition_t *p, oid_t *oid, int type, int *attr)
{
	struct inode *inode;
	struct jffs2_inode_info *f;

	if (!oid->id)
		return -EINVAL;

	if (attr == NULL)
		return -EINVAL;

	inode = jffs2_iget(p->sb, oid->id);

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
		case (atPollStatus):
			// trivial implementation: assume read/write is always possible
			*attr = POLLIN|POLLRDNORM|POLLOUT|POLLWRNORM;
			break;
	}

	mutex_unlock(&f->sem);
	iput(inode);

	return EOK;
}


static int jffs2_srv_link(jffs2_partition_t *p, oid_t *dir, const char *name, oid_t *oid)
{
	struct inode *idir, *inode, *ivictim = NULL;
	struct jffs2_inode_info *victim_f = NULL;
	struct dentry *old, *new;
	oid_t toid, t;
	int ret;
	struct jffs2_sb_info *c = JFFS2_SB_INFO(p->sb);

	if (jffs2_is_readonly(c))
		return -EROFS;

	if (!dir->id || !oid->id)
		return -EINVAL;

	if (name == NULL || !strlen(name))
		return -EINVAL;

	if (jffs2_is_device(p, dir))
		return -EINVAL;

	idir = jffs2_iget(p->sb, dir->id);

	if (IS_ERR(idir))
		return -ENOENT;

	if (!S_ISDIR(idir->i_mode)) {
		iput(idir);
		return -EINVAL;
	}

	if (jffs2_srv_lookup(p, dir, name, &t, &toid, NULL, 0) > 0) {
		ivictim = jffs2_srv_get(p, &toid);

		if (S_ISDIR(ivictim->i_mode) || (toid.id == oid->id)) {
			iput(ivictim);
			iput(idir);
			return -EEXIST;
		}
	}

	inode = jffs2_srv_get(p, oid);

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
		dev_inc(p->devs, oid);

	iput(idir);
	iput(inode);

	if (!ret && ivictim != NULL) {
		victim_f = JFFS2_INODE_INFO(ivictim);
		mutex_lock(&victim_f->sem);
		if (victim_f->inocache->pino_nlink)
			victim_f->inocache->pino_nlink--;
		mutex_unlock(&victim_f->sem);
		drop_nlink(ivictim);
		iput(ivictim);
	}

	free(old);
	free(new->d_name.name);
	free(new);

	return ret;
}


static int jffs2_srv_unlink(jffs2_partition_t *p, oid_t *dir, const char *name)
{
	struct inode *idir, *inode;
	struct dentry *dentry;
	oid_t oid, t;
	int ret;
	struct jffs2_sb_info *c = JFFS2_SB_INFO(p->sb);

	if (jffs2_is_readonly(c))
		return -EROFS;

	if (!dir->id)
		return -EINVAL;

	if (name == NULL || !strlen(name))
		return -EINVAL;

	idir = jffs2_iget(p->sb, dir->id);

	if (IS_ERR(idir))
		return -ENOENT;

	if (jffs2_srv_lookup(p, dir, name, &t, &oid, NULL, 0) <= 0) {
		iput(idir);
		return -ENOENT;
	}

	inode = jffs2_srv_get(p, &oid);

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

	dentry->d_name.name = strdup(name);
	dentry->d_name.len = strlen(name);

	d_instantiate(dentry, inode);

	if (S_ISDIR(inode->i_mode))
		ret = idir->i_op->rmdir(idir, dentry);
	else
		ret = idir->i_op->unlink(idir, dentry);

	if (!ret && S_ISCHR(inode->i_mode))
		dev_dec(p->devs, &oid);

	iput(idir);
	iput(inode);

	free(dentry->d_name.name);
	free(dentry);

	return ret;
}


static int jffs2_srv_create(jffs2_partition_t *p, oid_t *dir, const char *name, size_t namelen, oid_t *oid, int type, int mode, oid_t *dev)
{
	struct inode *idir, *inode;
	struct dentry *dentry, *dtemp;
	int ret = 0;
	struct jffs2_sb_info *c = JFFS2_SB_INFO(p->sb);

	if (jffs2_is_readonly(c))
		return -EROFS;

	if (name == NULL || !strlen(name))
		return -EINVAL;

	if (jffs2_is_device(p, dir))
		return -EINVAL;

	idir = jffs2_iget(p->sb, dir->id);

	if (IS_ERR(idir))
		return -ENOENT;

	if (!S_ISDIR(idir->i_mode)) {
		iput(idir);
		return -ENOTDIR;
	}

	if (!strcmp(".", name) || !strcmp("..", name)) {
		iput(idir);
		return -EEXIST;
	}

	dentry = malloc(sizeof(struct dentry));
	memset(dentry, 0, sizeof(struct dentry));
	dentry->d_name.name = strdup(name);
	dentry->d_name.len = strlen(name);

	/* Check if entry already exists */
	dtemp = idir->i_op->lookup(idir, dentry, 0);

	if (dtemp != NULL && PTR_ERR(dtemp) != -ENAMETOOLONG) {

		ret = -EEXIST;
		inode = d_inode(dtemp);

		/* Entry exists so we need to check if it is dangling device entry */
		if(S_ISCHR(inode->i_mode) && dev_find_ino(p->devs, inode->i_ino) == NULL) {
			/* Now we check if we can reuse this entry. If we want to create device,
			 * entry can be used again. Otherwise we continue, entry will be obsoleted */
			if (type == otDev) {
				ret = EOK;
				dev_find_oid(p->devs, dev, inode->i_ino, 1);
				oid->id = inode->i_ino;
				iput(inode);
				free(dentry->d_name.name);
				free(dentry);
				iput(idir);
				return ret;
			}
			iput(inode);

		} else {
			iput(inode);
			free(dentry->d_name.name);
			free(dentry);
			iput(idir);
			return ret;
		}
	}

	oid->port = p->port;
	//mutexLock(idir->i_lock);

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
			if (!ret)
				dev_find_oid(p->devs, dev, d_inode(dentry)->i_ino, 1);
			break;
		case otSymlink:
			/* empty target check */
			if (dentry->d_name.len >= (namelen - 1) || !strlen(name + dentry->d_name.len + 1)) {
				ret = -ENOENT;
				break;
			}
			mode = S_IFLNK | S_IRWXU | S_IRWXG | S_IRWXO;
			ret = idir->i_op->symlink(idir, dentry, name + dentry->d_name.len + 1);
			break;
		default:
			ret = -EINVAL;
			break;
	}

	//mutexUnlock(idir->i_lock);
	iput(idir);

	if (!ret) {
		oid->id = d_inode(dentry)->i_ino;
		iput(d_inode(dentry));
	}

	free(dentry->d_name.name);
	free(dentry);
	return ret;
}


static int jffs2_srv_destroy(oid_t *oid)
{
	return 0;
}


static int jffs2_srv_readdir(jffs2_partition_t *p, oid_t *dir, offs_t offs, struct dirent *dent, unsigned int size)
{
	struct inode *inode;
	struct file file;
	struct dir_context ctx = {dir_print, offs, dent, -1, p->devs};

	if (!dir->id)
		return -EINVAL;

	inode = jffs2_iget(p->sb, dir->id);

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


static int jffs2_srv_open(jffs2_partition_t *p, oid_t *oid)
{
	if (oid->id)
		jffs2_iget(p->sb, oid->id);

	return EOK;
}


static int jffs2_srv_close(jffs2_partition_t *p, oid_t *oid)
{
	if(!oid->id)
		return -EINVAL;

	struct inode *inode = ilookup(p->sb, oid->id);

	if (inode != NULL) {
		iput(inode);
		iput(inode);
	}

	return EOK;
}


static int jffs2_srv_read(jffs2_partition_t *p, oid_t *oid, offs_t offs, void *data, unsigned long len)
{
	struct inode *inode;
	struct jffs2_inode_info *f;
	struct jffs2_sb_info *c;
	int ret;

	if (!oid->id)
		return -EINVAL;

	inode = jffs2_iget(p->sb, oid->id);

	if (IS_ERR(inode))
		return -EINVAL;

	if(S_ISDIR(inode->i_mode)) {
		iput(inode);
		return -EISDIR;
	} else if (S_ISCHR(inode->i_mode)) {
		printf("jffs2: Used wrong oid to read from device\n");
		iput(inode);
		return -EINVAL;
	} else if (S_ISLNK(inode->i_mode)) {
		ret = strlen(inode->i_link);

		if (len < ret)
			ret = len;

		memcpy(data, inode->i_link, ret);
		iput(inode);
		return ret;
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

static int jffs2_srv_write(jffs2_partition_t *p, oid_t *oid, offs_t offs, void *data, unsigned long len)
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

	if (!oid->id)
		return -EINVAL;

	inode = jffs2_iget(p->sb, oid->id);

	if (IS_ERR(inode)) {
		return -EINVAL;
	}

	if(S_ISDIR(inode->i_mode)) {
		iput(inode);
		return -EISDIR;
	} else if (S_ISCHR(inode->i_mode)) {
		printf("jffs2: Used wrong oid to write to device\n");
		iput(inode);
		return -EINVAL;
	} else if (S_ISLNK(inode->i_mode)) {
		iput(inode);
		return -EINVAL;
	}

	ri = jffs2_alloc_raw_inode();

	if (ri == NULL) {
		iput(inode);
		return -ENOMEM;
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
	}

	jffs2_free_raw_inode(ri);
	iput(inode);
	return ret ? ret : writelen;
}


static int jffs2_srv_truncate(jffs2_partition_t *p, oid_t *oid, unsigned long len)
{
	struct jffs2_sb_info *c = JFFS2_SB_INFO(p->sb);
	if (jffs2_is_readonly(c))
		return -EROFS;

	return jffs2_srv_setattr(p, oid, atSize, len, NULL, 0);
}


int jffs2lib_message_handler(void *partition, msg_t *msg)
{
	jffs2_partition_t *p = partition;

	switch (msg->type) {
	case mtOpen:
		msg->o.io.err = jffs2_srv_open(p, &msg->i.openclose.oid);
		break;

	case mtClose:
		msg->o.io.err = jffs2_srv_close(p, &msg->i.openclose.oid);
		break;

	case mtRead:
		msg->o.io.err = jffs2_srv_read(p, &msg->i.io.oid, msg->i.io.offs, msg->o.data, msg->o.size);
		break;

	case mtWrite:
		msg->o.io.err = jffs2_srv_write(p, &msg->i.io.oid, msg->i.io.offs, msg->i.data, msg->i.size);
		break;

	case mtTruncate:
		msg->o.io.err = jffs2_srv_truncate(p, &msg->i.io.oid, msg->i.io.len);
		break;

	case mtDevCtl:
		msg->o.io.err = -EINVAL;
		break;

	case mtCreate:
		msg->o.create.err = jffs2_srv_create(p, &msg->i.create.dir, msg->i.data, msg->i.size, &msg->o.create.oid, msg->i.create.type, msg->i.create.mode, &msg->i.create.dev);
		break;

	case mtDestroy:
		msg->o.io.err = jffs2_srv_destroy(&msg->i.destroy.oid);
		break;

	case mtSetAttr:
		msg->o.attr.val = jffs2_srv_setattr(p, &msg->i.attr.oid, msg->i.attr.type, msg->i.attr.val, msg->i.data, msg->i.size);
		break;

	case mtGetAttr:
		jffs2_srv_getattr(p, &msg->i.attr.oid, msg->i.attr.type, &msg->o.attr.val);
		break;

	case mtLookup:
		msg->o.lookup.err = jffs2_srv_lookup(p, &msg->i.lookup.dir, msg->i.data, &msg->o.lookup.fil, &msg->o.lookup.dev, msg->o.data, msg->o.size);
		break;

	case mtLink:
		msg->o.io.err = jffs2_srv_link(p, &msg->i.ln.dir, msg->i.data, &msg->i.ln.oid);
		break;

	case mtUnlink:
		msg->o.io.err = jffs2_srv_unlink(p, &msg->i.ln.dir, msg->i.data);
		break;

	case mtReaddir:
		msg->o.io.err = jffs2_srv_readdir(p, &msg->i.readdir.dir, msg->i.readdir.offs,
				msg->o.data, msg->o.size);
		break;

	case mtSync:
		p->sb->s_op->sync_fs(p->sb, 0);
		break;
	}

	return EOK;
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






#if 0
int main(int argc, char **argv)
{
	oid_t toid = { 0 };
	int i = 0, c, pest, argn;

	while(write(0, "", 1) < 0)
		usleep(500000);

	memset(&jffs2_common, 0, sizeof(jffs2_common));

	printf("jffs2: Starting jffs2 server\n");

	if (init_jffs2_fs() != EOK) {
		printf("jffs2: Error initialising jffs2\n");
		return -1;
	}

	if ((pest = argc / 5) == 0) {
		jffs2_help();
		return -1;
	}

	jffs2_common.partition = malloc(pest * sizeof(jffs2_partition_t));

	while ((c = getopt(argc, argv, "r:p:h")) != -1) {

		switch (c) {
			case 'r':
				if (jffs2_common.partition != NULL) {
					argn = optind - 1;
					if (argn + 3 <= argc) {
						jffs2_common.partition[0].start = atoi(argv[argn++]);
						jffs2_common.partition[0].size = atoi(argv[argn++]);
						jffs2_common.partition[0].flags = atoi(argv[argn++]);
						jffs2_common.partition[0].mountpt = "/";
						jffs2_common.partition[0].root = 1;
						if (argn < argc) {
							if (argv[argn][0] == '-')
								optind += 2;
							else
								optind += 3;
						} else
							optind += 2;

						jffs2_common.partition_cnt++;
					}
				}
				break;

			case 'p':
				if (jffs2_common.partition != NULL) {
					if (jffs2_common.partition_cnt == pest) {
						pest++;
						jffs2_common.partition = realloc(jffs2_common.partition, pest * sizeof(jffs2_partition_t));
					}
					argn = optind - 1;
					if (argn + 3 < argc) {
						jffs2_common.partition[jffs2_common.partition_cnt].start = atoi(argv[argn++]);
						jffs2_common.partition[jffs2_common.partition_cnt].size = atoi(argv[argn++]);
						jffs2_common.partition[jffs2_common.partition_cnt].flags = atoi(argv[argn++]);
						jffs2_common.partition[jffs2_common.partition_cnt].mountpt = argv[argn];
						optind += 3;
						jffs2_common.partition[jffs2_common.partition_cnt].root = 0;
						jffs2_common.partition_cnt++;
					}
				}
				break;

			case 'h':
			default:
				jffs2_help();
				return 0;
		}
	}

	if (jffs2_common.partition_cnt == 0) {
		jffs2_help();
		return 0;
	}

	if (jffs2_common.partition[i].root) {

		jffs2_mount_partition(&jffs2_common.partition[i]);
		if (lookup("/", NULL, &toid) < 0) {
			printf("jffs2: Rootfs failed to mount\n");
			return -1;
		}

		jffs2_common.partition[i].stack = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, 0, OID_NULL, 0);
		if (jffs2_common.partition[i].stack == NULL) {
			printf("jffs2: Failed to allocate stack for jffs2. Partition won't be mounted at %s\n", jffs2_common.partition[i].mountpt);
			return -1;
		}

		beginthread(jffs2_run, 3, jffs2_common.partition[i].stack, 0x1000, &jffs2_common.partition[i]);
		i++;
	}

	if (lookup("/", NULL, &toid) < 0) {
		printf("jffs2: No rootfs found\n");
		return -1;
	}

	for (; i < jffs2_common.partition_cnt; i++) {
		jffs2_common.partition[i].stack = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, 0, OID_NULL, 0);
		if (jffs2_common.partition[i].stack == NULL) {
			printf("jffs2: Failed to allocate stack for jffs2. Partition won't be mounted at %s\n", jffs2_common.partition[i].mountpt);
			continue;
		}

		beginthread(jffs2_mount_partition, 3, jffs2_common.partition[i].stack, 0x1000, &jffs2_common.partition[i]);
	}

	delayed_work_starter(system_long_wq);

	return EOK;
}
#endif
