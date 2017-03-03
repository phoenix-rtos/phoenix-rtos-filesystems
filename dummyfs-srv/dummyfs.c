/*
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * Dummy filesystem (used before regular filesystem mounting)
 *
 * Copyright 2012 Phoenix Systems
 * Copyright 2007 Pawel Pisarczyk
 * Author: Pawel Pisarczyk
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <hal/if.h>
#include <main/if.h>
#include <fs/if.h>
#include <vm/if.h>
#include <proc/if.h>

#include "dummyfs.h"

static int dummyfs_create(vnode_t *dir, const char *name, int mode, vnode_t **res);
static int dummyfs_lookup(vnode_t *dir, const char *name, vnode_t **res);
static int dummyfs_link(vnode_t *dir, const char *name, vnode_t *vnode);
static int dummyfs_unlink(vnode_t *dir, const char *name);
static int dummyfs_symlink(vnode_t *dir, const char *name, const char *ref);
static int dummyfs_mkdir(vnode_t *dir, const char *name, int mode);
static int dummyfs_rmdir(vnode_t *dir, const char *name);
static int dummyfs_mknod(vnode_t *dir, const char *name, unsigned int type, dev_t dev);
static int dummyfs_readlink(vnode_t *vnode, char *buf, unsigned int size);
static int dummyfs_readdir(vnode_t *vnode, offs_t offs, dirent_t *dirent, unsigned int count);

static int dummyfs_read(file_t* file, offs_t offs, char *buff, unsigned int len);
static int dummyfs_write(file_t* file, offs_t offs, char *buff, unsigned int len);
static int dummyfs_poll(file_t* file, ktime_t timeout, int op);
static int dummyfs_ioctl(file_t* file, unsigned int cmd, unsigned long arg);
static int dummyfs_open(vnode_t* vnode, file_t *file);
static int dummyfs_fsync(file_t* file);
static int dummyfs_truncate(vnode_t *v, unsigned int size);

static int dummyfs_release(vnode_t *vnode);
/* File locking is on vnode level, fs must only ensure that each entry gives appropriate vnode id */

#define CAN_READ 1
#define CAN_WRITE 2

static const vnode_ops_t  dummyfs_vops = {
	.create = dummyfs_create,
	.lookup = dummyfs_lookup,
	.link = dummyfs_link,
	.unlink = dummyfs_unlink,
	.symlink = dummyfs_symlink,
	.mkdir = dummyfs_mkdir,
	.rmdir = dummyfs_rmdir,
	.mknod = dummyfs_mknod,
	.readlink = dummyfs_readlink,
	//.stat   = dummyfs_stat,
	.readdir = dummyfs_readdir,
	.release = dummyfs_release
};

static const file_ops_t  dummyfs_fops = {
    .open = dummyfs_open,
    .read = dummyfs_read,
    .write = dummyfs_write,
    .poll = dummyfs_poll,
    .ioctl = dummyfs_ioctl,
    .fsync = dummyfs_fsync,
	.truncate = dummyfs_truncate,
};

#define DUMMYFS_MIN_ALLOC 64

#define DUMMYFS_MAX_MEMUSAGE 4096*128

#if DUMMYFS_MAX_MEMUSAGE
static mutex_t memLock;
static u32 dummyfs_mem=0;

#define CHECK_MEMAVAL(size) \
({\
	int res=0;\
	proc_mutexLock(&memLock);\
	res=((size) <= (DUMMYFS_MAX_MEMUSAGE - dummyfs_mem));\
	if(res) dummyfs_mem += (size);\
	proc_mutexUnlock(&memLock);\
	res;\
})
#define MEM_RELEASE(size)\
	({\
		assert(dummyfs_mem >= (size));\
		proc_mutexLock(&memLock);\
		dummyfs_mem -= (size);\
		proc_mutexUnlock(&memLock);\
	})
#else
#define CHECK_MEMAVAL(size) 1
#define MEM_RELEASE(size)
#endif







typedef struct dummyfs_chunk_t{
	char *data;

	size_t offs;/* offset in file */
	size_t size;/* size of chunk */
	size_t used;/* number of used bytes of chunk (might be less than size at the end of file) */

	struct dummyfs_chunk_t *next;
	struct dummyfs_chunk_t *prev;
} dummyfs_chunk_t;


