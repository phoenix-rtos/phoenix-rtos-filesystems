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
#ifndef _OS_PHOENIX_H_
#define _OS_PHOENIX_H_

#include <stdlib.h>
#include <sys/minmax.h>
#include <sys/threads.h>
#include <sys/mman.h>
#include <sys/rb.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <syslog.h>

#include <mtd/mtd.h>

#include "linux/list.h"

#include "phoenix-rtos/types.h"
#include "phoenix-rtos/kernel.h"
#include "phoenix-rtos/completion.h"
#include "phoenix-rtos/dev.h"
#include "phoenix-rtos/rb.h"
#include "phoenix-rtos/locks.h"
#include "phoenix-rtos/dentry.h"
#include "phoenix-rtos/fs.h"
#include "phoenix-rtos/object.h"
#include "phoenix-rtos/crc32.h"
#include "phoenix-rtos/slab.h"
#include "phoenix-rtos/capability.h"
#include "phoenix-rtos/wait.h"
#include "phoenix-rtos/mtd.h"

#include "jffs2.h"
#include "jffs2_fs_i.h"
#include "jffs2_fs_sb.h"

#ifndef __PHOENIX
#define __PHOENIX
#endif

#define JFFS2_SUPER_MAGIC 0x72b6

#define SECTOR_ADDR(x) ( (((unsigned long)(x) / c->sector_size) * c->sector_size) )

struct page {
	unsigned long flags;
	union {
		struct address_space *mapping;
		void *s_mem;
		atomic_t compound_mapcount;
	};

	pgoff_t index;
	void *virtual;
};


void *page_address(const struct page *page);

void put_page(struct page *page);


int PageUptodate(struct page *page);

struct page *grab_cache_page_write_begin(struct address_space *mapping,
			pgoff_t index, unsigned flags);

void unlock_page(struct page *page);

void flush_dcache_page(struct page *pg);

typedef int filler_t(void *, struct page *);

struct page *read_cache_page(struct address_space *mapping, pgoff_t index, filler_t *filler, void *data);

extern unsigned int dirty_writeback_interval; /* centiseconds */


#define PageLocked(x) { }
#define SetPageError(x) { }
#define ClearPageError(x) { }
#define SetPageUptodate(x) { }
#define ClearPageUptodate(x) { }


#define BUG() do { \
	printf("BUG at %s:%d function %s\n", __FILE__, __LINE__, __func__); \
	sleep(10000); \
} while (1)

#define BUG_ON(condition) do { \
	if (condition) \
		BUG(); \
} while (0)

#define WARN_ON(condition) do { } while (0)
#define BUILD_BUG_ON(condition) ((void)sizeof(char[1 - 2*!!(condition)]))

#define JFFS2_INODE_INFO(i) (container_of(i, struct jffs2_inode_info, vfs_inode))
#define OFNI_EDONI_2SFFJ(f)  (&(f)->vfs_inode)

#define OFNI_BS_2SFFJ(c)  ((struct super_block *)c->os_priv)

#define JFFS2_SB_INFO(sb) (sb->s_fs_info)

#define ITIME(sec) ((struct timespec){sec, 0})
#define I_SEC(tv) ((tv).tv_sec)

#define JFFS2_F_I_SIZE(f) (OFNI_EDONI_2SFFJ(f)->i_size)
#define JFFS2_F_I_MODE(f) (OFNI_EDONI_2SFFJ(f)->i_mode)
#define JFFS2_F_I_UID(f) (i_uid_read(OFNI_EDONI_2SFFJ(f)))
#define JFFS2_F_I_GID(f) (i_gid_read(OFNI_EDONI_2SFFJ(f)))
#define JFFS2_F_I_RDEV(f) (OFNI_EDONI_2SFFJ(f)->i_rdev)

#define JFFS2_F_I_CTIME(f) (OFNI_EDONI_2SFFJ(f)->i_ctime.tv_sec)
#define JFFS2_F_I_MTIME(f) (OFNI_EDONI_2SFFJ(f)->i_mtime.tv_sec)
#define JFFS2_F_I_ATIME(f) (OFNI_EDONI_2SFFJ(f)->i_atime.tv_sec)

struct user_namespace {
};

struct user_namespace init_user_ns;

uid_t from_kuid(struct user_namespace *to, kuid_t kuid);

gid_t from_kgid(struct user_namespace *to, kgid_t kgid);

long PTR_ERR(const void *ptr);

void *ERR_PTR(long error);

void *ERR_CAST(const void *ptr);

bool IS_ERR(const void *ptr);

ssize_t generic_read_dir(struct file *filp, char __user *buf, size_t siz, loff_t *ppos);

loff_t generic_file_llseek(struct file *file, loff_t offset, int whence);

#define TASK_INTERRUPTIBLE 0x0001

#define current (pid_t)0
#define schedule()
#define jiffies 0

