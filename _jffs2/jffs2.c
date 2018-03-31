/* 
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * Jffs2 FileSystem.
 *
 * Copyright 2014-2015 Phoenix Systems
 * Author: Katarzyna Baranowska, Pawel Krezolek
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <main/if.h>
#include <fs/if.h>
#include <vm/if.h>
#include <dev/if.h>
#include <proc/if.h>

#include <dev/storage/flash/mtd_if.h>

#include <fs/jffs2/jffs2.h>
#include <fs/jffs2/nodelist.h>
#include <fs/jffs2/compr.h>


/* GC sleeps at most 60s */
#define JFFS2_GC_SLEEP_TIME (60 * 1000000)


//TODO return proper time when real time module in phoenix will be ready
static inline u32 getNow(void)
{
	return os_to_jffs2_time(timesys_getTime() * 1000000);
}


int jffs2_garbage_collect_thread(void *arg)
{
	struct jffs2_sb_info *c = arg;

	while(1) {
		cond_resched();
		spin_lock(&c->erase_completion_lock);
		while (!jffs2_thread_should_wake(c)) 
			proc_condWait(&c->gc_task, &c->erase_completion_lock, JFFS2_GC_SLEEP_TIME);
		spin_unlock(&c->erase_completion_lock);

		if (jffs2_garbage_collect_pass(c) == -ENOSPC)
			return -ENOSPC;
	}
}


int jffs2_start_garbage_collect_thread(struct jffs2_sb_info *c)
{
	return proc_thread(NULL, jffs2_garbage_collect_thread, NULL, 0, c, ttRegular);
}


static void jffs2_stop_garbage_collect_thread(struct jffs2_sb_info *c)
{
	//TODO Inform GC process (if it is working) that it should end
	// and wait till its end
}


static int jffs2_create(vnode_t *dir, const char *name, int mode, vnode_t **res);
static int jffs2_lookup(vnode_t *dir, const char *name, vnode_t **res);
static int jffs2_link(vnode_t *dir, const char *name, vnode_t *vnode);
static int jffs2_stat(vnode_t *vnode, struct stat *st);
static int jffs2_unlink(vnode_t *dir, const char *name);

static int jffs2_symlink(vnode_t *dir, const char *name, const char *ref);
static int jffs2_mkdir(vnode_t *dir, const char *name, int mode);
static int jffs2_rmdir(vnode_t *dir, const char *name);
static int jffs2_mknod(vnode_t *dir, const char *name, unsigned int type, dev_t dev);
static int jffs2_readlink(vnode_t *vnode, char *buff, unsigned int size);
static int jffs2_readdir(vnode_t *vnode, offs_t offs, dirent_t *dirent, unsigned int count);
static int jffs2_release(vnode_t *vnode);

static int jffs2_read(file_t* file, offs_t offs, char *buff, unsigned int len);
static int jffs2_write(file_t* file, offs_t offs, char *buff, unsigned int len);
static int jffs2_truncate(vnode_t *vnode, unsigned int size);
static int jffs2_fsync(file_t* file);
static int jffs2_setattr(vnode_t *v, vattr_t *attr);

static const vnode_ops_t jffs2_vops = {
	.create = jffs2_create,
	.mknod  = jffs2_mknod,
	.lookup = jffs2_lookup,

	.link   = jffs2_link,
	.stat   = jffs2_stat,
	.unlink = jffs2_unlink,
 
	.symlink  = jffs2_symlink,
	.readlink = jffs2_readlink,

	.mkdir    = jffs2_mkdir,
	.rmdir    = jffs2_rmdir,
	.readdir  = jffs2_readdir,

    .setattr  = jffs2_setattr,

	.release = jffs2_release,
};

static const file_ops_t jffs2_fops = {
    .read     = jffs2_read,
    .write    = jffs2_write,
    .truncate = jffs2_truncate,
    .fsync    = jffs2_fsync,
};


//TODO set st_blksize and st_blocks
static int jffs2_stat(vnode_t *vnode, struct stat *st)
{
	struct jffs2_inode_info *vi = JFFS2_INODE_INFO(vnode);
	struct jffs2_full_dirent *fd;
	os_priv_data *osPriv = JFFS2_SB_INFO(vnode->sb)->os_priv;

	st->st_dev = osPriv->dev;

	if (vnode->type == vnodeDirectory) {
		st->st_nlink = 2;
		mutex_lock(&vi->sem);
		for (fd = vi->dents; fd; fd = fd->next)
			if (fd->type == DT_DIR)
				st->st_nlink++;
		mutex_unlock(&vi->sem);
	} else {
		mutex_lock(&vi->sem);
		st->st_nlink = vi->inocache->pino_nlink;
		mutex_unlock(&vi->sem);
	}
	return EOK;
}