/* one file descriptor per file - necessary for hard links
 * multiple entries could point to the same file descriptor
 */
typedef struct _dummyfs_filedesc_t {
	offs_t size;
	unsigned ref;//how many entries points to this file
	//TODO - monitor number of opens
	unsigned opens;//how many times this file is opened currently
	dummyfs_chunk_t *first;
	dummyfs_chunk_t *recent;
	dummyfs_chunk_t *last;
} dummyfs_filedesc_t;

typedef struct _dummyfs_entry_t {
	char name[SIZE_DUMMYFS_NAME];
	u32 type;
	dev_t dev;
	u64 id;
	unsigned int uid;
	unsigned int gid;
	u32 mode;

	dummyfs_filedesc_t filedes;

	mutex_t lock;

	struct _dummyfs_entry_t *entries;
	struct _dummyfs_entry_t *next;
	struct _dummyfs_entry_t *prev;
} dummyfs_entry_t;

//all we need is directory entry lock


static inline int _dummyfs_insert(dummyfs_entry_t *prev, dummyfs_entry_t *entry)
{
	prev->next->prev = entry;
	entry->next = prev->next;
	prev->next = entry;
	entry->prev = prev;
	return EOK;
}


static int _dummyfs_add(dummyfs_entry_t **list, dummyfs_entry_t *entry)
{
	if ((*list) == NULL) {
		*list = entry;
		entry->next = entry;
		entry->prev = entry;
	}
	else
		_dummyfs_insert((*list)->prev, entry);

	return EOK;
}


static inline int _dummyfs_remove(dummyfs_entry_t **list, dummyfs_entry_t *entry)
{
	entry->prev->next = entry->next;
	entry->next->prev = entry->prev;
	
	if ((entry->next == entry) && (entry->prev == entry))
		(*list) = NULL;
	else if (entry == (*list))
		(*list) = entry->next;

	return EOK;
}


static dummyfs_entry_t *_dummyfs_newentry(dummyfs_entry_t *dir, const char *name, dummyfs_entry_t *child)
{
	dummyfs_entry_t *entry, *direntry;

	if (!CHECK_MEMAVAL(sizeof(dummyfs_entry_t)))
		return NULL;
	 if((entry = (dummyfs_entry_t *)vm_kmalloc(sizeof(dummyfs_entry_t))) == NULL) {
		 MEM_RELEASE(sizeof(dummyfs_entry_t));
		 return NULL;
	 }
	memset(entry, 0, sizeof(dummyfs_entry_t));
	entry->entries = child;
	hal_memcpy(entry->name, (char *)name, main_strlen(name));
	entry->name[main_strlen(name)]='\0';
	direntry = (dummyfs_entry_t *)dir;
	_dummyfs_add(&direntry->entries, entry);

	return entry;
}


int dummyfs_create(vnode_t *dir, const char *name, int mode, vnode_t **res)
{
	dummyfs_entry_t *entry;
	dummyfs_filedesc_t *fd;
	dummyfs_entry_t *dirent;

	*res = NULL;

	if (dir == NULL) {
		return -EINVAL;
	}
	if (dir->type != vnodeDirectory) {
		return -EINVAL;
	}
	if (name == NULL) {
		return -EINVAL;
	}

	dirent = (dummyfs_entry_t *)dir->fs_priv;
	assert(dirent != NULL);

	mode = (mode & ~S_IFMT) | S_IFREG;
	proc_mutexLock(&dirent->lock);
	if ((entry = _dummyfs_newentry(dir->fs_priv, name, NULL)) == NULL) {
		proc_mutexUnlock(&dirent->lock);
		return -ENOMEM;
	}

	entry->mode = mode;
	entry->type = vnodeFile;

	fd = &entry->filedes;
	if(CHECK_MEMAVAL(sizeof(dummyfs_chunk_t))) {
		fd->first = vm_kmalloc(sizeof(dummyfs_chunk_t));

		if(fd->first != NULL) {
			memset(fd->first, 0x0, sizeof(dummyfs_chunk_t));
			fd->first->next = fd->first;
			fd->first->prev = fd->first;
			fd->ref = 1;
			if(CHECK_MEMAVAL(DUMMYFS_MIN_ALLOC))
				fd->first->data = vm_kmalloc(DUMMYFS_MIN_ALLOC);
			if(fd->first->data)
				fd->first->size = DUMMYFS_MIN_ALLOC;
			else
				MEM_RELEASE(DUMMYFS_MIN_ALLOC);
		}
	}
	fd->last = fd->first;
	fd->recent = fd->first;

	*res =vnode_get(dir->sb, (unsigned long)&entry->filedes);
	assert(*res != NULL);
	if (*res) {
        vnode_setDbgName(*res, name);
		(*res)->fops = &dummyfs_fops;
		(*res)->type = vnodeFile;
		(*res)->mode = mode;
		(*res)->size = fd->size;
		entry->uid = (*res)->uid;
		entry->gid = (*res)->gid;
		(*res)->fs_priv = (void *) entry;
	}
	proc_mutexUnlock(&dirent->lock);
	return EOK;
}