unsigned long msecs_to_jiffies(const unsigned int m);

long schedule_timeout_interruptible(long timeout);

struct task_struct {
	pid_t pid;
};

pid_t task_pid_nr(struct task_struct *tsk);

void set_user_nice(struct task_struct *p, long nice);

void set_freezable(void);

bool freezing(struct task_struct *p);

bool try_to_freeze(void);


#define _NSIG		64
#define _NSIG_BPW	sizeof(long) * 8
#define _NSIG_WORDS	(_NSIG / _NSIG_BPW)

#define sigmask(sig)	(1UL << ((sig) - 1))

int kernel_dequeue_signal(siginfo_t *info);

void allow_signal(int sig);

int signal_pending(struct task_struct *p);

int send_sig(int sig, struct task_struct *task, int priv);

void kernel_signal_stop(void);

void siginitset(sigset_t *set, unsigned long mask);


#define set_current_state(state_value) do { } while (0)

void *kmalloc(int len, int flag);

void kfree(void *ptr);

void *kzalloc(int len, int flag);

void *kcalloc(size_t n, size_t size, gfp_t flags);

void *vmalloc(unsigned long size);

void *vzalloc(unsigned long size);

void kvfree(const void *addr);

void *kmap(struct page *page);

void kunmap(struct page *page);


#define KERN_DEBUG
#define printk(...)				syslog(LOG_DEBUG, ##__VA_ARGS__)
#define pr_notice(fmt, ...)		syslog(LOG_NOTICE, fmt, ##__VA_ARGS__)
#define pr_info(fmt, ...)		syslog(LOG_INFO, fmt, ##__VA_ARGS__)
#define pr_debug(fmt, ...)		syslog(LOG_DEBUG, fmt, ##__VA_ARGS__)
#define pr_warn(fmt, ...)		syslog(LOG_WARNING, fmt, ##__VA_ARGS__)
#define pr_cont(fmt, ...)		syslog(LOG_DEBUG, fmt, ##__VA_ARGS__)
#define pr_err(fmt, ...)		printf(fmt, ##__VA_ARGS__)
#define pr_crit(fmt, ...)		printf(fmt, ##__VA_ARGS__)


#define GFP_KERNEL 0
#define GFP_USER 1


extern const struct inode_operations jffs2_file_inode_operations;
extern const struct inode_operations jffs2_symlink_inode_operations;
extern const struct address_space_operations jffs2_file_address_operations;
extern const struct file_operations jffs2_file_operations;
extern const struct inode_operations jffs2_dir_inode_operations;
extern const struct file_operations jffs2_dir_operations;


#define jffs2_wbuf_dirty(c) (!!(c)->wbuf_len)
#define jffs2_can_mark_obsolete(c) (c->mtd->flags & (MTD_BIT_WRITEABLE))
#define jffs2_is_readonly(c) (OFNI_BS_2SFFJ(c)->s_flags & SB_RDONLY)
#define jffs2_is_writebuffered(c) (c->wbuf != NULL)
#define jffs2_cleanmarker_oob(c) (c->mtd->type == MTD_NANDFLASH)


int jffs2_fsync(struct file *, loff_t, loff_t, int);

long jffs2_ioctl(struct file *, unsigned int, unsigned long);

int jffs2_setattr (struct dentry *, struct iattr *);

struct inode *jffs2_iget(struct super_block *, unsigned long);

struct inode *jffs2_new_inode (struct inode *dir_i, umode_t mode,
			       struct jffs2_raw_inode *ri);

void jffs2_garbage_collect_trigger(struct jffs2_sb_info *c);

void jffs2_gc_release_inode(struct jffs2_sb_info *c,
			    struct jffs2_inode_info *f);

struct jffs2_inode_info *jffs2_gc_fetch_inode(struct jffs2_sb_info *c,
					      int inum, int unlinked);

unsigned char *jffs2_gc_fetch_page(struct jffs2_sb_info *c,
				   struct jffs2_inode_info *f,
				   unsigned long offset,
				   unsigned long *priv);

void jffs2_gc_release_page(struct jffs2_sb_info *c,
			   unsigned char *pg,
			   unsigned long *priv);

void jffs2_flash_cleanup(struct jffs2_sb_info *c);

int jffs2_do_readpage_unlock (struct inode *inode, struct page *pg);

int jffs2_start_garbage_collect_thread(struct jffs2_sb_info *c);

void jffs2_stop_garbage_collect_thread(struct jffs2_sb_info *c);


int jffs2_flash_read(struct jffs2_sb_info *c, loff_t ofs, size_t len, size_t *retlen, u_char *buf);

int jffs2_flash_write(struct jffs2_sb_info *c, loff_t ofs, size_t len, size_t *retlen, const u_char *buf);

int jffs2_flash_writev(struct jffs2_sb_info *c, const struct kvec *vecs, unsigned long count, loff_t to, size_t *retlen, uint32_t ino);

