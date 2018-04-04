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


#ifndef _OS_PHOENIX_FS_H_
#define _OS_PHOENIX_FS_H_

#include <sys/stat.h>

#define DT_UNKNOWN		0
#define DT_FIFO			1
#define DT_CHR			2
#define DT_DIR			4
#define DT_BLK			6
#define DT_REG			8
#define DT_LNK			10
#define DT_SOCK			12
#define DT_WHT			14

#define RENAME_NOREPLACE		(1 << 0)		/* Don't overwrite target */
#define RENAME_EXCHANGE			(1 << 1)		/* Exchange source and dest */
#define RENAME_WHITEOUT			(1 << 2)		/* Whiteout source */

#define S_IRWXUGO		(S_IRWXU|S_IRWXG|S_IRWXO)
#define S_IALLUGO		(S_ISUID|S_ISGID|S_ISVTX|S_IRWXUGO)
#define S_IRUGO			(S_IRUSR|S_IRGRP|S_IROTH)
#define S_IWUGO			(S_IWUSR|S_IWGRP|S_IWOTH)
#define S_IXUGO			(S_IXUSR|S_IXGRP|S_IXOTH)

struct dir_context {
	loff_t pos;
	int todo;
};


struct file {
	struct inode *f_inode;
	int todo;
};


static inline struct inode *file_inode(const struct file *f)
{
	return f->f_inode;
}


static inline bool dir_emit_dots(struct file *file, struct dir_context *ctx)
{
	return 1;
}

struct file_operations {
	loff_t (*llseek) (struct file *, loff_t, int);
	ssize_t (*read) (struct file *, char __user *, size_t, loff_t *);
	ssize_t (*read_iter) (struct kiocb *, struct iov_iter *);
	ssize_t (*write_iter) (struct kiocb *, struct iov_iter *);
	int (*iterate_shared) (struct file *, struct dir_context *);
	long (*unlocked_ioctl) (struct file *, unsigned int, unsigned long);
	int (*mmap) (struct file *, struct vm_area_struct *);
	int (*open) (struct inode *, struct file *);
	int (*fsync) (struct file *, loff_t, loff_t, int datasync);
	ssize_t (*splice_read)(struct file *, loff_t *, struct pipe_inode_info *, size_t, unsigned int);
};


struct inode_operations;

struct inode {

	ssize_t i_size;
	umode_t i_mode;
	unsigned long i_ino;
	struct super_block *i_sb;
	struct timespec i_mtime;
	struct timespec i_ctime;
	struct inode_operations *i_op;
	struct file_operations *i_fop;

	char *i_link;
};


struct inode_operations {
	struct dentry * (*lookup) (struct inode *,struct dentry *, unsigned int);
	const char * (*get_link) (struct dentry *, struct inode *, struct delayed_call *);
	int (*permission) (struct inode *, int);
	struct posix_acl * (*get_acl)(struct inode *, int);

	int (*readlink) (struct dentry *, char __user *,int);

	int (*create) (struct inode *,struct dentry *, umode_t, bool);
	int (*link) (struct dentry *,struct inode *,struct dentry *);
	int (*unlink) (struct inode *,struct dentry *);
	int (*symlink) (struct inode *,struct dentry *,const char *);
	int (*mkdir) (struct inode *,struct dentry *,umode_t);
	int (*rmdir) (struct inode *,struct dentry *);
	int (*mknod) (struct inode *,struct dentry *,umode_t,dev_t);
	int (*rename) (struct inode *, struct dentry *,
			struct inode *, struct dentry *, unsigned int);
	int (*setattr) (struct dentry *, struct iattr *);
	int (*getattr) (const struct path *, struct kstat *, u32, unsigned int);
	ssize_t (*listxattr) (struct dentry *, char *, size_t);
	int (*update_time)(struct inode *, struct timespec *, int);
	int (*atomic_open)(struct inode *, struct dentry *,
			   struct file *, unsigned open_flag,
			   umode_t create_mode, int *opened);
	int (*tmpfile) (struct inode *, struct dentry *, umode_t);
	int (*set_acl)(struct inode *, struct posix_acl *, int);
};


void init_special_inode(struct inode *inode, umode_t mode, dev_t dev)
{
	return;
}


void inc_nlink(struct inode *inode) 
{
	return;
}

void clear_nlink(struct inode *inode) 
{
	return;
}

void set_nlink(struct inode *inode, unsigned int nlink) 
{
	return;
}

void drop_nlink(struct inode *inode)
{
	return;
}

#endif /* _OS_PHOENIX_FS_H_ */