static int _dummyfs_lookup(vnode_t *dir, const char *name, vnode_t **res)
{
	dummyfs_entry_t *ei;
	*res = NULL;

	if ((ei = ((dummyfs_entry_t *)dir->fs_priv)->entries) == NULL)
		return -ENOENT;

	do {
		if (!main_strcmp(ei->name, (char *)name)) {
			*res = vnode_get(dir->sb, (unsigned long)&ei->filedes);
			vnode_setDbgName(*res, name);
			//TODO - what about hardlinks with already open file from another entry?
			(*res)->fs_priv = (void *)ei;
			(*res)->type = ei->type;
			(*res)->dev = ei->dev;
			(*res)->mode = ei->mode;
			if(ei->type == vnodeFile) {
				(*res)->fops = &dummyfs_fops;
				(*res)->size = ei->filedes.size;
			}
			(*res)->flags = VNODE_RELEASE_EARLY;
			return EOK;
		}
		ei = ei->next;
	} while (ei != ((dummyfs_entry_t *)dir->fs_priv)->entries);

	return -ENOENT;
}

int dummyfs_lookup(vnode_t *dir, const char *name, vnode_t **res)
{
	dummyfs_entry_t *ei;
	dummyfs_entry_t *dirent;

	*res = NULL;
	int ret=EOK;
	if ((ei = ((dummyfs_entry_t *)dir->fs_priv)->entries) == NULL)
		return -ENOENT;

	dirent = (dummyfs_entry_t *)dir->fs_priv;

	assert(dirent != NULL);

	proc_mutexLock(&dirent->lock);
	ret = _dummyfs_lookup(dir, name, res);
	proc_mutexUnlock(&dirent->lock);
	return ret;
}