#define alignTo4(no) ((((no) + 3) / 4) * 4)
static int jffs2_readdir(vnode_t *dir, offs_t offs, dirent_t *dirent, unsigned int len)
{
	struct jffs2_inode_info *di = JFFS2_INODE_INFO(dir);
	struct jffs2_full_dirent *fd;
	offs_t diroffs = 0;
	unsigned int dirsize, lenLeft = len;
	
	if (dir == NULL)
		return -EINVAL;
	if (dir->type != vnodeDirectory)
		return -EINVAL;
	
	mutex_lock(&di->sem);
	for (fd = di->dents; fd; fd = fd->next, diroffs += dirsize) {
		dirsize = alignTo4(strlen(fd->name) + 1 + sizeof(dirent_t));
		if (diroffs < offs)
			continue;
		if (dirsize > lenLeft)
			break;
		dirent->d_ino = fd->ino;
		dirent->d_off = diroffs;
		dirent->d_reclen = dirsize;
		strcpy(&(dirent->d_name[0]), fd->name);

		dirent = (dirent_t *)(((void *)dirent) + dirsize);
		lenLeft -= dirsize;
	}
	mutex_unlock(&di->sem);
	return len - lenLeft;
}


void jffs2_gc_release_inode(struct jffs2_sb_info *c, struct jffs2_inode_info *f)
{
	vnode_put(OFNI_EDONI_2SFFJ(f));
}


vnode_t *jffs2_ilookup(superblock_t *sb, u32 ino)
{
	return vnode_getExisting(sb, ino);
}


static int jffs2_truncate(vnode_t *v, unsigned int size)
{
	struct jffs2_inode_info *vi = JFFS2_INODE_INFO(v);
	struct jffs2_sb_info *c = JFFS2_SB_INFO(v->sb);
	struct jffs2_raw_inode *ri;
	int ret;
	u32 now;

	if (v == NULL)
		return -EINVAL;
	if (v->type != vnodeFile)
		return -EINVAL;
	if (v->size < size)
		return -EINVAL;
	if (v->size == size)
		return EOK;

	if ((ri = jffs2_alloc_raw_inode()) == NULL)
		return -ENOMEM;

	now = getNow();
	ri->uid = cpu_to_je16(v->uid);
	ri->gid = cpu_to_je16(v->gid);
	ri->mode = cpu_to_jemode(v->mode);
	ri->isize = cpu_to_je32(size);
	ri->atime = cpu_to_je32(now);
	ri->mtime = cpu_to_je32(now);
	ri->ctime = cpu_to_je32(now);

	if ((ret = jffs2_do_change_meta(c, vi, ri, NULL, 0)) != 0) {
		jffs2_free_raw_inode(ri);
		return ret;
	}

	jffs2_truncate_fragtree(c, &vi->fragtree, size);
	v->size = size;
	v->mtime = v->ctime = v->atime = jffs2_to_os_time(now);

	mutex_unlock(&vi->sem);
	jffs2_free_raw_inode(ri);
	return EOK;
}


/* Jffs2 for NOR flash is synchronic */
static int jffs2_fsync(file_t* file)
{
	return EOK;
}


static int jffs2_write(file_t* file, offs_t offs, char *buff, unsigned int len)
{
    vnode_t* v = file->vnode;
	int ret;
	struct jffs2_inode_info *vi;
	struct jffs2_raw_inode *ri;
	struct jffs2_sb_info *c;
	unsigned int writtenlen;
	u32 now;

	if (v == NULL)
		return -EINVAL;
	if (v->type != vnodeFile)
		return -EINVAL;
	if (buff == NULL)
		return -EINVAL;

	vi = JFFS2_INODE_INFO(v);
	c = JFFS2_SB_INFO(v->sb);

	if ((ri = jffs2_alloc_raw_inode()) == NULL)
		return -ENOMEM;

	now = getNow();
	ri->isize = cpu_to_je32(v->size);
	ri->mode = cpu_to_jemode(v->mode);
	ri->uid = cpu_to_je16(v->uid);
	ri->gid = cpu_to_je16(v->gid);
	ri->atime = ri->ctime = ri->mtime = cpu_to_je32(now);

	if (offs > v->size) {
		if ((ret = jffs2_write0_inode_range(c, vi, ri, v->size, offs - v->size)) != 0) {
			jffs2_free_raw_inode(ri);
			return ret;
		}
		v->size = offs;
		v->atime = v->ctime = v->mtime = jffs2_to_os_time(now);
		mutex_unlock(&vi->sem);
	}

	ret = jffs2_write_inode_range(c, vi, ri, buff, offs, len, &writtenlen, &v->mtime, &v->ctime, &v->atime);
	jffs2_free_raw_inode(ri);

	if (v->size < offs + writtenlen)
		v->size = offs + writtenlen;

	if (ret != 0)
		return ret;

	return writtenlen;
}


