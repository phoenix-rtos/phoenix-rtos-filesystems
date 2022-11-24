/*
 * Phoenix-RTOS
 *
 * jffs2 library
 *
 * Copyright 2012, 2016, 2018, 2022 Phoenix Systems
 * Copyright 2007 Pawel Pisarczyk
 * Author: Kamil Amanowicz, Hubert Buczynski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <poll.h>
#include <sys/statvfs.h>

#include "phoenix-rtos.h"
#include "nodelist.h"
#include "include/libjffs2.h"

#define TRACE(x, ...) printf("jffs trace: " x "\n", ##__VA_ARGS__)


jffs2_common_t jffs2_common;


static inline int libjffs2_isDevice(jffs2_partition_t *p, oid_t *oid)
{
	return p->port != oid->port;
}


struct inode *libjffs2_inodeGet(jffs2_partition_t *p, oid_t *oid)
{
	jffs2_dev_t *dev;

	if (libjffs2_isDevice(p, oid)) {
		dev = dev_find_oid(p->devs, oid, 0, 0);
		return jffs2_iget(p->sb, dev->ino);
	}

	return jffs2_iget(p->sb, oid->id);
}


static int libjffs2_lookup(void *info, oid_t *dir, const char *name, oid_t *res, oid_t *dev, char *lnk, int lnksz)
{
	struct dentry *dentry, *dtemp;
	struct inode *inode = NULL;
	int len = 0;
	char *end;
	jffs2_partition_t *p = (jffs2_partition_t *)info;

	if (info == NULL) {
		return -EINVAL;
	}

	if (dir->id == 0) {
		dir->id = 1;
	}

	if (libjffs2_isDevice(p, dir)) {
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
				inode_lock_shared(inode);
				dtemp = inode->i_op->lookup(inode, dentry, 0);
				inode_unlock_shared(inode);
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
	else
		memcpy(dev, res, sizeof(oid_t));

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


static int libjffs2_setattr(void *info, oid_t *oid, int type, long long attr, void *data, size_t size)
{
	struct iattr iattr;
	struct inode *inode;
	struct dentry dentry;
	int ret = EOK;
	struct jffs2_sb_info *c;
	jffs2_partition_t *p = (jffs2_partition_t *)info;
	int done = 0;
	oid_t *dev;

	if (info == NULL) {
		return -EINVAL;
	}

	c = JFFS2_SB_INFO(p->sb);

	if (!oid->id)
		return -EINVAL;

	if (type != atDev && jffs2_is_readonly(c))
		return -EROFS;

	inode = jffs2_iget(p->sb, oid->id);
	if (IS_ERR(inode))
		return -ENOENT;

	inode_lock(inode);
	switch (type) {

		case (atMode): /* mode */
			iattr.ia_valid = ATTR_MODE;
			iattr.ia_mode = (inode->i_mode & ~ALLPERMS) | (attr & ALLPERMS);
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
			if (data != NULL && size == sizeof(oid_t)) {
				dev = (oid_t *)data;
				/* Detach device */
				if ((dev->port == oid->port) && (dev->id == oid->id)) {
					dev_destroy(p->devs, dev_find_ino(p->devs, inode->i_ino));
				}
				/* Attach device */
				else if (dev_find_oid(p->devs, dev, inode->i_ino, 1) == NULL) {
					ret = -ENOMEM;
				}
			}
			else {
				ret = -EINVAL;
			}
			done = 1;
			break;

		case (atMTime):
			iattr.ia_valid = ATTR_MTIME;
			iattr.ia_mtime.tv_sec = attr;
			iattr.ia_mtime.tv_nsec = 0;
			break;

		case (atATime):
			iattr.ia_valid = ATTR_ATIME;
			iattr.ia_atime.tv_sec = attr;
			iattr.ia_atime.tv_nsec = 0;
			break;
		default:
			/* unknown / invalid attribute to set */
			ret = -EINVAL;
			done = 1;
			break;
	}

	if (done == 0) {
		d_instantiate(&dentry, inode);
		ret = inode->i_op->setattr(&dentry, &iattr);
	}

	inode_unlock(inode);
	iput(inode);

	return ret;
}


