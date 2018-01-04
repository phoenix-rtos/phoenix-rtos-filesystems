/*
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * dummyfs - directory operations
 *
 * Copyright 2018 Phoenix Systems
 * Author: Pawel Pisarczyk
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdio.h>
#include <sys/msg.h>

#include "dummyfs.h"


int dir_find(void)
{

	/* Iterate over all entries to find the matching one */
	do {
		if (!strcmp(e->name, (char *)name)) {
			memcpy(res, &e->oid, sizeof(oid_t));
			mutexUnlock(dummyfs_common.mutex);
			dummyfs_put(o);
			return EOK;
		}

		e = e->next;
	} while (e != dir->entries);

	return EOK;
}


int dummyfs_symlink(vnode_t *dir, const char *name, const char *ref)
{
	dummyfs_entry_t *dirent;

	if (dir == NULL)
		return -EINVAL;
	if (dir->type != vnodeDirectory)
		return -EINVAL;
	if (name == NULL)
		return -EINVAL;
	if (ref == NULL)
		return -EINVAL;

	dirent = (dummyfs_entry_t *)dir->fs_priv;
	assert(dirent != NULL);
	//TODO
	return -ENOENT;
}


int dummyfs_mkdir(vnode_t *dir, const char *name, int mode)
{
	dummyfs_entry_t *entry;
	dummyfs_entry_t *dirent;
	vnode_t *res;

	if (dir == NULL)
		return -EINVAL;
	if (dir->type != vnodeDirectory)
		return -EINVAL;
	if (name == NULL)
		return -EINVAL;

	dirent = (dummyfs_entry_t *)dir->fs_priv;

	proc_mutexLock(&dirent->lock);
	_dummyfs_lookup(dir, name, &res);
	if(res != NULL) {
		proc_mutexUnlock(&dirent->lock);
		return -EEXIST;
	}

	if ((entry = _dummyfs_newentry(dir->fs_priv, name, NULL)) == NULL) {
		proc_mutexUnlock(&dirent->lock);
		return -ENOMEM;
	}

	entry->mode = mode;
	entry->type = vnodeDirectory;

	proc_mutexCreate(&entry->lock);

	if ((entry = _dummyfs_newentry(entry, "..", dir->fs_priv)) == NULL) {
		_dummyfs_remove(&((dummyfs_entry_t *)dir->fs_priv)->entries, entry);
		MEM_RELEASE(sizeof(dummyfs_entry_t));
		vm_kfree(entry);
		proc_mutexUnlock(&dirent->lock);
		return -ENOMEM;
	}
	proc_mutexUnlock(&dirent->lock);
	return EOK;
}


int dummyfs_rmdir(vnode_t *dir, const char *name)
{
	dummyfs_entry_t *dirent;
	vnode_t *tr;
	dummyfs_entry_t *entry;


	if (dir == NULL)
		return -EINVAL;
	if (dir->fs_priv == NULL)
		return -EINVAL;
	if (dir->type != vnodeDirectory)
		return -ENOTDIR;
	if (name == NULL)
		return -EINVAL;

	dirent = (dummyfs_entry_t *)dir->fs_priv;
	assert(dirent != NULL);

	proc_mutexLock(&dirent->lock);
	_dummyfs_lookup(dir, name, &tr);
	if(tr == NULL) {
		proc_mutexUnlock(&dirent->lock);
		return -ENOENT;
	}
	assert(tr->type == vnodeDirectory);
	entry = (dummyfs_entry_t *)tr->fs_priv;

	_dummyfs_remove(&((dummyfs_entry_t *)dir->fs_priv)->entries, entry);
	MEM_RELEASE(sizeof(dummyfs_entry_t));
	proc_mutexTerminate(&entry->lock);
	vm_kfree(entry);


	proc_mutexUnlock(&dirent->lock);
	return EOK;
}


int dummyfs_mknod(vnode_t *dir, const char *name, unsigned int mode, dev_t dev)
{
	dummyfs_entry_t *entry;
	dummyfs_entry_t *dirent;
    unsigned int type;

	if (dir == NULL)
		return -EINVAL;
	if (dir->type != vnodeDirectory)
		return -EINVAL;
	if (name == NULL)
		return -EINVAL;

	dirent = (dummyfs_entry_t *)dir->fs_priv;
	assert(dirent != NULL);
	proc_mutexLock(&dirent->lock);
    if (S_ISCHR(mode) || S_ISBLK(mode)) {
        type = vnodeDevice;
    } else if (S_ISFIFO(mode)) {
        type = vnodePipe;
    } else {
		proc_mutexUnlock(&dirent->lock);
        return -EINVAL;
    }

	if ((entry = _dummyfs_newentry(dir->fs_priv, name, NULL)) == NULL) {
		proc_mutexUnlock(&dirent->lock);
		return -ENOMEM;
	}

    entry->type = type;
	entry->dev = dev;
    entry->mode = mode & S_IRWXUGO;
	proc_mutexUnlock(&dirent->lock);
	return EOK;
}


int dummyfs_readlink(vnode_t *vnode, char *buf, unsigned int size)
{

	if (vnode == NULL)
		return -EINVAL;
	if (vnode->fs_priv == NULL)
		return -EINVAL;
	if (buf == NULL)
		return -EINVAL;

	//TODO
	return -ENOENT;
}


int dummyfs_readdir(oid_t *oid, offs_t offs, dirent_t *dirent, unsigned int count)
{
	dummyfs_entry_t *ei;

	dirent_t *bdent = 0;
	u32 dir_offset = 0;
	u32 dirent_offs = 0;
	u32 item = 0;
	u32 u_4;

	o = dummyfs_get(oid);

	if (o->type != otDir)
		return -ENOTDIR;

	if ((dirent = o->entries) == NULL)
		return -EINVAL;

	dirent = o->entries;

	do {
		item = strlen(ei->name) + 1;

		u_4=(4 - (sizeof(dirent_t) + item) % 4) % 4;
		if(dir_offset >= offs){
			if ((dirent_offs + sizeof(dirent_t) + item) > count) goto quit;
			bdent = (dirent_t*) (((char*)dirent) + dirent_offs);
			bdent->d_ino = (addr_t)&ei;
			bdent->d_off = dirent_offs + sizeof(dirent_t) + item + u_4;
			bdent->d_reclen = sizeof(dirent_t) + item + u_4;
			memcpy(&(bdent->d_name[0]), ei->name, item);
			dirent_offs += sizeof(dirent_t) + item + u_4;
		}
		dir_offset += sizeof(dirent_t) + item + u_4;
		ei = ei->next;

	}while (ei != ((dummyfs_entry_t *)vnode->fs_priv)->entries);

	return 	dirent_offs;;

quit:
	if(dirent_offs == 0)
		return  -EINVAL; /* Result buffer is too small */

	return dirent_offs;

}