static int jffs2_read(file_t* file, offs_t offs, char *buff, unsigned int len)
{
    vnode_t* v = file->vnode;
	int ret;
	struct jffs2_inode_info *vi;
	
	if (v == NULL)
		return -EINVAL;
	if (v->type != vnodeFile)
		return -EINVAL;
	if (buff == NULL)
		return -EINVAL;

	if (offs > v->size)
		return 0;
	if (offs + len > v->size)
		len = v->size - offs;
	
	vi = JFFS2_INODE_INFO(v);
	mutex_lock(&vi->sem);
	ret = jffs2_read_inode_range(JFFS2_SB_INFO(v->sb), vi, buff, offs, len);
	v->atime = jffs2_to_os_time(getNow());
	mutex_unlock(&vi->sem);

	if (ret != 0)
		return ret;

	return len;
}


static int jffs2_lookup(vnode_t *dir, const char *name, vnode_t **res)
{
	struct jffs2_inode_info *di;
	u32 ino;
	u32 namelen;
	int ret;

	*res = NULL;
	if (dir == NULL)
		return -EINVAL;
	if (dir->type != vnodeDirectory)
		return -EINVAL;
	if (name == NULL)
		return -EINVAL;

	namelen = strnlen(name, JFFS2_MAX_NAME_LEN);
	if (name[namelen] != 0)
		return -ENAMETOOLONG;

	di = JFFS2_INODE_INFO(dir);
	mutex_lock(&di->sem);
	if (!main_strcmp(name, ".."))
		ino = di->inocache->pino_nlink;
	else
		ino = jffs2_dir_get_ino(di, name, namelen);
	dir->atime = jffs2_to_os_time(getNow());
	mutex_unlock(&di->sem);

	if (ino != 0) {
		if (IS_ERR(*res = jffs2_iget(dir->sb, ino))) {
			ret = PTR_ERR(*res);
			*res = NULL;
			return ret;
		} else {
            vnode_setDbgName(*res, name);
			return EOK;
        }
	}
	return -ENOENT;
}


vnode_t *jffs2_iget(superblock_t *sb, u32 ino)
{
	vnode_t *v;
	struct jffs2_inode_info *vi;
	struct jffs2_sb_info *c;
	struct jffs2_raw_inode ri;
	int ret;

	if ((vi = jffs2_alloc_inode_info()) == NULL)
		return ERR_PTR(ENOMEM);

	if ((v = vnode_getWithPriv(sb, ino, vi)) == NULL) {
		jffs2_free_inode_info(vi);
		return ERR_PTR(ENOMEM);
	}

	if (v->fs_priv != vi) {
		jffs2_free_inode_info(vi);
	}

	vi = JFFS2_INODE_INFO(v);
	mutex_lock(&vi->sem);
	if (v->type != 128) {
		/* v->type is a valid value, so v structure is filled with data */
		mutex_unlock(&vi->sem);
		return v;
	}
	
	c = JFFS2_SB_INFO(sb);

	if ((ret = jffs2_do_read_inode(c, vi, ino, &ri)) != 0) {
		v->flags = VNODE_RELEASE_EARLY;
		mutex_unlock(&vi->sem);
		vnode_put(v);
		return ERR_PTR(ret);
	}

	v->mode = jemode_to_cpu(ri.mode);
	v->uid  = je16_to_cpu(ri.uid);
	v->gid  = je16_to_cpu(ri.gid);
	v->size = je32_to_cpu(ri.isize);
	v->atime = jffs2_to_os_time(je32_to_cpu(ri.atime));
	v->mtime = jffs2_to_os_time(je32_to_cpu(ri.mtime));
	v->ctime = jffs2_to_os_time(je32_to_cpu(ri.ctime));
	vi->vfs_inode = v;

	switch (v->mode & S_IFMT) {
		case S_IFDIR:
			v->type = vnodeDirectory;
			break;
		case S_IFREG:
			v->type = vnodeFile;
            v->fops = &jffs2_fops;
			break;
		case S_IFBLK:
		case S_IFCHR:
			if ((ret = jffs2_read_dnode(c, vi, vi->metadata, (char *)&v->dev, 0, vi->metadata->size)) != 0) {
				v->flags = VNODE_RELEASE_EARLY;
				mutex_unlock(&vi->sem);
				vnode_put(v);
				return ERR_PTR(ret);
			}
			v->type = vnodeDevice;
			break;
		case S_IFSOCK:
			v->type = vnodeSocket;
			break;
		case S_IFLNK:
			v->type = vnodeSymlink;
			break;
		case S_IFIFO:
            v->type = vnodePipe;
            break;
		default:
			break;
	}

	mutex_unlock(&vi->sem);
	return v;
}


