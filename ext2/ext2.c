/*
 * Phoenix-RTOS
 *
 * EXT2 filesystem
 *
 * Copyright 2017, 2020 Phoenix Systems
 * Author: Kamil Amanowicz, Lukasz Kosinski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include "ext2.h"


uint32_t ext2_find_zero_bit(uint32_t *addr, uint32_t size, uint32_t offs)
{
	uint32_t i, tmp;
	uint32_t len = size / (8 * sizeof(uint32_t));

	for (i = offs / (8 * sizeof(uint32_t)); i < len; i++) {
		tmp = addr[i] ^ ~0UL;

		if (tmp)
			break;
	}

	if (i == len)
		return 0;

	return i * (8 * sizeof(uint32_t)) + (uint32_t)__builtin_ffsl(tmp);
}


uint8_t ext2_check_bit(uint32_t *addr, uint32_t offs)
{
	uint32_t woffs = (offs - 1) % (8 * sizeof(uint32_t));
	uint32_t aoffs = (offs - 1) / (8 * sizeof(uint32_t));

	return (addr[aoffs] & 1UL << woffs) != 0;
}


uint32_t ext2_toggle_bit(uint32_t *addr, uint32_t offs)
{
	uint32_t woffs = (offs - 1) % (8 * sizeof(uint32_t));
	uint32_t aoffs = (offs - 1) / (8 * sizeof(uint32_t));
	uint32_t old = addr[aoffs];

	addr[aoffs] ^= 1UL << woffs;

	return (old & 1UL << woffs) != 0;
}


static int ext2_destroy(ext2_t *f, id_t *id);
static int ext2_link(ext2_t *f, id_t *dirId, const char *name, id_t *id);
static int ext2_unlink(ext2_t *f, id_t *dirId, const char *name);


static int ext2_lookup(ext2_t *f, oid_t *dir, const char *name, oid_t *resId, oid_t *dev)
{
	id_t *id = &dir->id;
	int err;
	uint32_t len = strlen(name);
	ext2_obj_t *d, *o;

	printf("Lookup start\n");
	if (*id < 2)
		return -EINVAL;

	if (len == 0)
		return -ENOENT;

	d = object_get(f, id);

	if (d == NULL)
		return -ENOENT;

	mutexLock(d->lock);
	err = dir_find(d, name, len, &resId->id);
	if (!err) {
		resId->port = f->port;
		o = object_get(f, &resId->id);
		if (o->id != d->id)
			mutexLock(o->lock);

		if (object_checkFlag(d, EXT2_FL_MOUNTPOINT) && len == strlen("..") && !strncmp(name, "..", len))
			resId->id = d->id;

		if(EXT2_S_ISCHR(o->inode->mode)) {
			dev->port = 0;
			dev->id = o->id;
		}
		else {
			dev->port = resId->port;
			dev->id = resId->id;
		}

		if (o->id != d->id)
			mutexUnlock(o->lock);
		object_put(o);
	}

	mutexUnlock(d->lock);
	object_put(d);
	printf("Lookup end\n");
	return err;
}


static int ext2_setattr(ext2_t *f, id_t *id, int type, const void *data, size_t size)
{
	ext2_obj_t *o = object_get(f, id);
	int res = EOK;

	printf("Setattr start\n");
	if (o == NULL)
		return -EINVAL;

	mutexLock(o->lock);

	switch(type) {

		case atMode:
			o->inode->mode = ((o->inode->mode & S_IFMT) | (*(int *)data & ~S_IFMT));
			break;

		case atUid:
			o->inode->uid = *(int *)data;
			break;

		case atGid:
			o->inode->gid = *(int *)data;
			break;

		case atSize:
			mutexUnlock(o->lock);
			ext2_truncate(f, id, *(int *)data);
			mutexLock(o->lock);
			break;
		//TODO:
		// case atMount:
		// 	object_sync(o);
		// 	object_setFlag(o, EXT2_FL_MOUNT);
		// 	memcpy(&o->mnt, data, sizeof(oid_t));
		// 	mutexUnlock(o->lock);
		// 	return res;
		// case atMountPoint:
		// 	object_setFlag(o, EXT2_FL_MOUNTPOINT);
		// 	memcpy(&o->mnt, data, sizeof(oid_t));
		// 	o->refs++;
		// 	object_get(f, id);
		// 	break;
	}

	o->inode->mtime = o->inode->atime = time(NULL);
	object_setFlag(o, EXT2_FL_DIRTY);
	object_sync(o);
	mutexUnlock(o->lock);
	object_put(o);
	printf("Setattr end\n");
	return res;
}


static int ext2_getattr(ext2_t *f, id_t *id, int type, void *attr, size_t maxlen)
{
	ext2_obj_t *o = object_get(f, id);
	//struct stat *stat;
	ssize_t ret = 0;

	printf("Getattr start\n");
	if (o == NULL)
		return -EINVAL;

	mutexLock(o->lock);

	switch(type) {

		case atMode:
			*(int *)attr = o->inode->mode;
			ret = sizeof(int);
			break;
		//TODO:
		// case atStatStruct:
		// 	stat = (struct stat *)attr;
		// 	//stat->st_dev = o->port;
		// 	stat->st_ino = o->id;
		// 	stat->st_mode = o->inode->mode;
		// 	stat->st_nlink = o->inode->nlink;
		// 	stat->st_uid = o->inode->uid;
		// 	stat->st_gid = o->inode->gid;
		// 	stat->st_size = o->inode->size;
		// 	stat->st_atime = o->inode->atime;
		// 	stat->st_mtime = o->inode->mtime;
		// 	stat->st_ctime = o->inode->ctime;
		// 	ret = sizeof(struct stat);
		// 	break;
		// case atMount:
		// case atMountPoint:
		// 	*(oid_t *)attr = o->mnt;
		// 	break;
		case atSize:
			*(int *)attr = o->inode->size;
			ret = sizeof(int);
			break;
		//TODO:
		// case atEvents:
		// 	*(int *)attr = POLLIN | POLLOUT;
		// 	ret = sizeof(int);
		// 	break;
	}

	mutexUnlock(o->lock);
	object_put(o);
	printf("Getattr end\n");
	return ret;
}


static int ext2_create(ext2_t *f, id_t *dirId, const char *name, id_t *resId, int type, int mode, oid_t *dev)
{
	ext2_obj_t *o;
	ext2_inode_t *inode = NULL;
	int ret;

	printf("Create start\n");
	if (name == NULL || strlen(name) == 0)
		return -EINVAL;

	switch (type) {
	case otDir:
		mode |= S_IFDIR;
		break;
	case otFile:
		mode |= S_IFREG;
		break;
	case otDev:
		mode = (mode & 0x1ff) | S_IFCHR;
		break;
	}

	o = object_create(f, resId, dirId, &inode, mode);

	if (o == NULL) {
		if (inode == NULL)
			return -ENOSPC;

		free(inode);
		return -ENOMEM;
	}

	o->inode->ctime = o->inode->mtime = o->inode->atime = time(NULL);

	if ((ret = ext2_link(f, dirId, name, resId)) != EOK) {
		object_put(o);
		ext2_destroy(f, resId);
		return ret;
	}
	object_put(o);
	TRACE("ino %llu ref %u", o->id, o->refs);
	printf("Create end\n");
	return EOK;
}


static int ext2_destroy(ext2_t *f, id_t *id)
{
	ext2_obj_t *o = object_get(f, id);
	printf("Destroy start\n");
	if (o == NULL)
		return -EINVAL;

	object_sync(o);
	ext2_truncate(f, id, 0);
	object_destroy(o);
	printf("Destroy end\n");
	return EOK;
}


static int ext2_link(ext2_t *f, id_t *dirId, const char *name, id_t *id)
{
	ext2_obj_t *d, *o;
	uint32_t len = strlen(name);
	int res;

	printf("Link start\n");
	if (dirId == NULL || id == NULL)
		return -EINVAL;

	if (*dirId < 2 || *id < 2)
		return -EINVAL;

	d = object_get(f, dirId);
	o = object_get(f, id);

	if (o == NULL || d == NULL)
		return -EINVAL;

	if (!(d->inode->mode & S_IFDIR)) {
		object_put(o);
		object_put(d);
		return -ENOTDIR;
	}

	if (dir_find(d, name, len, id) == EOK) {
		object_put(o);
		object_put(d);
		return -EEXIST;
	}

	if((o->inode->mode & S_IFDIR) && o->inode->nlink) {
		object_put(o);
		object_put(d);
		return -EMLINK;
	}

	mutexLock(d->lock);
	if ((res = dir_add(d, name, len, o->inode->mode, id)) == EOK) {

		mutexUnlock(d->lock);

		mutexLock(o->lock);
		o->inode->nlink++;
		o->inode->uid = 0;
		o->inode->gid = 0;
		o->inode->mtime = o->inode->atime = time(NULL);
		object_setFlag(o, EXT2_FL_DIRTY);

		if(o->inode->mode & S_IFDIR) {
			dir_add(o, ".", 1, S_IFDIR, id);
			o->inode->nlink++;
			dir_add(o, "..", 2, S_IFDIR, dirId);
			object_sync(o);
			mutexUnlock(o->lock);

			mutexLock(d->lock);
			d->inode->nlink++;
			object_setFlag(d, EXT2_FL_DIRTY);
			object_sync(d);
			mutexUnlock(d->lock);

			object_put(o);
			object_put(d);
			return res;
		}

		object_sync(o);
		mutexUnlock(o->lock);
		object_put(o);
		object_put(d);
		TRACE("ino %llu ref %u", o->id, o->refs);
		return res;
	}

	mutexUnlock(d->lock);
	object_put(o);
	object_put(d);
	printf("Link end\n");
	return res;
}


static int ext2_unlink(ext2_t *f, id_t *dirId, const char *name)
{
	ext2_obj_t *d, *o;
	uint32_t len = strlen(name);
	id_t id;

	printf("Unlink start\n");
	d = object_get(f, dirId);

	if (d == NULL)
		return -EINVAL;

	if (!(d->inode->mode & S_IFDIR)) {
		object_put(d);
		return -ENOTDIR;
	}

	mutexLock(d->lock);

	if (dir_find(d, name, len, &id) != EOK) {
		mutexUnlock(d->lock);
		object_put(d);
		return -ENOENT;
	}

	o = object_get(f, &id);

	if (o == NULL) {
		dir_remove(d, name, len);
		object_put(d);
		return -ENOENT;
	}

	if (object_checkFlag(o, EXT2_FL_MOUNTPOINT | EXT2_FL_MOUNT)) {
		mutexUnlock(d->lock);
		object_put(o);
		object_put(d);
		return -EBUSY;
	}

	if (dir_remove(d, name, len) != EOK) {
		mutexUnlock(d->lock);
		object_put(o);
		object_put(d);
		return -ENOENT;
	}

	mutexUnlock(d->lock);

	mutexLock(o->lock);
	o->inode->nlink--;
	if (o->inode->mode & S_IFDIR) {
		/* TODO: check if empty? */
		d->inode->nlink--;
		o->inode->nlink--;
		object_put(o);
		object_put(d);
		return EOK;
	}

	o->inode->mtime = o->inode->atime = time(NULL);
	mutexUnlock(o->lock);

	object_put(o);
	object_put(d);
	printf("Unlink end\n");
	return EOK;
}