int jffs2_write_nand_cleanmarker(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb);

int jffs2_write_nand_badblock(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb, uint32_t bad_offset);


#define jffs2_ubivol(c) (c->mtd->type == MTD_UBIVOLUME)

int jffs2_ubivol_setup(struct jffs2_sb_info *c);

void jffs2_ubivol_cleanup(struct jffs2_sb_info *c);


void jffs2_nor_wbuf_flash_cleanup(struct jffs2_sb_info *c);

void jffs2_dataflash_cleanup(struct jffs2_sb_info *c);

void jffs2_nand_flash_cleanup(struct jffs2_sb_info *c);

int jffs2_dataflash_setup(struct jffs2_sb_info *c);

int jffs2_nand_flash_setup(struct jffs2_sb_info *c);

#define jffs2_dataflash(c) (c->mtd->type == MTD_DATAFLASH)

#define jffs2_nor_wbuf_flash(c) (c->mtd->type == MTD_NORFLASH && ! (c->mtd->flags & MTD_BIT_WRITEABLE))

int jffs2_nor_wbuf_flash_setup(struct jffs2_sb_info *c);

void jffs2_dirty_trigger(struct jffs2_sb_info *c);

int jffs2_check_oob_empty(struct jffs2_sb_info *c, struct jffs2_eraseblock *jeb,int mode);

void jffs2_evict_inode (struct inode *);

int jffs2_statfs (struct dentry *, struct kstatfs *);

void jffs2_dirty_inode(struct inode *inode, int flags);

int jffs2_do_remount_fs(struct super_block *, int *, char *);

int jffs2_do_fill_super(struct super_block *sb, void *data, int silent);

int init_jffs2_fs(void);

void exit_jffs2_fs(void);


extern struct workqueue_struct *system_long_wq;

static inline void jffs2_init_inode_info(struct jffs2_inode_info *f)
{
	f->highest_version = 0;
	f->fragtree = RB_ROOT;
	f->metadata = NULL;
	f->dents = NULL;
	f->target = NULL;
	f->flags = 0;
	f->usercompr = 0;

}


int jffs2_flash_direct_writev(struct jffs2_sb_info *c, const struct kvec *vecs,
		       unsigned long count, loff_t to, size_t *retlen);

int jffs2_flash_direct_write(struct jffs2_sb_info *c, loff_t ofs, size_t len,
			size_t *retlen, const u_char *buf);

unsigned int full_name_hash(void *salt, const char * name, unsigned int len);

void *kmemdup(const void *src, size_t len, unsigned gfp);

unsigned long get_seconds(void);

#define os_to_jffs2_mode(x) (x)
#define jffs2_to_os_mode(x) (x)

#define uninitialized_var(x) x = *(&(x))

#define min_t(type, x, y) ({ \
	(type)x > (type) y ? (type)y : (type)x; })


#define kthread_run(threadfn, data, namefmt, ...)			   \
	({ \
		void *stack = mmap(NULL, 0x1000, PROT_WRITE | PROT_READ, MAP_ANONYMOUS, -1, 0); \
		struct task_struct *__k = malloc(sizeof(struct task_struct)); \
		beginthread(threadfn, 6, stack, 0x1000, data); \
		__k->pid = 0x33; \
		__k; \
	})

bool kthread_should_stop(void);

kuid_t current_fsuid();

kgid_t current_fsgid();

typedef void (*rcu_callback_t)(struct rcu_head *head);

void call_rcu(struct rcu_head *head, rcu_callback_t func);

void seq_printf(struct seq_file *m, const char *fmt, ...);

#define MODULE_LICENSE(x)
#define MODULE_AUTHOR(x)
#define MODULE_DESCRIPTION(x)
#define module_init(x)
#define module_exit(x)
#define __must_hold(x)

enum {MAX_OPT_ARGS = 3};

typedef struct {
	char *from;
	char *to;
} substring_t;


struct match_token {
	int token;
	const char *pattern;
};


typedef struct match_token match_table_t[];

char *strsep(char **s, const char *ct);

int match_token(char *s, const match_table_t table, substring_t args[]);

char *match_strdup(const substring_t *s);

int match_int(substring_t *s, int *result);


void rcu_barrier(void);


typedef struct _jffs2_partition_t {
	uint32_t port;
	int flags;
	int root;
	int stop_gc;
	storage_t *strg;

	struct super_block *sb;
	void *objects;
	void *devs;
} jffs2_partition_t;

typedef struct _jffs2_common_t {
	struct file_system_type		*fs;
	struct workqueue_struct		*system_long_wq;
	jffs2_partition_t			*partition;
	uint32_t			partition_cnt;
} jffs2_common_t;

extern jffs2_common_t jffs2_common;

#define system_long_wq jffs2_common.system_long_wq

#endif /* _OS_PHOENIX_H_ */