static int jffs2_link(vnode_t *dir, const char *name, vnode_t *v)
{
	int ret; 
	struct jffs2_inode_info *vi, *di;
	u32 namelen;
	u32 now;

	if (dir == NULL)
		return -EINVAL;
	if (dir->type != vnodeDirectory)
		return -EINVAL;
	if (name == NULL)
		return -EINVAL;
	if (v == NULL)
		return -EINVAL;
	if (v->type == vnodeDirectory)
		return -EINVAL;
	if (v->sb != dir->sb)
		return -EINVAL;

	namelen = strnlen(name, JFFS2_MAX_NAME_LEN);
	if (name[namelen] != 0)
		return -ENAMETOOLONG;

	vi = JFFS2_INODE_INFO(v);
	di = JFFS2_INODE_INFO(dir);
	proc_semaphoreDown(&v->mutex);
	now = getNow();
	
	if ((ret = jffs2_do_link(JFFS2_SB_INFO(dir->sb), JFFS2_INODE_INFO(dir),
			v->id, os_mode_to_type(v->mode), name, namelen, now)) != 0) {
		mutex_unlock(&di->sem);
		proc_semaphoreUp(&v->mutex);
		return ret;
	}

	dir->atime = dir->mtime = dir->ctime = jffs2_to_os_time(now);
	mutex_unlock(&di->sem);
	mutex_lock(&vi->sem);
	vi->inocache->pino_nlink++;
	mutex_unlock(&vi->sem);
	proc_semaphoreUp(&v->mutex);

	return ret;
}


static int jffs2_fillVnode(vnode_t *v, struct jffs2_inode_info *vi, struct jffs2_raw_inode *ri,
							const char *data, unsigned int datasize)
{
	mutex_lock(&vi->sem);
	v->mode = jemode_to_cpu(ri->mode);
	v->uid  = je16_to_cpu(ri->uid);
	v->gid  = je16_to_cpu(ri->gid);
	v->size = je32_to_cpu(ri->isize);
	v->atime = jffs2_to_os_time(je32_to_cpu(ri->atime));
	v->mtime = jffs2_to_os_time(je32_to_cpu(ri->mtime));
	v->ctime = jffs2_to_os_time(je32_to_cpu(ri->ctime));
	
	v->fs_priv = vi;
	switch (v->mode & S_IFMT) {
		case S_IFDIR:
			v->type = vnodeDirectory;
			break;
		case S_IFREG:
			v->type = vnodeFile;
            v->fops = &jffs2_fops;
			break;
		case S_IFBLK:
		case S_IFCHR:
			v->type = vnodeDevice;
			v->dev = *(dev_t *)data;
			break;
		case S_IFSOCK:
			v->type = vnodeSocket;
			break;
		case S_IFLNK:
			if ((vi->target = vm_kmalloc(datasize + 1)) == NULL) {
				mutex_unlock(&vi->sem);
				return -ENOMEM;
			}
			strcpy(vi->target, data);
			v->type = vnodeSymlink;
			break;
		case S_IFIFO:
            v->type = vnodePipe;
            break;
		default:
			break;
	}
	mutex_unlock(&vi->sem);
	return EOK;
}


