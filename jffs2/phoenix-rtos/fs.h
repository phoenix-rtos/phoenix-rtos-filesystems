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


/* Some parts of this file are copied from Linux kernel */

#ifndef _OS_PHOENIX_FS_H_
#define _OS_PHOENIX_FS_H_

#include <sys/stat.h>
#include <sys/file.h>
#include <dirent.h>

#define RENAME_NOREPLACE		(1 << 0)		/* Don't overwrite target */

#define S_IRWXUGO		(S_IRWXU|S_IRWXG|S_IRWXO)
#define S_IRUGO			(S_IRUSR|S_IRGRP|S_IROTH)
#define S_IWUGO			(S_IWUSR|S_IWGRP|S_IWOTH)
#define S_IXUGO			(S_IXUSR|S_IXGRP|S_IXOTH)

#define SB_RDONLY		 1		/* Mount read-only */
#define SB_NOATIME		1024	/* Do not update access times. */
#define SB_POSIXACL		(1<<16)	/* VFS does not apply the umask */

#define I_DIRTY_DATASYNC		(1 << 1)
#define __I_NEW					3
#define I_NEW					(1 << __I_NEW)
#define I_FREEING				(1 << 5)
#define I_CLEAR					(1 << 6)

struct timespec current_time(struct inode *inode);

struct dir_context;
struct page;
struct address_space;
struct iattr;
struct mtd_info;

typedef int (*filldir_t)(struct dir_context *, const char *, int, loff_t, uint64_t,
					 unsigned);

struct dir_context {
	const filldir_t actor;
	loff_t pos;
	struct dirent *dent;
	int emit;
};


struct file {
	struct inode *f_inode;
	struct address_space *f_mapping;
	uint64_t f_pino;
};


static inline struct inode *file_inode(const struct file *f)
{
	return f->f_inode;
}


static inline bool dir_emit(struct dir_context *ctx, const char *name,
		int namelen, uint64_t ino, unsigned type)
{
	return ctx->actor(ctx, name, namelen, ctx->pos, ino, type);
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



struct address_space {
	struct inode							*host;		/* owner: inode, block_device */
	spinlock_t								tree_lock;	/* and lock protecting it */
	atomic_t								i_mmap_writable;/* count VM_SHARED mappings */
	struct rb_root							i_mmap;		/* tree of private and shared mappings */
	unsigned long							nrpages;	/* number of total pages */
	unsigned long							nrexceptional;
	const struct address_space_operations 	*a_ops;	/* methods */
	unsigned long							flags;		/* error bits/gfp mask */
	spinlock_t								private_lock;	/* for use by the address_space */
	struct list_head						private_list;	/* ditto */
	void									*private_data;	/* ditto */
} __attribute__((aligned(sizeof(long))));

struct inode_operations;

struct inode {
	ssize_t						i_size;
	umode_t						i_mode;
	unsigned long				i_ino;
	struct super_block			*i_sb;
	struct timespec				i_atime;
	struct timespec				i_mtime;
	struct timespec				i_ctime;
	struct inode_operations		*i_op;
	struct file_operations		*i_fop;
	struct address_space		*i_mapping;
	char						*i_link;
	unsigned int				i_nlink;
	unsigned int				i_count;
	blkcnt_t					i_blocks;
	dev_t						i_rdev;
	unsigned long				i_state;
	struct address_space		i_data;
	kuid_t						i_uid;
	kgid_t						i_gid;
	struct rcu_head				i_rcu;
	handle_t					i_lock;
	struct rw_semaphore			i_rwsem;
};

static inline bool dir_emit_dots(struct file *file, struct dir_context *ctx)
{
	if (ctx->pos == 0)
		return ctx->actor(ctx, ".", 1, ctx->pos, file_inode(file)->i_ino, DT_DIR);
	else if (ctx->pos == 1)
		return ctx->actor(ctx, "..", 2, ctx->pos, file->f_pino, DT_DIR);
	return 1;
}

static inline int dir_print(struct dir_context *ctx, const char *name, int len, loff_t pos, uint64_t ino, unsigned type)
{
	ctx->dent->d_type = type;
	ctx->pos++;
	ctx->emit++;
	ctx->dent->d_namlen = len;
	ctx->dent->d_ino = ino;
	memcpy(ctx->dent->d_name, name, len);
	ctx->dent->d_name[len] = '\0';
	return 0;
}

struct super_operations;

struct xattr_handler {
	const char *name;
	const char *prefix;
	int flags;      /* fs private flags */
	bool (*list)(struct dentry *dentry);
	int (*get)(const struct xattr_handler *, struct dentry *dentry,
		   struct inode *inode, const char *name, void *buffer,
		   size_t size);
	int (*set)(const struct xattr_handler *, struct dentry *dentry,
		   struct inode *inode, const char *name, const void *buffer,
		   size_t size, int flags);
};


struct super_block {
	void *s_fs_info;
	unsigned long s_magic;
	unsigned char s_blocksize_bits;
	unsigned long s_blocksize;
	unsigned long s_flags;
	loff_t s_maxbytes;
	struct dentry *s_root;
	struct mtd_info *s_mtd;
	void *s_part;
	const struct super_operations *s_op;
	const struct export_operations *s_export_op;
	const struct xattr_handler **s_xattr;
};

static inline bool sb_rdonly(const struct super_block *sb) { return sb->s_flags & SB_RDONLY; }


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
	int (*getattr) (const struct path *, struct kstat *, uint32_t, unsigned int);
	ssize_t (*listxattr) (struct dentry *, char *, size_t);
	int (*update_time)(struct inode *, struct timespec *, int);
	int (*atomic_open)(struct inode *, struct dentry *,
			   struct file *, unsigned open_flag,
			   umode_t create_mode, int *opened);
	int (*tmpfile) (struct inode *, struct dentry *, umode_t);
	int (*set_acl)(struct inode *, struct posix_acl *, int);
};