static int libjffs2_getattr(void *info, oid_t *oid, int type, long long *attr)
{
	struct inode *inode;
	jffs2_partition_t *p = (jffs2_partition_t *)info;
	struct jffs2_sb_info *c;
	int ret = EOK;

	if (info == NULL) {
		return -EINVAL;
	}

	c = JFFS2_SB_INFO(p->sb);

	if (!oid->id)
		return -EINVAL;

	if (attr == NULL)
		return -EINVAL;

	inode = jffs2_iget(p->sb, oid->id);

	if (IS_ERR(inode))
		return -ENOENT;

	inode_lock_shared(inode);
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
		case (atBlocks):
			*attr = inode->i_blocks;
			break;

		case (atIOBlock):
			*attr = c->mtd->writesize;
			break;

		case (atType): /* type */
			if (S_ISDIR(inode->i_mode))
				*attr = otDir;
			else if (S_ISREG(inode->i_mode))
				*attr = otFile;
			else if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode) || S_ISFIFO(inode->i_mode))
				*attr = otDev;
			else if (S_ISLNK(inode->i_mode))
				*attr = otSymlink;
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

		default:
			ret = -EINVAL;
			break;
	}

	inode_unlock_shared(inode);
	iput(inode);

	return ret;
}


static int libjffs2_link(void *info, oid_t *dir, const char *name, oid_t *oid)
{
	struct inode *idir, *inode, *ivictim = NULL;
	struct jffs2_inode_info *victim_f = NULL;
	struct dentry *old, *new;
	oid_t toid, t;
	int ret;
	struct jffs2_sb_info *c;
	jffs2_partition_t *p = (jffs2_partition_t *)info;

	if (info == NULL) {
		return -EINVAL;
	}

	c = JFFS2_SB_INFO(p->sb);

	if (jffs2_is_readonly(c))
		return -EROFS;

	if (!dir->id || !oid->id)
		return -EINVAL;

	if (name == NULL || (*name == '\0'))
		return -EINVAL;

	if (libjffs2_isDevice(p, dir))
		return -EINVAL;

	idir = jffs2_iget(p->sb, dir->id);

	if (IS_ERR(idir))
		return -ENOENT;

	if (!S_ISDIR(idir->i_mode)) {
		iput(idir);
		return -EINVAL;
	}

	if (libjffs2_lookup(p, dir, name, &t, &toid, NULL, 0) > 0) {
		ivictim = libjffs2_inodeGet(p, &toid);

		if (S_ISDIR(ivictim->i_mode) || (toid.id == oid->id)) {
			iput(ivictim);
			iput(idir);
			return -EEXIST;
		}
	}

	inode = libjffs2_inodeGet(p, oid);

	if (IS_ERR(inode)) {
		iput(idir);
		return -ENOENT;
	}

	old = malloc(sizeof(struct dentry));
	new = malloc(sizeof(struct dentry));

	new->d_name.name = strdup(name);
	new->d_name.len = strlen(name);

	d_instantiate(old, inode);

	inode_lock(idir);
	ret = idir->i_op->link(old, idir, new);
	inode_unlock(idir);

	if (!ret) {
		/* cancel i_count increment done by link() */
		iput(inode);
	}

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


static int libjffs2_unlink(void *info, oid_t *dir, const char *name)
{
	struct inode *idir, *inode;
	struct dentry *dentry;
	oid_t oid, t;
	int ret;
	struct jffs2_sb_info *c;
	jffs2_partition_t *p = (jffs2_partition_t *)info;

	if (info == NULL) {
		return -EINVAL;
	}

	c = JFFS2_SB_INFO(p->sb);

	if (jffs2_is_readonly(c))
		return -EROFS;

	if (!dir->id)
		return -EINVAL;

	if (name == NULL || !strlen(name))
		return -EINVAL;

	idir = jffs2_iget(p->sb, dir->id);

	if (IS_ERR(idir))
		return -ENOENT;

	if (libjffs2_lookup(p, dir, name, &t, &oid, NULL, 0) <= 0) {
		iput(idir);
		return -ENOENT;
	}

	inode = libjffs2_inodeGet(p, &oid);

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

	inode_lock(idir);
	if (S_ISDIR(inode->i_mode))
		ret = idir->i_op->rmdir(idir, dentry);
	else
		ret = idir->i_op->unlink(idir, dentry);
	inode_unlock(idir);

	iput(idir);
	iput(inode);

	free(dentry->d_name.name);
	free(dentry);

	return ret;
}


static int libjffs2_create(void *info, oid_t *dir, const char *name, oid_t *oid, unsigned mode, int type, oid_t *dev)
{
	struct inode *idir, *inode;
	struct dentry *dentry, *dtemp;
	int ret = 0;
	size_t namelen;
	struct jffs2_sb_info *c;
	jffs2_partition_t *p = (jffs2_partition_t *)info;

	if (info == NULL) {
		return -EINVAL;
	}

	if (name == NULL || (*name == '\0'))
		return -EINVAL;

	p = (jffs2_partition_t *)info;
	c = JFFS2_SB_INFO(p->sb);
	namelen = strlen(name);

	if (jffs2_is_readonly(c))
		return -EROFS;


	if (libjffs2_isDevice(p, dir))
		return -EEXIST;

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
	if (dentry == NULL) {
		iput(idir);
		return -ENOMEM;
	}

	memset(dentry, 0, sizeof(struct dentry));
	dentry->d_name.name = strdup(name);
	if (dentry->d_name.name == NULL) {
		free(dentry);
		iput(idir);
		return -ENOMEM;
	}

	dentry->d_name.len = namelen;

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

	oid->port = p->port;

	switch (type) {
		case otFile:
			if (!S_ISREG(mode)) {
				mode &= ALLPERMS;
				mode |= S_IFREG;
			}
			ret = idir->i_op->create(idir, dentry, mode, 0);
			break;
		case otDir:
			if (!S_ISDIR(mode)) {
				mode &= ALLPERMS;
				mode |= S_IFDIR;
			}
			ret = idir->i_op->mkdir(idir, dentry, mode);
			break;
		case otDev:
			if (!(S_ISCHR(mode) || S_ISBLK(mode) || S_ISFIFO(mode))) {
				mode &= ALLPERMS;
				mode |= S_IFCHR;
			}
			ret = idir->i_op->mknod(idir, dentry, mode, dev->port);
			if (!ret)
				dev_find_oid(p->devs, dev, d_inode(dentry)->i_ino, 1);
			break;
		case otSymlink:
			if (!S_ISLNK(mode)) {
				mode &= ALLPERMS;
				mode |= S_IFLNK;
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
		oid->id = d_inode(dentry)->i_ino;
		iput(d_inode(dentry));
	}

	free(dentry->d_name.name);
	free(dentry);
	return ret;
}


static int libjffs2_destroy(void *info, oid_t *oid)
{
	return 0;
}


static int libjffs2_readdir(void *info, oid_t *dir, offs_t offs, struct dirent *dent, size_t size)
{
	struct inode *inode;
	struct file file;
	struct dir_context dctx = { dir_print, offs, dent, -1 };
	jffs2_partition_t *p = (jffs2_partition_t *)info;

	if (info == NULL) {
		return -EINVAL;
	}

	if (!dir->id)
		return -EINVAL;

	inode = jffs2_iget(p->sb, dir->id);

	if (IS_ERR(inode))
		return -EINVAL;

	inode_lock_shared(inode);
	if (!(S_ISDIR(inode->i_mode))) {
		inode_unlock_shared(inode);
		iput(inode);
		return -ENOTDIR;
	}

	mutex_lock(&JFFS2_INODE_INFO(inode)->sem);
	file.f_pino = JFFS2_INODE_INFO(inode)->inocache->pino_nlink;
	file.f_inode = inode;
	mutex_unlock(&JFFS2_INODE_INFO(inode)->sem);

	inode->i_fop->iterate_shared(&file, &dctx);
	inode_unlock_shared(inode);

	iput(inode);

	dent->d_reclen = dctx.pos - offs;

	return dctx.emit;
}


static int libjffs2_open(void *info, oid_t *oid)
{
	jffs2_partition_t *p = (jffs2_partition_t *)info;

	if (info == NULL) {
		return -EINVAL;
	}

	if (oid->id)
		jffs2_iget(p->sb, oid->id);

	return EOK;
}


static int libjffs2_close(void *info, oid_t *oid)
{
	jffs2_partition_t *p = (jffs2_partition_t *)info;

	if (info == NULL) {
		return -EINVAL;
	}

	if(!oid->id)
		return -EINVAL;

	struct inode *inode = ilookup(p->sb, oid->id);

	if (inode != NULL) {
		iput(inode);
		iput(inode);
	}

	return EOK;
}


static int libjffs2_read(void *info, oid_t *oid, offs_t offs, void *data, size_t len)
{
	struct inode *inode;
	struct jffs2_inode_info *f;
	struct jffs2_sb_info *c;
	int ret;
	jffs2_partition_t *p = (jffs2_partition_t *)info;

	if (info == NULL) {
		return -EINVAL;
	}

	if (!oid->id)
		return -EINVAL;

	inode = jffs2_iget(p->sb, oid->id);

	if (IS_ERR(inode))
		return -EINVAL;

	inode_lock_shared(inode);

	if(S_ISDIR(inode->i_mode)) {
		inode_unlock_shared(inode);
		iput(inode);
		return -EISDIR;
	} else if (S_ISCHR(inode->i_mode)) {
		printf("jffs2: Used wrong oid to read from device\n");
		inode_unlock_shared(inode);
		iput(inode);
		return -EINVAL;
	} else if (S_ISLNK(inode->i_mode)) {
		ret = strlen(inode->i_link);

		if (len < ret)
			ret = len;

		memcpy(data, inode->i_link, ret);
		inode_unlock_shared(inode);
		iput(inode);
		return ret;
	}

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

	if (!ret)
		ret = len > inode->i_size - offs ? inode->i_size - offs : len;

	inode_unlock_shared(inode);
	iput(inode);
	return ret;
}


static int libjffs2_prepareWrite(struct inode *inode, loff_t offs, size_t len)
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


static int libjffs2_write(void *info, oid_t *oid, offs_t offs, const void *data, size_t len)
{
	struct inode *inode;
	struct jffs2_inode_info *f;
	struct jffs2_sb_info *c;
	struct jffs2_raw_inode *ri;
	uint32_t writelen = 0;
	jffs2_partition_t *p = (jffs2_partition_t *)info;
	int ret;

	if (info == NULL)
		return -EINVAL;

	c = JFFS2_SB_INFO(p->sb);
	if (jffs2_is_readonly(c))
		return -EROFS;

	if (!oid->id)
		return -EINVAL;

	inode = jffs2_iget(p->sb, oid->id);

	if (IS_ERR(inode)) {
		return -EINVAL;
	}

	inode_lock(inode);

	if(S_ISDIR(inode->i_mode)) {
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

	if ((ret = libjffs2_prepareWrite(inode, offs, len))) {
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
		if (offs + writelen > inode->i_size) {
			inode->i_size = offs + writelen;
			inode->i_blocks = (inode->i_size + 511) >> 9;
			inode->i_ctime = inode->i_mtime = ITIME(je32_to_cpu(ri->ctime));
		}
	}

	jffs2_free_raw_inode(ri);

	inode_unlock(inode);
	iput(inode);
	return ret ? ret : writelen;
}


static int libjffs2_truncate(void *info, oid_t *oid, size_t len)
{
	return libjffs2_setattr(info, oid, atSize, len, NULL, 0);
}


static int libjffs2_statfs(void *info, void *buf, size_t len)
{
	struct super_block *sb;
	struct jffs2_sb_info *c;
	struct statvfs *st = buf;
	fsblkcnt_t avail, resv;
	jffs2_partition_t *p = (jffs2_partition_t *)info;

	if (info == NULL) {
		return -EINVAL;
	}

	if ((st == NULL) || (len != sizeof(*st))) {
		return -EINVAL;
	}

	sb = p->sb;
	c = JFFS2_SB_INFO(sb);

	spin_lock(&c->erase_completion_lock);

	avail = c->dirty_size + c->free_size;
	resv = c->resv_blocks_write * c->sector_size;

	spin_unlock(&c->erase_completion_lock);

	st->f_bsize = st->f_frsize = sb->s_blocksize;
	st->f_blocks = c->flash_size >> sb->s_blocksize_bits;
	st->f_bavail = st->f_bfree = (avail > resv) ? (avail - resv) >> sb->s_blocksize_bits : 0;
	st->f_files = 0;
	st->f_ffree = 0;
	st->f_favail = 0;
	st->f_fsid = c->mtd->index;
	st->f_flag = sb->s_flags;
	st->f_namemax = JFFS2_MAX_NAME_LEN;

	return EOK;
}


static int libjffs2_sync(void *info, oid_t *oid)
{
	jffs2_partition_t *partition = (jffs2_partition_t *)info;

	if (info == NULL) {
		return -EINVAL;
	}

	return partition->sb->s_op->sync_fs(partition->sb, 0);
}


const static storage_fsops_t fsOps = {
	.open = libjffs2_open,
	.close = libjffs2_close,
	.read = libjffs2_read,
	.write = libjffs2_write,
	.setattr = libjffs2_setattr,
	.getattr = libjffs2_getattr,
	.truncate = libjffs2_truncate,
	.devctl = NULL,
	.create = libjffs2_create,
	.destroy = libjffs2_destroy,
	.lookup = libjffs2_lookup,
	.link = libjffs2_link,
	.unlink = libjffs2_unlink,
	.readdir = libjffs2_readdir,
	.statfs = libjffs2_statfs,
	.sync = libjffs2_sync
};


int libjffs2_mount(storage_t *strg, storage_fs_t *fs, const char *data, unsigned long mode, oid_t *root)
{
	jffs2_partition_t *part;

	if ((strg == NULL) || (strg->dev == NULL) || (strg->dev->mtd == NULL) || (fs == NULL))
		return -EINVAL;

	part = malloc(sizeof(jffs2_partition_t));
	if (part == NULL)
		return -ENOMEM;

	/* Set root id used by jffs2 */
	root->id = 1;

	part->stop_gc = 0;
	part->flags = mode;
	part->strg = strg;
	part->port = root->port;

	fs->info = part;
	fs->ops = &fsOps;

	if (jffs2_common.fs == NULL) {
		init_jffs2_fs();
		beginthread(delayed_work_starter, 4, malloc(0x2000), 0x2000, system_long_wq);
	}

	object_init(part);
	dev_init(&part->devs);

	/* TODO: stop delayed_work_starter */
	if (jffs2_common.fs->mount(jffs2_common.fs, 0, "jffs2", part) == NULL) {
		printf("jffs2: Failed to mount partition.\n");
		free(part);
		return -EIO;
	}

	return EOK;
}


int libjffs2_umount(storage_fs_t *fs)
{
	jffs2_partition_t *part;

	if ((fs == NULL) || (jffs2_common.fs == NULL)) {
		return -EINVAL;
	}
	part = (jffs2_partition_t *)fs->info;

	/* TODO: is it safe to unmount? */
	/* Check for open files, mountpoints within the filesystem etc. */

	/* End gc thread, sync fs, destroy superblock and mtd context */
	jffs2_common.fs->kill_sb(part->sb);

	/* Destroy in-memory objects */
	dev_done(part->devs);
	object_done(part);
	free(part);

	/* No need to destroy jffs2_common and call exit_jffs2_fs() on last unmounted jffs2 partition */
	/* Objects initialized by init_jffs2_fs() may stay in memory */
	/* delayed_work_starter thread and system_long_wq are still running (they're treated as part of the server) */

	return EOK;
}
