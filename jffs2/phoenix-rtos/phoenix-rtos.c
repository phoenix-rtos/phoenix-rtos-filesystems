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

unsigned int dirty_writeback_interval = 5 * 100000; /* centiseconds */

void *page_address(const struct page *page)
{
	return page->virtual;
}


void put_page(struct page *page)
{
	if (page->virtual)
		free(page->virtual);
	free(page);
}


int PageUptodate(struct page *page)
{
	return 0;
}


struct page *grab_cache_page_write_begin(struct address_space *mapping,
			pgoff_t index, unsigned flags)
{
	return NULL;
}


void unlock_page(struct page *page)
{
}


void flush_dcache_page(struct page *pg)
{
}


struct page *read_cache_page(struct address_space *mapping, pgoff_t index, filler_t *filler, void *data)
{
	struct inode *inode = (struct inode *)data;
	struct page *pg = malloc(sizeof(struct page));
	int ret;

	pg->index = index;
	pg->virtual = malloc(PAGE_SIZE);

	if ((ret = filler(inode, pg))) {
		free(pg->virtual);
		free(pg);
		return ERR_PTR(ret);
	}

	return pg;
}

uid_t from_kuid(struct user_namespace *to, kuid_t kuid)
{
	return kuid.val;
}

gid_t from_kgid(struct user_namespace *to, kgid_t kgid)
{
	return kgid.val;
}


long PTR_ERR(const void *ptr)
{
	return (long) ptr;
}


void *ERR_PTR(long error)
{
	return (void *) error;
}

void *ERR_CAST(const void *ptr)
{
	return (void *) ptr;
}


bool IS_ERR(const void *ptr)
{
	return (unsigned long)ptr >= (unsigned long)-0xfff;
}


ssize_t generic_read_dir(struct file *filp, char __user *buf, size_t siz, loff_t *ppos)
{
	return -EISDIR;
}


loff_t generic_file_llseek(struct file *file, loff_t offset, int whence)
{
	return -EINVAL;
}

unsigned long msecs_to_jiffies(const unsigned int m)
{
	return m;
}

long schedule_timeout_interruptible(long timeout)
{
	return 0;
}

inline pid_t task_pid_nr(struct task_struct *tsk)
{
	static pid_t pid = -1;
	if (pid == -1)
		pid = getpid();
	return pid;
}

void set_user_nice(struct task_struct *p, long nice)
{
}

void set_freezable(void)
{
}

bool freezing(struct task_struct *p)
{
	return 0;
}

bool try_to_freeze(void)
{
	return 0;
}

int kernel_dequeue_signal(siginfo_t *info)
{
	return 0;
}

void allow_signal(int sig)
{
}


int signal_pending(struct task_struct *p)
{
	return 0;
}


int send_sig(int sig, struct task_struct *task, int priv)
{
	return 0;
}

void kernel_signal_stop(void)
{
}

void siginitset(sigset_t *set, unsigned long mask)
{
}

void *kmalloc(int len, int flag)
{
	return malloc(len);
}

void kfree(void *ptr)
{
	free(ptr);
}

void *kzalloc(int len, int flag)
{
	void *ptr = malloc(len);

	if (ptr != NULL)
		memset(ptr, 0, len);

	return ptr;
}

void *kcalloc(size_t n, size_t size, gfp_t flags)
{
	void *ptr = malloc(n * size);

	if (ptr != NULL)
		memset(ptr, 0, n * size);

	return ptr;
}

void *vmalloc(unsigned long size)
{
	return malloc(size);
}

void *vzalloc(unsigned long size)
{
	void *ptr = malloc(size);

	if (ptr != NULL)
		memset(ptr, 0, size);

	return ptr;
}

void kvfree(const void *addr)
{
	free((void *)addr);
}

void *kmap(struct page *page)
{
	return page->virtual;
}

void kunmap(struct page *page)
{
//	free(page->virtual);
}

/* taken form Linux kernel */
unsigned int full_name_hash(void *salt, const char * name, unsigned int len)
{
	unsigned hash = 0;
	unsigned long c;

	while (len--) {
		c = *name++;
		hash = (hash + (c << 4) + (c >> 4)) * 11;
	}
	return hash;
}


void *kmemdup(const void *src, size_t len, unsigned gfp)
{
	void *dst = malloc(len);
	memcpy(dst, src, len);
	return dst;
}


unsigned long get_seconds(void)
{
	return time(NULL);
}

bool kthread_should_stop(void)
{
	return 0;
}

kuid_t current_fsuid()
{
	kuid_t kuid;
	kuid.val = 0;
	return kuid;
}

kgid_t current_fsgid()
{
	kgid_t kgid;
	kgid.val = 0;
	return kgid;
}


void call_rcu(struct rcu_head *head, rcu_callback_t func)
{
	func(head);
}


void rcu_barrier(void)
{
}

void seq_printf(struct seq_file *m, const char *fmt, ...)
{
}

int match_token(char *s, const match_table_t table, substring_t args[])
{
	return 0;
}

char *match_strdup(const substring_t *s)
{
	return NULL;
}

int match_int(substring_t *s, int *result)
{
	return 0;
}
