/* 
 * %LICENSE%
 */

#define NO_TRACE

#include <hal/if.h>
#include <main/if.h>
#include <fs/if.h>
#include <lib/assert.h>
#include <lib/debug.h>
#include <vm/if.h>
#include <proc/if.h>
#include <net/if.h>
#include "procfs.h"


int procfs_lookup(vnode_t *dir, const char *name, vnode_t **res);
int procfs_readdir(vnode_t *vnode, offs_t offs, dirent_t *dirent, unsigned int count);

int procfs_read(file_t* file, offs_t offs, char *buff, unsigned int len);
int procfs_write(file_t* file, offs_t offs, char *buff, unsigned int len);
int procfs_ioctl(file_t* file, unsigned int cmd, unsigned long arg);
int procfs_open(vnode_t *vnode, file_t* files);
int procfs_fsync(file_t* file);
int procfs_release(vnode_t *vnode);
int procfs_poll(file_t *file, ktime_t timeout, int op);
int procfs_select_poll(file_t *file, unsigned *ready);


static const vnode_ops_t procfs_vops = {
	.lookup = procfs_lookup,
	.readdir = procfs_readdir,
};


static const file_ops_t  procfs_fops = {
  .read = procfs_read,
  .write = procfs_write,
  .open = procfs_open,
  .fsync = procfs_fsync,
  .release = procfs_release,
	.poll = procfs_poll,
	.select_poll = procfs_select_poll,
};


typedef struct{
	int type;
} procfs_node_t;


struct file_desc_s {
	char name[20];

	enum {
		T_PROC = 1,
		T_MEM,
		T_SPINLOCK,
		T_VNODE,
		T_VM_KMALLOC,
		T_INTERRUPTS,
		T_METER_CTRL,
		T_IP,
		T_LOAD
	} type;
};


static const struct file_desc_s files[] = {
  { "proc",       T_PROC },
	{ "mem",        T_MEM },
	{ "spinlock",   T_SPINLOCK },
	{ "vnode",      T_VNODE },
	{ "vm_kmalloc", T_VM_KMALLOC },
	{ "interrupts", T_INTERRUPTS },
	{ "meter_ctrl", T_METER_CTRL },
	{ "ip",         T_IP },
	{ "load",       T_LOAD }
};


int procfs_release(vnode_t *vnode)
{
  if (vnode->file_priv) {
    vm_kfree(vnode->file_priv);
    vnode->file_priv = NULL;
  }

	return EOK;
}


static int dumpMem(char *buff, int len)
{
	int o = 0;
	unsigned allocsz, freesz, kmapAllocated;

	vm_pageStat(&allocsz, &freesz);
	vm_kmapStats(&kmapAllocated);
	o += main_snprintf(buff + o, len - o - 1, "mem: %d/%d KB \nvmKmap: %d pages\n", allocsz / 1024, (allocsz + freesz) / 1024, kmapAllocated);

	return o;
}


static char meterCtrl=0;


int procfs_read(file_t* file, offs_t offs, char *buff, unsigned int len)
{  
	TRACE("");
	vnode_t* vnode = file->vnode;
	
	procfs_node_t *pn= (procfs_node_t*)vnode->file_priv;

	if (offs)
		return 0;

	switch (pn->type){
		case T_PROC:
			proc_procDump(buff, len);
			break;

		case T_SPINLOCK:
			proc_spinlockDump(buff, len);
			break;
		case T_VNODE:
			vnode_dump(buff, len);
			break;
		case T_VM_KMALLOC:
			vm_dumpKmalloc(buff, len);
			break;
		case T_MEM:
			dumpMem(buff, len);
			break;
		case T_INTERRUPTS:
			hal_interuptsDump(buff, len);
			break;
		case T_METER_CTRL:
			buff[0]=meterCtrl;
			meterCtrl=0;
			len=1;
			break;

		case T_IP:
		{
/*			u32 ip = 0;

			if (len < 15)
				return -EINVAL;

      else {
				struct netif *n = netif_find("eth0");

				if (n != NULL)
					ip = n->ip_addr.addr;

        main_snprintf(buff, 16, "%u.%u.%u.%u", ip & 0xFF, 0xFF & (ip >> 8), 0xFF & (ip >> 16), 0xFF & (ip >> 24));
      } */
		}
		break;

		case T_LOAD:
		{
			int idle = proc_getIdle();
			if (len < 4)
				return -EINVAL;

			main_snprintf(buff, 4, "%d%%", 100 - idle);
		}
		break;

		default:
			strcpy(buff, "no info\n");
	}
	int bufflen = strlen(buff);
	if(len>bufflen)
		len=bufflen;
	return len;
}


