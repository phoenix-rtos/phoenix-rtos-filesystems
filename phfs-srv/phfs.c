/* 
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * Remote PHoenix FileSystem
 *
 * Copyright 2012-2015 Phoenix Systems
 * Copyright 2005-2006 Pawel Pisarczyk
 * Author: Pawel Pisarczyk
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <hal/if.h>
#include <main/if.h>
#include <fs/if.h>
#include "phfs.h"
#include "phfs_msg.h"
#include <include/stat.h>
#include <vm/if.h>


typedef struct _phfs_io_t {
	u32 handle;
	u32 pos;
	u32 len;
	u8 data[PHFS_MSG_MAXLEN - 3 * sizeof(u32)];
} phfs_io_t;


static int phfs_create(vnode_t *dir, const char *name, int mode, vnode_t **res);
static int phfs_lookup(vnode_t *dir, const char *name, vnode_t **res);
static int phfs_link(vnode_t *dir, const char *name, vnode_t *vnode);
static int phfs_unlink(vnode_t *dir, const char *name);
static int phfs_symlink(vnode_t *dir, const char *name, const char *ref);
static int phfs_mkdir(vnode_t *dir, const char *name, int mode);
static int phfs_rmdir(vnode_t *dir, const char *name);
static int phfs_mknod(vnode_t *dir, const char *name, unsigned int type, dev_t dev);
static int phfs_readlink(vnode_t *vnode, char *buf, unsigned int size);
static int phfs_readdir(vnode_t *vnode, offs_t offs, dirent_t *dirent, unsigned int count);

static int phfs_read(file_t* file, offs_t offs, char *buff, unsigned int len);
static int phfs_write(file_t* file, offs_t offs, char *buff, unsigned int len);
static int phfs_poll(file_t* file, ktime_t timeout, int op);
static int phfs_ioctl(file_t* file, unsigned int cmd, unsigned long arg);
static int phfs_open(vnode_t *vnode, file_t* file);
static int phfs_fsync(file_t* file);


static const vnode_ops_t phfs_vops = {
  .create = phfs_create,
  .lookup = phfs_lookup,
	.link = phfs_link,
	.unlink = phfs_unlink,
	.symlink = phfs_symlink,
	.mkdir = phfs_mkdir,
	.rmdir = phfs_rmdir,
	.mknod = phfs_mknod,
	.readlink =phfs_readlink,
	.readdir = phfs_readdir,
};

static const file_ops_t phfs_fops = {
  .read = phfs_read,
  .write = phfs_write,
  .poll = phfs_poll,
  .ioctl = phfs_ioctl,
  .open = phfs_open,
  .fsync = phfs_fsync,
};


static int phfs_create(vnode_t *dir, const char *name, int mode, vnode_t **res)
{
	return -ENXIO;
}


int phfs_lookup(vnode_t *dir, const char *name, vnode_t **res)
{
	phfs_priv_t *priv = (phfs_priv_t *)dir->sb->priv;
	phfs_io_t *io;
	phfs_msg_t smsg, rmsg;
	u16 l,hdrsz;
	u32 h;
	vnode_t *vnode;
	int rc;
	struct stat st;

	l = min(main_strlen(name)+1, PHFS_MSG_MAXLEN - sizeof(u32) - 1);
	
	/* rw mode */
	smsg.data[0] = 1;
	smsg.data[1] = 0;
	smsg.data[2] = 0;
	smsg.data[3] = 0;
	
	hal_memcpy(&smsg.data[sizeof(u32)], (char *)name, l);
	l += sizeof(u32);
	smsg.data[l] = 0;

	phfs_msg_settype(&smsg, PHFS_OPEN);
	phfs_msg_setlen(&smsg, l);

	proc_semaphoreDown(&priv->mutex);
	rc = phfs_msg_send(priv, &smsg, &rmsg);
	proc_semaphoreUp(&priv->mutex);

	if (rc < 0)
		return -EIO;

	if (phfs_msg_gettype(&rmsg) != PHFS_OPEN)
		return -EPROTO;

	if (phfs_msg_getlen(&rmsg) != sizeof(u32))
		return -EPROTO;

  hal_memcpy(&h, rmsg.data, 4);
	if (!h)
		return -EIO;

	if ((vnode = vnode_get(dir->sb, h)) == NULL)
		return -EIO;

	hal_memcpy(smsg.data, &h, 4);

	l = sizeof(u32);
	smsg.data[l] = 0;

	phfs_msg_settype(&smsg, PHFS_FSTAT);
	phfs_msg_setlen(&smsg, l);

	proc_semaphoreDown(&priv->mutex);
	rc = phfs_msg_send(priv, &smsg, &rmsg);
	proc_semaphoreUp(&priv->mutex);
	if( rc < 0 )
		return -EIO;

	io = (phfs_io_t *)rmsg.data;
	hdrsz = (u16)((u32)io->data - (u32)io);

	if (phfs_msg_gettype(&rmsg) != PHFS_FSTAT)
		return -EPROTO;
	if (phfs_msg_getlen(&rmsg) <hdrsz)
		return -EPROTO;

	if(io->len!= phfs_msg_getlen(&rmsg) - hdrsz)
		return -EIO;
	hal_memcpy(&st, io->data,io->len);

	/* Prepare vnode for cache */

	vnode->size=st.st_size;
	vnode->gid=st.st_gid;
	vnode->uid=st.st_uid;
	vnode->mode=st.st_mode;

	vnode->type = vnodeFile;
	vnode->fops = &phfs_fops;
	*res = vnode;

	return EOK;
}


