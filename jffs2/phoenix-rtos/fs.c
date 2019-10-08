/*
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * Jffs2 FileSystem - system specific information.
 *
 * Copyright 2018 Phoenix Systems
 * Author: Kamil Amanowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include "../phoenix-rtos.h"
#include <time.h>
#include "fs.h"


struct timespec current_time(struct inode *inode)
{
	struct timespec t = {0, 0};
	t.tv_sec = time(NULL);
	return t;
}

int setattr_prepare(struct dentry *dentry, struct iattr *iattr)
{
	return 0;
}

void init_special_inode(struct inode *inode, umode_t mode, dev_t dev)
{
	return;
}


void inc_nlink(struct inode *inode)
{
	inode->i_nlink++;
}

void clear_nlink(struct inode *inode)
{
	inode->i_nlink = 0;
}

void set_nlink(struct inode *inode, unsigned int nlink)
{
	inode->i_nlink = nlink;
}

void drop_nlink(struct inode *inode)
{
	if (inode->i_nlink)
		inode->i_nlink--;
}

void ihold(struct inode * inode)
{
	inode->i_count++;
	if (inode->i_count < 2)
		printf("jffs2: ihold #%lu refs < 2\n", inode->i_ino);
}

struct inode *new_inode(struct super_block *sb)
{
	struct inode *inode;

   inode = sb->s_op->alloc_inode(sb);

	if (inode != NULL) {
		inode->i_sb = sb;
		inode->i_count = 1;
		inode->i_mapping = malloc(sizeof(struct address_space));
		inode->i_state = I_NEW;
		mutexCreate(&inode->i_lock);
		init_rwsem(&inode->i_rwsem);
	}
	return inode;
}

void unlock_new_inode(struct inode *inode)
{
	inode->i_state &= ~I_NEW;
	mutexUnlock(inode->i_lock);
}

void iget_failed(struct inode *inode)
{
	make_bad_inode(inode);
	unlock_new_inode(inode);
	/* TODO: check if it is right. maybe refs need clearing */
	iput(inode);
}

struct inode * iget_locked(struct super_block *sb, unsigned long ino)
{
	struct inode *inode = NULL;
	jffs2_object_t *o = object_get(sb->s_part, ino, 1);

	if (o != NULL)
		return o->inode;

	return inode;
}


void iput(struct inode *inode)
{
	object_put(inode->i_sb->s_part, inode->i_ino);
}

/* form linux kernel */
static int bad_file_open(struct inode *inode, struct file *filp)
{
	return -EIO;
}


static const struct file_operations bad_file_ops =
{
		.open			= bad_file_open,
};


static int bad_inode_create (struct inode *dir, struct dentry *dentry,
		umode_t mode, bool excl)
{
	return -EIO;
}

static struct dentry *bad_inode_lookup(struct inode *dir,
			struct dentry *dentry, unsigned int flags)
{
	return ERR_PTR(-EIO);
}

static int bad_inode_link (struct dentry *old_dentry, struct inode *dir,
		struct dentry *dentry)
{
	return -EIO;
}

static int bad_inode_unlink(struct inode *dir, struct dentry *dentry)
{
	return -EIO;
}

static int bad_inode_symlink (struct inode *dir, struct dentry *dentry,
		const char *symname)
{
	return -EIO;
}

static int bad_inode_mkdir(struct inode *dir, struct dentry *dentry,
			umode_t mode)
{
	return -EIO;
}

static int bad_inode_rmdir (struct inode *dir, struct dentry *dentry)
{
	return -EIO;
}

static int bad_inode_mknod (struct inode *dir, struct dentry *dentry,
			umode_t mode, dev_t rdev)
{
	return -EIO;
}

static int bad_inode_rename2(struct inode *old_dir, struct dentry *old_dentry,
			     struct inode *new_dir, struct dentry *new_dentry,
			     unsigned int flags)
{
	return -EIO;
}

static int bad_inode_readlink(struct dentry *dentry, char __user *buffer,
		int buflen)
{
	return -EIO;
}

static int bad_inode_permission(struct inode *inode, int mask)
{
	return -EIO;
}

static int bad_inode_getattr(const struct path *path, struct kstat *stat,
			     uint32_t request_mask, unsigned int query_flags)
{
	return -EIO;
}

static int bad_inode_setattr(struct dentry *direntry, struct iattr *attrs)
{
	return -EIO;
}

static ssize_t bad_inode_listxattr(struct dentry *dentry, char *buffer,
			size_t buffer_size)
{
	return -EIO;
}

static const char *bad_inode_get_link(struct dentry *dentry,
				      struct inode *inode,
				      struct delayed_call *done)
{
	return ERR_PTR(-EIO);
}