int procfs_write(file_t* file, offs_t offs, char *buff, unsigned int len)
{	
  vnode_t* vnode = file->vnode;
	procfs_node_t *pn= (procfs_node_t*)vnode->file_priv;
	
	switch(pn->type){
		case T_SPINLOCK:
			proc_spinlockReset();
			break;
		case T_METER_CTRL:
			meterCtrl = buff[0];
			break;
		case T_PROC:
		case T_MEM:
		case T_VNODE:
		case T_VM_KMALLOC:
		case T_IP:
		case T_LOAD:
		default:
			break;
	}

	return len;
}


int procfs_poll(file_t *file, ktime_t timeout, int op)
{
	return EOK;
}


int procfs_select_poll(file_t *file, unsigned *ready)
{
	TRACE("");
	vnode_t* vnode = file->vnode;
	procfs_node_t *pn= (procfs_node_t*)vnode->file_priv;

	switch(pn->type){
		case T_METER_CTRL:
			if(meterCtrl != 0)
				*ready |= FS_READY_READ;
			*ready |= FS_READY_WRITE;
		break;
		case T_PROC:
		case T_SPINLOCK:
		case T_VNODE:
		case T_VM_KMALLOC:
		case T_MEM:
		case T_INTERRUPTS:
		case T_IP:
		case T_LOAD:
			*ready |= FS_READY_READ;
		break;
		default:
		break;
	}
	return EOK;
}

/**
 *
 * \param vnode directory to read
 * \param offs offset in bytes
 * \param dirent output buffer
 * \param count length of buffer in bytes
 *
 * returns bytes written;
 */
#define alignTo4(no) ((((no) + 3) / 4) * 4)
int procfs_readdir(vnode_t *dir, offs_t offs, dirent_t *dirent, unsigned int len)
{
	offs_t diroffs = 0;
	unsigned int dirsize, lenLeft = len;
	int file;

	if (dir == NULL)
		return -EINVAL;
	if (dir->type != vnodeDirectory) 
		return -ENOTDIR;

	for (file = 0; file < sizeof(files)/sizeof(files[0]); file++, diroffs += dirsize) {
		dirsize = alignTo4(strlen(files[file].name) + 1 + sizeof(dirent_t));
		if (diroffs < offs)
			continue;
		if (dirsize > lenLeft)
			break;
		dirent->d_ino = file + 3;
		dirent->d_off = diroffs;
		dirent->d_reclen = dirsize;
		strcpy(&(dirent->d_name[0]), files[file].name);

		dirent = (dirent_t *)(((void *)dirent) + dirsize);
		lenLeft -= dirsize;
	}
	return len - lenLeft;
}


int procfs_lookup(vnode_t *dir, const char *name, vnode_t **res)
{
	int i;
	*res = NULL;
	vnode_t *vnode;
	procfs_node_t *pn=NULL;
	u64 type=0;
	TRACE("begin name '%s'", name);

	for (i = 0; i < sizeof(files)/sizeof(files[0]); i++)
		if (strcmp(name, files[i].name) == 0)
			type = files[i].type;
	if (!type)
		return -ENOENT;

	vnode = vnode_get(dir->sb, type + 1);
	if (!vnode)
		return -ENOMEM;

	if (vnode->file_priv == NULL){
		vnode->file_priv = vm_kmalloc(sizeof(procfs_node_t));
		pn = (procfs_node_t*) vnode->file_priv;
		pn->type = type;
		vnode->type = vnodeFile;
		vnode->fops = &procfs_fops;
		vnode->uid = 0;
		vnode->mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	}
	pn = (procfs_node_t*) vnode->file_priv;
	assert(pn->type == type);

	*res = vnode;

	return EOK;
}


int procfs_open(vnode_t *vnode, file_t* file)
{
	return EOK;
}


int procfs_fsync(file_t* file)
{
	return EOK;
}


int procfs_readsuper(void *opt, superblock_t **superblock)
{
	superblock_t *sb;

	if ((sb = (superblock_t *)vm_kmalloc(sizeof(superblock_t))) == NULL)
		return -ENOMEM;
	sb->priv = NULL; // sb->piriv used as node counter
	sb->vops = &procfs_vops;

	sb->root = vnode_get(sb, (u32)++sb->priv);

	sb->root->type = vnodeDirectory;
	sb->root->dev = 0;
	sb->root->mode = 0;
	sb->root->uid = 0;
	sb->root->gid = 0;
	sb->root->size = 0;
	vnode_setDbgName(sb->root, "-procfs-root-");

	sb->root->file_priv = NULL;

	*superblock = sb;
	TRACE("done");
	return EOK;
}


int procfs_init(void)
{
	static filesystem_t procfs;

	procfs.type = TYPE_PROCFS;
	procfs.readsuper = procfs_readsuper;

	fs_register(&procfs);

	return EOK;
}