static int phfs_link(vnode_t *dir, const char *name, vnode_t *vnode)
{
	return -ENXIO;
}


static int phfs_unlink(vnode_t *dir, const char *name)
{
	return -ENXIO;
}


static int phfs_symlink(vnode_t *dir, const char *name, const char *ref)
{
	return -ENXIO;
}


static int phfs_mkdir(vnode_t *dir, const char *name, int mode)
{
	return -ENXIO;
}


static int phfs_rmdir(vnode_t *dir, const char *name)
{
	return -ENXIO;
}


static int phfs_mknod(vnode_t *dir, const char *name, unsigned int type, dev_t dev)
{
	return -ENXIO;
}


static int phfs_readlink(vnode_t *vnode, char *buf, unsigned int size)
{
	return -ENOENT;
}


static int phfs_read(file_t* file, offs_t offs, char *buff, unsigned int len)
{  
    vnode_t *vnode = file->vnode;
	phfs_priv_t *priv = (phfs_priv_t *)vnode->sb->priv;
	phfs_msg_t smsg, rmsg;
	phfs_io_t *io;
	u16 hdrsz;
	u32 l=0;
	int rc,i=0,tmplen=len ;
	int maxdatalenght;//500

	io = (phfs_io_t *)smsg.data;
	hdrsz = (u16)((u32)io->data - (u32)io);
	maxdatalenght= PHFS_MSG_MAXLEN - hdrsz;

	if ((vnode->type != vnodeFile)||(len == 0))
		return -EINVAL;
	while(tmplen>0){

		if(tmplen>=maxdatalenght)
			l=maxdatalenght;
		else
			l= tmplen % maxdatalenght;

		io = (phfs_io_t *)smsg.data;
		io->handle = vnode->id;
		io->pos = (offs & 0xffffffff)+i*maxdatalenght;
		io->len=l;
		phfs_msg_settype(&smsg, PHFS_READ);
		phfs_msg_setlen(&smsg, hdrsz);

		proc_semaphoreDown(&priv->mutex);
		rc = phfs_msg_send(priv, &smsg, &rmsg);
		proc_semaphoreUp(&priv->mutex);
		if (rc < 0 )
			return -EIO;

		if (phfs_msg_gettype(&rmsg) != PHFS_READ)
			return -EPROTO;
		if (phfs_msg_getlen(&rmsg) < hdrsz)
			return -EPROTO;

		io = (phfs_io_t *)rmsg.data;
		if ((long)io->len < 0)
			return -EIO;

		if(l>io->len){
			memcpy(buff+(i*maxdatalenght), io->data, io->len);
			return i*maxdatalenght+io->len;
		}

		memcpy(buff+(i*maxdatalenght), io->data, l);
		i++;
		tmplen=tmplen-maxdatalenght;
	}

	return (i-1)*maxdatalenght+l;
}


