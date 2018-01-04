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


int dummyfs_mknod(oid_t *dir, const char *name, unsigned int mode, dev_t dev)
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


int dummyfs_mkdir(oid_t *dir, const char *name, int mode)
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


int dummyfs_rmdir(oid_t *dir, const char *name)
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


int dummyfs_poll(file_t* file, ktime_t timeout, int op)
{

	//TODO
	return -ENOENT;
}


int dummyfs_ioctl(file_t* file, unsigned int cmd, unsigned long arg)
{

	//TODO
	return -ENOENT;
}


int dummyfs_open(vnode_t *vnode, file_t* file)
{
	if (file == NULL || file->priv != NULL || file->vnode == NULL || vnode == NULL || vnode != file->vnode)
		return -EINVAL;
	if (vnode->type != vnodeFile)
		return -EINVAL;

	file->priv = (dummyfs_entry_t *) vnode->fs_priv;
	assert(file->priv != NULL);
	return EOK;
}


int dummyfs_fsync(file_t* file)
{
	dummyfs_entry_t *entry;

	if (file == NULL || file->vnode == NULL || file->vnode->type != vnodeFile)
		return -EINVAL;
	entry = file->priv;
	assert(entry != NULL);

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