static int jffs2_mkVnode(vnode_t *dir, const char *name, int mode, const char *data, uint32_t datasize, vnode_t **res)
{
	struct jffs2_raw_inode *ri;
	struct jffs2_inode_info *vi;
	struct jffs2_inode_info *di;
	struct jffs2_sb_info *c;
	vnode_t *v = NULL;
	int ret;
	u32 namelen;
	u32 now;
	
	namelen = strnlen(name, JFFS2_MAX_NAME_LEN);
	if (name[namelen] != 0)
		return -ENAMETOOLONG;

	c = JFFS2_SB_INFO(dir->sb);
	di = JFFS2_INODE_INFO(dir);

	now = getNow();
	mutex_lock(&di->sem);
	ret = jffs2_dir_get_ino(di, name, namelen);
	dir->atime = jffs2_to_os_time(now);
	mutex_unlock(&di->sem);

	if (ret != 0)
		return -EEXIST;

	if ((ri = jffs2_alloc_raw_inode()) == NULL)
		return -ENOMEM;
	
	ri->offset = cpu_to_je32(0);
    ri->uid = cpu_to_je16(proc_getuid(EUID, NULL));
    ri->gid = cpu_to_je16(proc_getgid(EGID, NULL));
	ri->atime = ri->mtime = ri->ctime = cpu_to_je32(now);

	if ((vi = jffs2_alloc_inode_info()) == NULL) {
		jffs2_free_raw_inode(ri);
		return -ENOMEM;
	}

	if ((ret = jffs2_do_new_inode(c, vi, cpu_to_jemode(mode), datasize, ri)) != 0) {
		jffs2_free_inode_info(vi);
		jffs2_free_raw_inode(ri);
		return ret;
	}
	
	if (res != NULL) {
		if ((v = vnode_get(dir->sb, je32_to_cpu(ri->ino))) == NULL) {
			jffs2_do_clear_inode(c, vi);
			jffs2_free_inode_info(vi);
			jffs2_free_raw_inode(ri);
			return -ENOMEM;
		} else {
			vi->vfs_inode = v;
			if ((ret = jffs2_fillVnode(v, vi, ri, data, datasize)) != EOK) {
				v->flags = VNODE_RELEASE_EARLY;
				vnode_put(v);
				jffs2_free_raw_inode(ri);
				return ret;
			}
			vnode_setDbgName(v, name);
		}
	}

	if (S_ISDIR(mode))
		vi->inocache->pino_nlink = di->inocache->ino;

	if ((ret = jffs2_do_create(c, di, vi, ri, name, namelen, data, datasize, os_mode_to_type(mode))) != 0) {
		mutex_unlock(&di->sem);
		if (res != NULL) {
			v->flags = VNODE_RELEASE_EARLY;
			vnode_put(v);
		} else {
			jffs2_do_clear_inode(c, vi);
			jffs2_free_inode_info(vi);
		}
		jffs2_free_raw_inode(ri);
		return ret;
	}

	dir->atime = dir->mtime = dir->ctime = jffs2_to_os_time(now);
	mutex_unlock(&di->sem);
	if (res != NULL) {
		*res = v;
	} else {
		jffs2_do_clear_inode(c, vi);
		jffs2_free_inode_info(vi);
	}
	jffs2_free_raw_inode(ri);

	return EOK;
}


static int jffs2_create(vnode_t *dir, const char *name, int mode, vnode_t **res)
{
	if (dir == NULL)
		return -EINVAL;
	if (dir->type != vnodeDirectory)
		return -EINVAL;
	if (name == NULL)
		return -EINVAL;

	mode = (mode & ~S_IFMT) | S_IFREG;

	return jffs2_mkVnode(dir, name, mode, NULL, 0, res);
}


static int jffs2_mknod(vnode_t *dir, const char *name, unsigned int mode, dev_t dev)
{
	if (dir == NULL)
		return -EINVAL;
	if (dir->type != vnodeDirectory)
		return -EINVAL;
	if (name == NULL)
		return -EINVAL;

    if (S_ISCHR(mode) || S_ISBLK(mode) || S_ISFIFO(mode)) {
        mode &= (S_IFMT | S_IRWXUGO);
    } else {
        return -EINVAL;
    }

	return jffs2_mkVnode(dir, name, mode, (char *)&dev, sizeof(dev), NULL);
}


static int jffs2_mkdir(vnode_t *dir, const char *name, int mode)
{
	if (dir == NULL)
		return -EINVAL;
	if (dir->type != vnodeDirectory)
		return -EINVAL;
	if (name == NULL)
		return -EINVAL;

	mode = (mode & ~S_IFMT) | S_IFDIR;

	return jffs2_mkVnode(dir, name, mode, NULL, 0, NULL);
}