int ext2_readdir(ext2_obj_t *d, off_t offs, struct dirent *dent, size_t size)
{
	ext2_dir_entry_t *dentry;
	int err = EOK, ret = -ENOENT;

	printf("Readdir start\n");
	if (d == NULL)
		return -EINVAL;

	if (!d->inode->nlink)
		return -ENOENT;

	if (!d->inode->size)
		return -ENOENT;

	if (size < sizeof(ext2_dir_entry_t))
		return -EINVAL;

	dentry = malloc(size);
	memset(dent, 0, size);
	while (offs < d->inode->size && offs >= 0) {
		err = ext2_read_internal(d, offs, (void *)dentry, size);

		if (err < 0) {
			ret = err;
			break;
		}

		if (!dentry->name_len)
			break;

		if (size <= dentry->name_len + sizeof(struct dirent)) {
			ret = -EINVAL;
			break;
		}

		dent->d_ino = dentry->inode;
		dent->d_reclen = dentry->rec_len;
		dent->d_namlen = dentry->name_len;
		dent->d_type = dentry->file_type == EXT2_FT_DIR ? 0 : 1;
		memcpy(&(dent->d_name[0]), dentry->name, dentry->name_len);

		if (object_checkFlag(d, EXT2_FL_MOUNTPOINT) && !strcmp(dent->d_name, ".."))
			dent->d_ino = d->mnt.id;
		ret = dent->d_reclen;
		break;
	}
	d->inode->atime = time(NULL);

	free(dentry);
	printf("Readdir end\n");
	return ret;
}