static int dummyfs_truncate(vnode_t *v, unsigned int size)
{
	dummyfs_entry_t *entry;
	dummyfs_chunk_t *chunk;
	dummyfs_filedesc_t *fd;

	if (v == NULL)
		return -EINVAL;

	if (v->type != vnodeFile)
		return -EINVAL;
	if (v->size == size)
		return EOK;

	entry = v->fs_priv;
	assert(entry != NULL);
	fd = &entry->filedes;

	if (size == fd->size)
		return EOK;
		
	if (size > fd->size) {

		/* expansion */
		if(fd->first == NULL) {
			/* allocate new chunk */
			unsigned allocSize = (size < DUMMYFS_MIN_ALLOC) ? DUMMYFS_MIN_ALLOC : size;
			if(!CHECK_MEMAVAL(sizeof(dummyfs_chunk_t)))
				return -ENOMEM;

			if((fd->first=vm_kmalloc(sizeof(dummyfs_chunk_t)))==NULL) {
				MEM_RELEASE(sizeof(dummyfs_chunk_t));
				return -ENOMEM;
			}
			memset(fd->first, 0x0, sizeof(dummyfs_chunk_t));
			fd->last = fd->first;
			fd->recent = fd->first;
			fd->first->next = fd->first;
			fd->first->prev = fd->first;
			if(!CHECK_MEMAVAL(allocSize))
				return -ENOMEM;
			if((fd->first->data = vm_kmalloc(allocSize))==NULL) {
				MEM_RELEASE(allocSize);
				return -ENOMEM;
			}
			fd->first->size=allocSize;
			fd->first->used=size;
			memset(fd->first->data, 0x0, size);
		}
		else {
			/* reallocate last chunk */
			if(!CHECK_MEMAVAL(size - (fd->last->offs + fd->last->size)))
				return -ENOMEM;
			char *n = vm_krealloc(fd->last->data, size - fd->last->offs);
			if(n==NULL) {
				MEM_RELEASE(size - (fd->last->offs + fd->last->size));
				return -ENOMEM;
			}
			fd->last->data = n;
			fd->last->size = size - fd->last->offs;
			memset(fd->last->data + fd->last->used, 0x0, fd->last->size - fd->last->used);
			fd->last->used = fd->last->size;
		}
	}
	else {
		/* shrink */
		dummyfs_chunk_t *toDel;
		for (chunk = fd->last; chunk != fd->first && chunk->offs >= size; chunk = chunk->prev);

		/* chunk now points to last area that shuold be preserved - everything after it will be freed. */
		toDel = chunk->next;
		while(toDel != fd->first) {
			dummyfs_chunk_t *tmp=toDel->next;
			vm_kfree(toDel->data);
			MEM_RELEASE(toDel->size);
			vm_kfree(toDel);
			MEM_RELEASE(sizeof(dummyfs_chunk_t));
			toDel = tmp;
		}
		fd->last = chunk;
		fd->first->prev = chunk;
		chunk->next = fd->first;
		fd->recent = chunk;
		chunk->used = size-chunk->offs;
	}
	fd->size = size;
	v->size = size;
	return EOK;
}

int dummyfs_link(vnode_t *dir, const char *name, vnode_t *vnode)
{
	dummyfs_entry_t *dirent;

	if (dir == NULL)
		return -EINVAL;
	if (dir->type != vnodeDirectory)
		return -EINVAL;
	if (name == NULL)
		return -EINVAL;
	if (vnode == NULL)
		return -EINVAL;
	if (vnode->type == vnodeDirectory)
		return -EINVAL;
	if (vnode->sb != dir->sb)
		return -EINVAL;

	dirent = (dummyfs_entry_t *)dir->fs_priv;
	assert(dirent != NULL);
	//TODO
	return -EIO ;
}


int dummyfs_unlink(vnode_t *dir, const char *name)
{
	dummyfs_entry_t *entry;
	dummyfs_filedesc_t *fd;
	dummyfs_entry_t *dirent;

	if (dir == NULL)
		return -EINVAL;
	if (dir->type != vnodeDirectory)
		return -EINVAL;
	if (name == NULL)
		return -EINVAL;

	dirent = (dummyfs_entry_t *)dir->fs_priv;
	assert(dirent != NULL);
	//TODO - support for device files
	proc_mutexLock(&dirent->lock);
	if ((entry = ((dummyfs_entry_t *)dir->fs_priv)->entries) == NULL) {
		proc_mutexUnlock(&dirent->lock);
		return -ENOENT;
	}

	do {
		if (!main_strcmp(entry->name, (char *)name))
			break;
		entry = entry->next;
	} while (entry != ((dummyfs_entry_t *)dir->fs_priv)->entries);

	if(entry == ((dummyfs_entry_t *)dir->fs_priv)->entries && main_strcmp(entry->name, (char *)name)) {
		proc_mutexUnlock(&dirent->lock);
		return -ENOENT;
	}


	assert(entry != NULL);
	fd = &entry->filedes;
	assert((entry->type == vnodeFile && fd != NULL) || (entry->type != vnodeFile));
	if(fd != NULL) {
		if(fd->first != NULL) {
			--fd->ref;
			if(fd->ref == 0)
			{
				dummyfs_chunk_t *curr = fd->last->prev;
				dummyfs_chunk_t *toDel = curr->next;
				while(toDel != NULL)
				{
					MEM_RELEASE(toDel->size);
					vm_kfree(toDel->data);
					MEM_RELEASE(sizeof(dummyfs_chunk_t));
					vm_kfree(toDel);
					if(toDel == fd->first)
						toDel = NULL;
					else {
						toDel = curr;
						curr = curr->prev;
					}
				}
				fd->first = fd->last = fd->recent = NULL;
			}
		}
		fd->size = 0;
	}
	_dummyfs_remove(&((dummyfs_entry_t *)dir->fs_priv)->entries, entry);

	MEM_RELEASE(sizeof(dummyfs_entry_t));
	vm_kfree(entry);
	proc_mutexUnlock(&dirent->lock);
	//TODO - what do do with opened file
	return EOK;
}