static int jffs2_symlink(vnode_t *dir, const char *name, const char *ref)
{
	u32 reflen;

	if (dir == NULL)
		return -EINVAL;
	if (dir->type != vnodeDirectory)
		return -EINVAL;
	if (name == NULL)
		return -EINVAL;
	if (ref == NULL)
		return -EINVAL;

	reflen = strnlen(ref, JFFS2_MAX_SYMLINK_LEN);
	if (ref[reflen] != 0)
		return -ENAMETOOLONG;

	return jffs2_mkVnode(dir, name, S_IFLNK | S_IRWXUGO, ref, reflen, NULL);
}


static int jffs2_unlink(vnode_t *dir, const char *name)
{
	int ret; 
	vnode_t *v;
	u32 namelen;

	if (dir == NULL)
		return -EINVAL;
	if (dir->type != vnodeDirectory)
		return -EINVAL;
	if (name == NULL)
		return -EINVAL;

	namelen = strnlen(name, JFFS2_MAX_NAME_LEN);
	if (name[namelen] != 0)
		return -ENAMETOOLONG;

	if ((ret = jffs2_lookup(dir, name, &v)) != EOK)
		return ret;
	if (v == NULL)
		return -ENOENT;

	if (v->type == vnodeDirectory) {
		vnode_put(v);
		return -EINVAL;
	}

	proc_semaphoreDown(&v->mutex);
	ret = jffs2_do_unlink(JFFS2_SB_INFO(dir->sb), JFFS2_INODE_INFO(dir),
		name, namelen, JFFS2_INODE_INFO(v), getNow(), &dir->mtime, &dir->ctime, &dir->atime);

	if ((JFFS2_INODE_INFO(v)->inocache == NULL) || (JFFS2_INODE_INFO(v)->inocache->pino_nlink == 0))
		v->flags |= VNODE_DELETED;

	proc_semaphoreUp(&v->mutex);
	vnode_put(v);

	return ret;
}


static int jffs2_rmdir(vnode_t *dir, const char *name)
{
	int ret; 
	vnode_t *v;
	u32 namelen;
	u32 now;
	struct jffs2_inode_info *vi;

	if (dir == NULL)
		return -EINVAL;
	if (dir->fs_priv == NULL)
		return -EINVAL;
	if (dir->type != vnodeDirectory)
		return -EINVAL;
	if (name == NULL)
		return -EINVAL;

	namelen = strnlen(name, JFFS2_MAX_NAME_LEN);
	if (name[namelen] != 0)
		return -ENAMETOOLONG;

	if ((ret = jffs2_lookup(dir, name, &v)) != EOK)
		return ret;

	if (v->type != vnodeDirectory) {
		vnode_put(v);
		return -EINVAL;
	}

	proc_semaphoreDown(&v->mutex);
	vi = JFFS2_INODE_INFO(v);
	mutex_lock(&vi->sem);
	now = getNow();

	ret = jffs2_dir_is_empty(vi);
	v->atime = jffs2_to_os_time(now);
	mutex_unlock(&vi->sem);
	if (ret == 0) {
		vnode_put(v);
		return -ENOTEMPTY;
	}

	if ((ret = jffs2_do_unlink(JFFS2_SB_INFO(dir->sb), JFFS2_INODE_INFO(dir),
		name, namelen, JFFS2_INODE_INFO(v), now, &dir->mtime, &dir->ctime, &dir->atime)) == 0)
		v->flags |= VNODE_DELETED;

	proc_semaphoreUp(&v->mutex);
	vnode_put(v);

	return ret;
}


static int jffs2_readlink(vnode_t *v, char *buff, unsigned int size)
{
	struct jffs2_inode_info *vi;

	if (v == NULL)
		return -EINVAL;
	if (v->fs_priv == NULL)
		return -EINVAL;
	if (buff == NULL)
		return -EINVAL;

	vi = JFFS2_INODE_INFO(v);
	mutex_lock(&vi->sem);
	strncpy(buff, vi->target, size);
	v->atime = getNow();
	mutex_unlock(&vi->sem);

	return v->size;
}


static int jffs2_release(vnode_t *vnode)
{
	if (vnode == NULL)
		return -EINVAL;

	if (vnode->fs_priv != NULL) {
		jffs2_do_clear_inode(JFFS2_SB_INFO(vnode->sb), JFFS2_INODE_INFO(vnode));
		jffs2_free_inode_info(vnode->fs_priv);
	}
	return EOK;
}