static struct posix_acl *bad_inode_get_acl(struct inode *inode, int type)
{
	return ERR_PTR(-EIO);
}


static int bad_inode_update_time(struct inode *inode, struct timespec *time,
				 int flags)
{
	return -EIO;
}

static int bad_inode_atomic_open(struct inode *inode, struct dentry *dentry,
				 struct file *file, unsigned int open_flag,
				 umode_t create_mode, int *opened)
{
	return -EIO;
}

static int bad_inode_tmpfile(struct inode *inode, struct dentry *dentry,
			     umode_t mode)
{
	return -EIO;
}

static int bad_inode_set_acl(struct inode *inode, struct posix_acl *acl,
			     int type)
{
	return -EIO;
}

static const struct inode_operations bad_inode_ops =
{
	.create		= bad_inode_create,
	.lookup		= bad_inode_lookup,
	.link		= bad_inode_link,
	.unlink		= bad_inode_unlink,
	.symlink	= bad_inode_symlink,
	.mkdir		= bad_inode_mkdir,
	.rmdir		= bad_inode_rmdir,
	.mknod		= bad_inode_mknod,
	.rename		= bad_inode_rename2,
	.readlink	= bad_inode_readlink,
	.permission	= bad_inode_permission,
	.getattr	= bad_inode_getattr,
	.setattr	= bad_inode_setattr,
	.listxattr	= bad_inode_listxattr,
	.get_link	= bad_inode_get_link,
	.get_acl	= bad_inode_get_acl,
	.update_time	= bad_inode_update_time,
	.atomic_open	= bad_inode_atomic_open,
	.tmpfile	= bad_inode_tmpfile,
	.set_acl	= bad_inode_set_acl,
};

void clear_inode(struct inode *inode)
{
	inode->i_state = I_FREEING | I_CLEAR;
}


bool is_bad_inode(struct inode *inode)
{
	return (inode->i_op == &bad_inode_ops);
}

/* -------------- */


struct inode *ilookup(struct super_block *sb, unsigned long ino)
{
	jffs2_object_t *o = object_get(sb->s_part, ino, 0);

	if (o != NULL)
		return o->inode;

	return NULL;
}


int insert_inode_locked(struct inode *inode)
{
	int ret = object_insert(inode->i_sb->s_part, inode);

	if (ret != 0)
		return ret;

	return 0;
}


void make_bad_inode(struct inode *inode)
{
	free(inode->i_mapping);
	inode->i_mapping = NULL;
	inode->i_mode = S_IFREG;
	inode->i_atime = inode->i_mtime = inode->i_ctime =
					current_time(inode);
	inode->i_op = &bad_inode_ops;
	inode->i_fop = &bad_file_ops;
}


ssize_t generic_file_splice_read(struct file *filp, loff_t *off,
		struct pipe_inode_info *piinfo, size_t sz, unsigned int ui)
{
	return 0;
}


int generic_file_readonly_mmap(struct file *filp, struct vm_area_struct *vma)
{
	return 0;
}


ssize_t generic_file_write_iter(struct kiocb *kio, struct iov_iter *iov)
{
	return 0;
}


ssize_t generic_file_read_iter(struct kiocb *kio, struct iov_iter *iov)
{
	return 0;
}


int generic_file_open(struct inode *inode, struct file *filp)
{
	return 0;
}


int file_write_and_wait_range(struct file *file, loff_t start, loff_t end)
{
	return 0;
}


const char *simple_get_link(struct dentry *dentry, struct inode *inode, struct delayed_call *dc)
{
	return NULL;
}

void truncate_setsize(struct inode *inode, loff_t newsize)
{
	inode->i_size = newsize;
}


void truncate_inode_pages_final(struct address_space *addr_space)
{
}


void inode_init_once(struct inode *inode)
{
	memset(inode, 0, sizeof(struct inode));
}


int register_filesystem(struct file_system_type *fs)
{
	system_long_wq = malloc(sizeof(struct workqueue_struct));
	init_workqueue(system_long_wq);
	jffs2_common.fs = fs;
	return 0;
}


int unregister_filesystem(struct file_system_type *fs)
{
	return 0;
}


int sync_filesystem(struct super_block *sb)
{
	return 0;
}


struct dentry *generic_fh_to_dentry(struct super_block *sb,
	struct fid *fid, int fh_len, int fh_type,
	struct inode *(*get_inode) (struct super_block *sb, uint64_t ino, uint32_t gen))
{
	return NULL;
}


struct dentry *generic_fh_to_parent(struct super_block *sb,
	struct fid *fid, int fh_len, int fh_type,
	struct inode *(*get_inode) (struct super_block *sb, uint64_t ino, uint32_t gen))
{
	return NULL;
}