static int ext2_open(ext2_t *f, id_t *id)
{
	ext2_obj_t *o = object_get(f, id);
	printf("Open start\n");
	if (o != NULL) {
		TRACE("ino %llu ref %u", o->id, o->refs);
		o->inode->atime = time(NULL);
		return EOK;
	}
	printf("Open end\n");
	return -EINVAL;
}


static int ext2_close(ext2_t *f, id_t *id)
{
	ext2_obj_t *o = object_get(f, id);

	printf("Close start\n");
	if (!o)
		return -EINVAL;

	mutexLock(o->lock);
	object_sync(o);
	mutexUnlock(o->lock);
	object_put(o);
	object_put(o);
	TRACE("ino %llu ref	return err; %u", o->id, o->refs);
	printf("Close end\n");
	return EOK;
}


int libext2_handler(void *data, msg_t *msg)
{
	ext2_t *f = (ext2_t *)data;

	switch (msg->type) {
		case mtLookup:
			msg->o.lookup.err = ext2_lookup(f, &msg->i.lookup.dir, msg->i.data, &msg->o.lookup.fil, &msg->o.lookup.dev);
			break;

		case mtCreate:
			msg->o.create.err = ext2_create(f, &msg->i.create.dir.id, msg->i.data, &msg->o.create.oid.id, msg->i.create.type, msg->i.create.mode, &msg->i.create.dev);
			break;

		case mtDestroy:
			msg->o.io.err = ext2_destroy(f, &msg->i.destroy.oid.id);
			break;

		case mtRead:
			msg->o.io.err = ext2_read(f, &msg->i.io.oid.id, msg->i.io.offs, msg->o.data, msg->o.size);
			break;

		case mtWrite:
			msg->o.io.err = ext2_write(f, &msg->i.io.oid.id, msg->i.io.offs, msg->i.data, msg->i.size);
			break;

		case mtTruncate:
			msg->o.io.err = ext2_truncate(f, &msg->i.io.oid.id, msg->i.io.len);
			break;

		case mtDevCtl:
			msg->o.io.err = -EINVAL;
			break;

		case mtSetAttr:
			msg->o.io.err = ext2_setattr(f, &msg->i.attr.oid.id, msg->i.attr.type, &msg->i.attr.val, sizeof(int));
			break;

		case mtGetAttr:
			msg->o.io.err = ext2_getattr(f, &msg->i.attr.oid.id, msg->i.attr.type, &msg->o.attr.val, sizeof(int));
			break;

		case mtLink:
			msg->o.io.err = ext2_link(f, &msg->i.ln.dir.id, msg->i.data, &msg->i.ln.oid.id);
			break;

		case mtUnlink:
			msg->o.io.err = ext2_unlink(f, &msg->i.ln.dir.id, msg->i.data);
			break;

		case mtOpen:
			TRACE("direct open %llu",msg->i.openclose.oid.id);
			msg->o.io.err = ext2_open(f, &msg->i.openclose.oid.id);
			//TODO:
			//msg->o.open = msg->object;
			break;

		case mtClose:
			TRACE("direct close %llu", msg->i.openclose.oid.id);
			msg->o.io.err = ext2_close(f, &msg->i.openclose.oid.id);
			break;
	}

	return EOK;
}