struct kstatfs;

struct super_operations {
   	struct inode *(*alloc_inode)(struct super_block *sb);
	void (*destroy_inode)(struct inode *);

   	void (*dirty_inode) (struct inode *, int flags);
	void (*evict_inode) (struct inode *);
	void (*put_super) (struct super_block *);
	int (*sync_fs)(struct super_block *sb, int wait);
	int (*statfs) (struct dentry *, struct kstatfs *);
	int (*remount_fs) (struct super_block *, int *, char *);

	int (*show_options)(struct seq_file *, struct dentry *);
};


struct address_space_operations {
	int (*readpage)(struct file *, struct page *);

	/* Write back some dirty pages from this mapping. */
	int (*write_begin)(struct file *, struct address_space *mapping,
				loff_t pos, unsigned len, unsigned flags,
				struct page **pagep, void **fsdata);
	int (*write_end)(struct file *, struct address_space *mapping,
				loff_t pos, unsigned len, unsigned copied,
				struct page *page, void *fsdata);
};

#define ATTR_MODE	(1 << 0)
#define ATTR_UID	(1 << 1)
#define ATTR_GID	(1 << 2)
#define ATTR_SIZE	(1 << 3)
#define ATTR_ATIME	(1 << 4)
#define ATTR_MTIME	(1 << 5)
#define ATTR_CTIME	(1 << 6)
#define ATTR_ATIME_SET	(1 << 7)
#define ATTR_MTIME_SET	(1 << 8)
#define ATTR_ATTR_FLAG	(1 << 10)
#define ATTR_KILL_SUID	(1 << 11)
#define ATTR_KILL_SGID	(1 << 12)
#define ATTR_FILE	(1 << 13)
#define ATTR_KILL_PRIV	(1 << 14)
#define ATTR_OPEN	(1 << 15) /* Truncating from open(O_TRUNC) */
#define ATTR_TIMES_SET	(1 << 16)
#define ATTR_TOUCH	(1 << 17)

struct iattr {
	unsigned int	ia_valid;
	umode_t		ia_mode;
	kuid_t		ia_uid;
	kgid_t		ia_gid;
	loff_t		ia_size;
	struct timespec	ia_atime;
	struct timespec	ia_mtime;
	struct timespec	ia_ctime;

	struct file	*ia_file;
};

int setattr_prepare(struct dentry *dentry, struct iattr *iattr);

static inline int posix_acl_chmod(struct inode *inode, umode_t mode)
{
	return 0;
}

void init_special_inode(struct inode *inode, umode_t mode, dev_t dev);

void inc_nlink(struct inode *inode);

void clear_nlink(struct inode *inode);

void set_nlink(struct inode *inode, unsigned int nlink);

void drop_nlink(struct inode *inode);

void ihold(struct inode * inode);

struct inode *new_inode(struct super_block *sb);

void unlock_new_inode(struct inode *inode);

void iget_failed(struct inode *inode);

struct inode * iget_locked(struct super_block *sb, unsigned long ino);

void iput(struct inode *inode);

static inline void inode_lock(struct inode *inode)
{
	down_write(&inode->i_rwsem);
}

static inline void inode_unlock(struct inode *inode)
{
	up_write(&inode->i_rwsem);
}