int dummyfs_release(vnode_t *vnode)
{
	return -EFAULT;
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


int dummyfs_read(file_t* file, offs_t offs, char *buff, unsigned int len)
{  
	dummyfs_entry_t *entry;
	dummyfs_chunk_t *chunk;
	dummyfs_filedesc_t *fd;

	int ret = 0;
	if (file == NULL || file->priv == NULL)
		return -EINVAL;
	if (file->vnode->type != vnodeFile)
		return -EINVAL;
	if (buff == NULL)
		return -EINVAL;

	entry = file->priv;
	assert(entry != NULL);
	fd = &entry->filedes;

	if(fd->last == NULL || offs >= (fd->last->offs + fd->last->used)) {
		return 0;
	}

	for(chunk = fd->first; chunk->next != fd->first; chunk = chunk->next)
		if(chunk->offs <= offs && (chunk->offs + chunk->used) > offs )
			break;
	if(chunk->offs <= offs && (chunk->offs + chunk->size) > offs )
		do {
			int remaining = chunk->used - (offs-chunk->offs);
			assert(chunk->used > (offs-chunk->offs));
			remaining = (remaining > len) ? len : remaining;
			if(remaining > 0)
				memcpy(buff, chunk->data + (offs-chunk->offs), remaining);

			buff += remaining;
			len -= remaining;
			offs+=remaining;
			ret += remaining;
			fd->recent = chunk;
			chunk = chunk->next;
		}while(len > 0 && chunk != fd->first);
	else
		ret = 0;
	return ret;
}


int dummyfs_write(file_t* file, offs_t offs, char *buff, unsigned int len)
{
	vnode_t *vnode;
	dummyfs_entry_t *entry;
	dummyfs_chunk_t *chunk;
	dummyfs_filedesc_t *fd;

	int ret = 0;
	unsigned int allocSize = (len < DUMMYFS_MIN_ALLOC) ? DUMMYFS_MIN_ALLOC : len;

	if (file == NULL || file->priv == NULL || file->vnode == NULL)
		return -EINVAL;
	if (file->vnode->type != vnodeFile)
		return -EINVAL;
	if (buff == NULL)
		return -EINVAL;

	entry = file->priv;
	vnode = file->vnode;
	assert(entry != NULL);
	fd = &entry->filedes;

	if(fd->first == NULL) {
		if(!CHECK_MEMAVAL(sizeof(dummyfs_chunk_t)))
			return -ENOMEM;
		if((fd->first = vm_kmalloc(sizeof(dummyfs_chunk_t)))==NULL) {
			MEM_RELEASE(sizeof(dummyfs_chunk_t));
			return -ENOMEM;
		}
		chunk=fd->first;
		fd->last = chunk;
		fd->recent = chunk;
		chunk->next = chunk;
		chunk->prev = chunk;

		memset(chunk, 0x0, sizeof(dummyfs_chunk_t));
		if(!CHECK_MEMAVAL(allocSize))
			return -ENOMEM;/* no need to free entry, because it is a first entry */
		if((chunk->data = vm_kmalloc(allocSize))==NULL) {
			MEM_RELEASE(allocSize);
			return -ENOMEM;
		}
		chunk->size = allocSize;
	}

	/* NO SUPPORT FOR SPARSE FILES */
	if(offs > (fd->last->offs + fd->last->used))
		return -EINVAL;

	if(offs == (fd->last->offs + fd->last->used))
		chunk = fd->last;/* appending */
	else
		for(chunk = fd->first; chunk->next != fd->first; chunk = chunk->next)
			if(chunk->offs <= offs && (chunk->offs + chunk->size) > offs )
				break; /* found appropriate chunk */


	do {
		int remaining = chunk->size - (offs-chunk->offs);
		int used;
		remaining = (remaining > len) ? len : remaining;
		used = (offs - chunk->offs) + remaining;
		if(remaining > 0)
			memcpy(chunk->data + (offs-chunk->offs), buff, remaining);

		buff += remaining;
		len -= remaining;
		ret += remaining;
		offs += remaining;

		chunk->used = (chunk->used > used) ? chunk->used : used;
		vnode->size = fd->last->offs + fd->last->used;
		fd->size = fd->last->offs + fd->last->used;

		if(len > 0) {
			if(chunk->next == fd->first) {
				allocSize = (len < DUMMYFS_MIN_ALLOC) ? DUMMYFS_MIN_ALLOC : len;
				dummyfs_chunk_t *n;
				if(!CHECK_MEMAVAL(sizeof(dummyfs_chunk_t)))
					return ret;
				if( (n=vm_kmalloc(sizeof(dummyfs_chunk_t)))==NULL ) {
					MEM_RELEASE(sizeof(dummyfs_chunk_t));
					return ret;
				}
				memset(n, 0x0, sizeof(dummyfs_chunk_t));
				n->offs = chunk->offs + chunk->size;
				if(!CHECK_MEMAVAL(allocSize)) {
					return ret;
				}
				if((n->data = vm_kmalloc(allocSize))==NULL) {
					MEM_RELEASE(allocSize);
					vm_kfree(n);
					MEM_RELEASE(sizeof(dummyfs_chunk_t));
					return ret;
				}
				n->size = allocSize;
				n->next = fd->first;
				n->prev = chunk;
				fd->last=n;
				chunk->next = n;
				fd->first->prev = n;
			}
			chunk = chunk->next;
		}
		fd->recent = chunk;
	}
	while(len > 0);
	assert(len == 0);

	return ret;
}


int dummyfs_readdir(vnode_t *vnode, offs_t offs,dirent_t *dirent, unsigned int count)
{

	dummyfs_entry_t *ei;

	dirent_t *bdent = 0;
	u32 dir_offset = 0;
	u32 dirent_offs = 0;
	u32 item = 0;
	u32 u_4;

	if (vnode == NULL)
		return -EINVAL;
	if (vnode->type != vnodeDirectory)
		return -ENOTDIR;

	if ((ei = ((dummyfs_entry_t *)vnode->fs_priv)->entries) != NULL)
	do{

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

int dummyfs_readsuper(void *opt, superblock_t **superblock)
{
	superblock_t *sb;
	dummyfs_entry_t *entry;

	if (!CHECK_MEMAVAL(sizeof(superblock_t)))
		return -ENOMEM;
	if((sb = (superblock_t *)vm_kmalloc(sizeof(superblock_t))) == NULL) {
		MEM_RELEASE(sizeof(superblock_t));
		return -ENOMEM;
	}


	sb->root = vnode_get(sb, 0);
    sb->vops = &dummyfs_vops;
	vnode_setDbgName(sb->root, "-dummyfs-root-");

	sb->root->type = vnodeDirectory;
	sb->root->dev = 0;
	sb->root->mode = 0x41ed;
	sb->root->uid = 0;
	sb->root->gid = 0;
	sb->root->size = 0;
	sb->root->id = 0;
	sb->root->fops = 0;
//	sb->root->flags = 0;

	if ((entry = (dummyfs_entry_t *)vm_kmalloc(sizeof(dummyfs_entry_t))) == NULL) {
		vm_kfree(sb);
		return -ENOMEM;
	}
	memset(entry, 0, sizeof(dummyfs_entry_t));
	proc_mutexCreate(&entry->lock);
	sb->root->fs_priv = (void *)entry;

	*superblock = sb;
	return EOK;
}


int dummyfs_init(void)
{
	static filesystem_t dummyfs;

	dummyfs.type = TYPE_DUMMYFS;
	dummyfs.readsuper = dummyfs_readsuper;

#if DUMMYFS_MAX_MEMUSAGE
	proc_mutexCreate(&memLock);
#endif

	fs_register(&dummyfs);
	return EOK;
}