int libext2_mount(oid_t *oid, read_cb read, write_cb write, void **fsData)
{
	int ret = -EFAULT;
	id_t rootId = 2;
	ext2_t *f = calloc(1, sizeof(ext2_t));
	f->sb = calloc(1, EXT2_SB_SZ);
	f->read = read;
	f->write = write;
	f->port = oid->port;
	*fsData = f;

	ret = ext2_read_sb(oid->id, f);

	if (ret != EOK) {
		printf("ext2: no ext2 partition found\n");
		return ret;
	}

	ext2_init_fs(oid->id, f);
	object_init(f);
	f->root = object_get(f, &rootId);
	return (int)rootId;
}


int libext2_unmount(void *fsData)
{
	return 0;
}


int ext2_init(uint32_t port, id_t dev, uint32_t sectorsz, read_dev read, write_dev write, ext2_t *fs)
{
	int err;

	fs->port = port;
	fs->dev = dev;
	fs->sectorsz = sectorsz;
	fs->read = read;
	fs->write = write;

	if ((err = ext2_init_sb(fs)) < 0)
		return err;

	if ((err = ext2_init_gdt(fs)) < 0)
		return err;

	if ((err = ext2_init_objs(fs)) < 0)
		return err;

	fs->root = ext2_get_obj(fs, ROOT_INO);

	return EOK;
}