static inline void inode_lock_shared(struct inode *inode)
{
	down_read(&inode->i_rwsem);
}

static inline void inode_unlock_shared(struct inode *inode)
{
	up_read(&inode->i_rwsem);
}

void clear_inode(struct inode *inode);

bool is_bad_inode(struct inode *inode);

struct inode *ilookup(struct super_block *sb, unsigned long ino);

int insert_inode_locked(struct inode *inode);

void make_bad_inode(struct inode *inode);

/* Helper functions so that in most cases filesystems will
 * not need to deal directly with kuid_t and kgid_t and can
 * instead deal with the raw numeric values that are stored
 * in the filesystem.
 */
static inline uid_t i_uid_read(const struct inode *inode)
{
	return inode->i_uid.val;
}


static inline gid_t i_gid_read(const struct inode *inode)
{
	return inode->i_gid.val;
}

static inline void i_uid_write(struct inode *inode, uid_t uid)
{
	//inode->i_uid = make_kuid(inode->i_sb->s_user_ns, uid);
}

static inline void i_gid_write(struct inode *inode, gid_t gid)
{
	//inode->i_gid = make_kgid(inode->i_sb->s_user_ns, gid);
}


ssize_t generic_file_splice_read(struct file *filp, loff_t *off,
		struct pipe_inode_info *piinfo, size_t sz, unsigned int ui);

int generic_file_readonly_mmap(struct file *filp, struct vm_area_struct *vma);

ssize_t generic_file_write_iter(struct kiocb *kio, struct iov_iter *iov);

ssize_t generic_file_read_iter(struct kiocb *kio, struct iov_iter *iov);

int generic_file_open(struct inode *inode, struct file *filp);

int file_write_and_wait_range(struct file *file, loff_t start, loff_t end);

const char *simple_get_link(struct dentry *dentry, struct inode *inode, struct delayed_call *dc);

void truncate_setsize(struct inode *inode, loff_t newsize);

void truncate_inode_pages_final(struct address_space *addr_space);


extern void inode_init_once(struct inode *inode);

typedef struct {
	long	val[2];
} __kernel_fsid_t;

struct kstatfs {
	long f_type;
	long f_bsize;
	uint64_t f_blocks;
	uint64_t f_bfree;
	uint64_t f_bavail;
	uint64_t f_files;
	uint64_t f_ffree;
	__kernel_fsid_t f_fsid;
	long f_namelen;
	long f_frsize;
	long f_flags;
	long f_spare[4];
};

struct fid {
	union {
		struct {
			uint32_t ino;
			uint32_t gen;
			uint32_t parent_ino;
			uint32_t parent_gen;
		} i32;
		struct {
			uint32_t block;
			uint16_t partref;
			uint16_t parent_partref;
			uint32_t generation;
			uint32_t parent_block;
			uint32_t parent_generation;
		} udf;
		uint32_t raw[0];
	};
};


struct export_operations {
	int (*encode_fh)(struct inode *inode, uint32_t *fh, int *max_len,
			struct inode *parent);
	struct dentry * (*fh_to_dentry)(struct super_block *sb, struct fid *fid,
			int fh_len, int fh_type);
	struct dentry * (*fh_to_parent)(struct super_block *sb, struct fid *fid,
			int fh_len, int fh_type);
	int (*get_name)(struct dentry *parent, char *name,
			struct dentry *child);
	struct dentry * (*get_parent)(struct dentry *child);
	int (*commit_metadata)(struct inode *inode);

	int (*get_uuid)(struct super_block *sb, uint8_t *buf, uint32_t *len, uint64_t *offset);
};


extern struct dentry *generic_fh_to_dentry(struct super_block *sb,
	struct fid *fid, int fh_len, int fh_type,
	struct inode *(*get_inode) (struct super_block *sb, uint64_t ino, uint32_t gen));


extern struct dentry *generic_fh_to_parent(struct super_block *sb,
	struct fid *fid, int fh_len, int fh_type,
	struct inode *(*get_inode) (struct super_block *sb, uint64_t ino, uint32_t gen));


#define MODULE_ALIAS_FS(x)
#define THIS_MODULE 1
struct file_system_type {
	const char *name;
	int fs_flags;
	struct dentry *(*mount) (struct file_system_type *, int,
		       const char *, void *);
	void (*kill_sb) (struct super_block *);
	int owner;
};


int register_filesystem(struct file_system_type *fs);

int unregister_filesystem(struct file_system_type *fs);

int sync_filesystem(struct super_block *sb);

#endif /* _OS_PHOENIX_FS_H_ */