static int jffs2_setattr(vnode_t *v, vattr_t *attr)
{
	struct jffs2_inode_info *vi = JFFS2_INODE_INFO(v);
	struct jffs2_sb_info *c = JFFS2_SB_INFO(v->sb);
	struct jffs2_raw_inode *ri;
	int ret;
	u32 now;

	if (v == NULL)
		return -EINVAL;
	if(v->uid == attr->uid && v->gid == attr->gid && v->mode == attr->mode)
		return EOK;

	if ((ri = jffs2_alloc_raw_inode()) == NULL)
		return -ENOMEM;

	now = getNow();
	ri->uid = cpu_to_je16(attr->uid);
	ri->gid = cpu_to_je16(attr->gid);
	ri->mode = cpu_to_jemode(attr->mode);
	ri->isize = cpu_to_je32(v->size);
	ri->atime = cpu_to_je32(now);
	ri->mtime = cpu_to_je32(now);
	ri->ctime = cpu_to_je32(now);

	if ((ret = jffs2_do_change_meta(c, vi, ri, NULL, 0)) != 0) {
		jffs2_free_raw_inode(ri);
		return ret;
	}

	v->mtime = v->ctime = v->atime = jffs2_to_os_time(now);
	
	v->uid = attr->uid;
	v->gid = attr->gid;
	v->mode = attr->mode;

	mutex_unlock(&vi->sem);
	jffs2_free_raw_inode(ri);
	return EOK;
}


void jffs2_init_opts(jffs2_opt_t *mo)
{
	if (mo == NULL)
		return;
	mo->dev = MAKEDEV(MAJOR_MTD, 0);
	mo->partitionBegin = 512 * 1024;
	mo->partitionSize = 0;
	mo->mode = JFFS2_COMPR_MODE_DEFAULT | JFFS2_MODE_WRITABLE;
	mo->rpSize = 0;
}