static int phfs_write(file_t* file, offs_t offs, char *buff, unsigned int len)
{
    vnode_t *vnode = file->vnode;
	phfs_priv_t *priv = (phfs_priv_t *)vnode->sb->priv;
	phfs_msg_t smsg, rmsg;
	phfs_io_t *io;
	u16 hdrsz;
	u32 l=0;
	int rc,i=0,tmplen=len ;
	int maxdatalenght;//500

	io = (phfs_io_t *)smsg.data;
	hdrsz = (u16)((u32)io->data - (u32)io);
	maxdatalenght= PHFS_MSG_MAXLEN - hdrsz;
	
	if ((vnode->type != vnodeFile) || (len == 0))
		return -EINVAL;

	while(tmplen>0){

		if(tmplen>=maxdatalenght)
			l=maxdatalenght;
		else
			l= tmplen % maxdatalenght;

		io = (phfs_io_t *)smsg.data;
		io->handle = vnode->id;
		io->pos = (offs & 0xffffffff) + i * maxdatalenght;
		io->len=l;

		memcpy(io->data, buff+(i*maxdatalenght), io->len);
		i++;

		phfs_msg_settype(&smsg, PHFS_WRITE);
		phfs_msg_setlen(&smsg, io->len + hdrsz);
		proc_semaphoreDown(&priv->mutex);


		rc =  phfs_msg_send(priv, &smsg, &rmsg);
		proc_semaphoreUp(&priv->mutex);

		if (rc < 0)
			return -EIO;

		if (phfs_msg_gettype(&rmsg) != PHFS_WRITE)
			return -EPROTO;

		if (phfs_msg_getlen(&rmsg) < hdrsz)
			return -EPROTO;

		io = (phfs_io_t *)rmsg.data;
		if (io->len < 0)
			return -EIO;
		tmplen=tmplen-maxdatalenght;
	}
	return (i - 1) * maxdatalenght + l;
}


static int phfs_readdir(vnode_t* vnode, offs_t offs, dirent_t *dirent, unsigned int count)
{
	return -ENOENT;
}


static int phfs_poll(file_t* file, ktime_t timeout, int op)
{
	return -ENXIO;
}


static int phfs_ioctl(file_t* file, unsigned int cmd, unsigned long arg)
{
	return -ENXIO;
}


static int phfs_open(vnode_t *vnode, file_t* file)
{
	return EOK;
}


static int phfs_fsync(file_t* file)
{
  return EOK;

  vnode_t *vnode = file->vnode;
	phfs_priv_t *priv = (phfs_priv_t *)vnode->sb->priv;
	phfs_msg_t smsg, rmsg;
	u16 l;
	int res, rc;

	hal_memcpy(smsg.data, &vnode->id, 4);
	l = sizeof(u32);

	phfs_msg_settype(&smsg, PHFS_CLOSE);
	phfs_msg_setlen(&smsg, l);

	proc_semaphoreDown(&priv->mutex);
	rc = phfs_msg_send(priv, &smsg, &rmsg);
	proc_semaphoreUp(&priv->mutex);
	if (rc < 0)
		return -EIO;

	if (phfs_msg_gettype(&rmsg) != PHFS_CLOSE)
		return -EPROTO;

	if (phfs_msg_getlen(&rmsg) != sizeof(u32))
		return -EPROTO;

	hal_memcpy(&res, rmsg.data, 4);
	
	return res;
}


static int phfs_readsuper(void *opt, superblock_t **superblock)
{
	phfs_opt_t *phfs_opt = (phfs_opt_t *)opt;
	superblock_t *sb;
	phfs_priv_t *priv;
	phfs_msg_t smsg, rmsg;
	int status;

	if (phfs_opt->magic != 0xaa55a55a) {
		main_printf(ATTR_ERROR, "phfs: Bad magic number in option structure!\n");
		return -EINVAL;
	}

	if ((sb = (superblock_t *)vm_kmalloc(sizeof(superblock_t))) == NULL)
		return -ENOMEM;

	sb->root = vnode_get(sb, 0);
	sb->root->type = vnodeDirectory;
	vnode_setDbgName(sb->root, "-phfs-root-");

	if ((priv = (phfs_priv_t *)vm_kmalloc(sizeof(phfs_priv_t))) == NULL) {
		vm_kfree(sb);
		return -ENOMEM;
	}


	if ((status = phfs_msg_init(priv, opt)) != EOK) {
		vm_kfree(priv);
		vm_kfree(sb);
		return status;
	}

	phfs_msg_settype(&smsg, PHFS_RESET);
	phfs_msg_setlen(&smsg, 0);

	if (phfs_msg_send(priv, &smsg, &rmsg) < 0) {
		priv->terminate(priv);
		vm_kfree(priv);
		vm_kfree(sb);
		return -EIO;
	}

	if (phfs_msg_gettype(&rmsg) != PHFS_RESET) {
		priv->terminate(priv);
		vm_kfree(priv);
		vm_kfree(sb);
		return -EPROTO;
	}

	proc_semaphoreCreate(&priv->mutex, 1);
	sb->priv = (void *)priv;
    sb->vops = &phfs_vops;
	*superblock = sb;
	return EOK;
}


int phfs_init(void)
{
	static filesystem_t phfs;

	phfs.type = TYPE_PHFS;
	phfs.readsuper = phfs_readsuper;

	fs_register(&phfs);
	return EOK;
}