int jffs2_readsuper(void *opt, superblock_t **superblock)
{
	int ret, regNo;
	struct jffs2_sb_info *c;
	superblock_t *sb;
	jffs2_opt_t mo;
	struct jffs2_mount_opts jffs2mo;
	flash_cfi_t cfi;
	os_priv_data *osPriv;
	size_t sectorSize;
	size_t flashSize;
	
	if (opt == NULL)
		jffs2_init_opts(&mo);
	else
		mo = *((jffs2_opt_t *)opt);

	if (MAJOR(mo.dev) != MAJOR_MTD)
		return -EINVAL;

	mtd_lock(MINOR(mo.dev));
	ret = _mtd_getCfi(MINOR(mo.dev), &cfi);
	mtd_unlock(MINOR(mo.dev));

	if (ret != EOK)
		return ret;

	if ((sb = (superblock_t *)vm_kmalloc(sizeof(superblock_t))) == NULL)
		return -ENOMEM;

	if ((osPriv = (os_priv_data *)vm_kmalloc(sizeof(os_priv_data))) == NULL) {
		vm_kfree(sb);
		return -ENOMEM;
	}

	if ((c = jffs2_alloc_sb_info()) == NULL) {
		vm_kfree(osPriv);
		vm_kfree(sb);
		return -ENOMEM;
	}

	/* Compute uniform sector size */
	c->sector_size = 0;
	for (regNo = 0; regNo < cfi.regionsCount; regNo++) {
		if (cfi.region[regNo].blockSize == 0)
			sectorSize = 128;
		else
			sectorSize = cfi.region[regNo].blockSize * 256;
		c->sector_size = (c->sector_size < sectorSize) ? sectorSize : c->sector_size;
	}
	flashSize = 1 << cfi.chipSize;

	/* Compute and set where to mount jffs2 */
	if ((mo.partitionBegin > flashSize - c->sector_size)
		|| ((mo.partitionSize != 0) && (mo.partitionSize < c->sector_size))) {
		jffs2_free_sb_info(c);
		vm_kfree(osPriv);
		vm_kfree(sb);
		return -EINVAL;
	}
		
	if ((mo.partitionSize != 0) && (mo.partitionSize + mo.partitionBegin > flashSize)) {
		main_printf(ATTR_INFO, "Jffs2 partition size exceedes flash size. Decreasing partition size.\n");
		mo.partitionSize = flashSize - mo.partitionBegin;
	}

	osPriv->partitionBegin = mo.partitionBegin;
	if ((mo.partitionSize != 0) && (mo.partitionSize <= 1 << cfi.chipSize))
		c->flash_size = mo.partitionSize;
	else
		c->flash_size = flashSize - mo.partitionBegin;

	if (osPriv->partitionBegin % c->sector_size != 0) {
		main_printf(ATTR_INFO, "Jffs2 partition begin not aligned to erase sector. Increasing partition begin.\n");
		c->flash_size -=  c->sector_size - (osPriv->partitionBegin % c->sector_size);
		osPriv->partitionBegin += c->sector_size - (osPriv->partitionBegin % c->sector_size);
	}
	if (c->flash_size % c->sector_size != 0) {
		main_printf(ATTR_INFO, "Jffs2 partition size not aligned to erase sector. Decreasing partition size.\n");
		c->flash_size -=  c->flash_size % c->sector_size;
	}
	osPriv->dev = mo.dev;

	/* Set other jffs2 configuration options */
	if ((mo.mode & JFFS2_COMPR_MODE_MASK) == JFFS2_COMPR_MODE_DEFAULT)
		jffs2mo.override_compr = 0;
	else {
		jffs2mo.override_compr = 1;
		jffs2mo.compr = mo.mode & JFFS2_COMPR_MODE_MASK;
	}
	jffs2mo.rp_size = mo.rpSize;
	if ((mo.mode & JFFS2_MODE_MASK) == JFFS2_MODE_READONLY)
		osPriv->isReadonly = 1;
	else
		osPriv->isReadonly = 0;

	if ((ret = jffs2_init_sb_info(c, &jffs2mo))) {
		jffs2_free_sb_info(c);
		vm_kfree(osPriv);
		vm_kfree(sb);
		return ret;
	}

	sb->vops = &jffs2_vops;
	sb->priv = c;
	osPriv->osSb = sb;
	c->os_priv = osPriv;

	jffs2_init_xattr_subsystem(c);

	if ((ret = jffs2_flash_setup(c)) != 0) {
		jffs2_clear_xattr_subsystem(c);
		jffs2_free_sb_info(c);
		vm_kfree(osPriv);
		vm_kfree(sb);
		return ret;
	}

	if ((ret = jffs2_do_mount_fs(c)) != 0) {
		jffs2_flash_cleanup(c);
		jffs2_clear_xattr_subsystem(c);
		jffs2_free_sb_info(c);
		vm_kfree(osPriv);
		vm_kfree(sb);
		return ret;
	}

	if (IS_ERR(sb->root = jffs2_iget(sb, 1))) {
		ret = PTR_ERR(sb->root);
		jffs2_free_ino_caches(c);
		jffs2_free_raw_node_refs(c);
		free(c->blocks);
		jffs2_flash_cleanup(c);
		jffs2_clear_xattr_subsystem(c);
		jffs2_free_sb_info(c);
		vm_kfree(osPriv);
		vm_kfree(sb);
		return ret;
	}
	vnode_setDbgName(sb->root, "(jffs2)/");

	if (!jffs2_is_readonly(c) && ((ret = jffs2_start_garbage_collect_thread(c)) != EOK)) {
		main_printf(ATTR_ERROR, "Failed to execute jffs2 garbage collector thread. Filesystem running in read-only mode.");
		osPriv->isReadonly = 1;
	}

	*superblock = sb;
	return EOK;
}


void jffs2_freesuper(superblock_t *sb)
{
	struct jffs2_sb_info *c = sb->priv;

	jffs2_stop_garbage_collect_thread(c);
	vnode_put(sb->root);

	mutex_lock(&c->alloc_sem);
	jffs2_flush_wbuf_pad(c);
	mutex_unlock(&c->alloc_sem);

	jffs2_sum_exit(c);

	jffs2_free_ino_caches(c);
	jffs2_free_raw_node_refs(c);
	free(c->blocks);
	jffs2_flash_cleanup(c);
	jffs2_clear_xattr_subsystem(c);
	jffs2_free_sb_info(c);
	vm_kfree(c->os_priv);
	vm_kfree(sb);

	jffs2_compressors_exit();
	jffs2_destroy_slab_caches();
	return;
}


int jffs2_init(void)
{
	static filesystem_t fs;
	int ret;

	fs.type = TYPE_JFFS2FS;
	fs.readsuper = jffs2_readsuper;

	if ((ret = jffs2_create_slab_caches()) != 0)
		return ret;

	if ((ret = jffs2_compressors_init()) != 0) {
		jffs2_destroy_slab_caches();
		return ret;
	}

	fs_register(&fs);
	return EOK;
}
