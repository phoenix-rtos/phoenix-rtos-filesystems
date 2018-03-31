/* 
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * Jffs2 FileSystem - system specific information.
 *
 * Copyright 2014-2015 Phoenix Systems
 * Author: Katarzyna Baranowska
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef __JFFS2_PHOENIX_H__
#define __JFFS2_PHOENIX_H__

#include <fs/if.h>
#include <main/if.h>
#include <proc/if.h>
#include <lib/if.h>

#include <fs/jffs2/jffs2.h>


/******************* JFFS2 types *******************/
typedef vnode_t* os_inode_t;
typedef superblock_t* os_superblock_t;
typedef dev_t os_mtd_t;
typedef u64 os_time_t;
typedef u32 os_mode_t;

typedef int bool;
#define true 1

typedef struct _os_priv_data {
	dev_t dev;
	offs_t partitionBegin;
	int isReadonly;
	os_superblock_t osSb;
} os_priv_data;


/********** JFFS2 and PHOENIX relationship **********/
#define jffs2_to_os_time(time) (time)
#define os_to_jffs2_time(time) (time)

#define os_to_jffs2_mode(x) (x)
#define jffs2_to_os_mode(x) (x)

struct jffs2_inode_info;
struct jffs2_sb_info;

#define JFFS2_INODE_INFO(vnode) ((struct jffs2_inode_info *)(vnode)->fs_priv)
#define OFNI_EDONI_2SFFJ(f)  ((os_inode_t)(f)->vfs_inode)
#define JFFS2_SB_INFO(sb) ((struct jffs2_sb_info *)(sb)->priv)
#define OFNI_BS_2SFFJ(c)  (((os_priv_data *) (c)->os_priv)->osSb)

#define JFFS2_F_I_SIZE(f) (OFNI_EDONI_2SFFJ(f)->size)
#define JFFS2_F_I_MODE(f) (OFNI_EDONI_2SFFJ(f)->mode)
#define JFFS2_F_I_UID(f)  (OFNI_EDONI_2SFFJ(f)->uid)
#define JFFS2_F_I_GID(f)  (OFNI_EDONI_2SFFJ(f)->gid)

#define JFFS2_F_I_RDEV(f) (OFNI_EDONI_2SFFJ(f)->dev)
#define jffs2_devlen(dev) sizeof(dev)
#define jffs2_devdata(dev) ((char *)&(dev))
        
#define JFFS2_F_I_CTIME(f) os_to_jffs2_time(OFNI_EDONI_2SFFJ(f)->ctime)
#define JFFS2_F_I_MTIME(f) os_to_jffs2_time(OFNI_EDONI_2SFFJ(f)->mtime)
#define JFFS2_F_I_ATIME(f) os_to_jffs2_time(OFNI_EDONI_2SFFJ(f)->atime)


/**************** Errors as pointers ****************/
#define ERR_PTR(x) ((void *)(x))
#define PTR_ERR(x) (-((int)(((void *)(-1)) - (void *)(x) + 1)))
#define ERR_CAST(x) ((void *)(x))
#define IS_ERR(x)  ((void *)(x) > (void *)(-100))


/***************** JFFS2 constants *****************/
/* PAGE_SIZE = size of read bufor used for scanning flash
 * on mount or whec chcecking if block is fully erased in gc */
#define PAGE_SIZE SIZE_PAGE
/* INOCACHE_HASHSIZE = limits for size of table containing pointers
 * to inocaches for quick seaching for inocaches with given inodde nr */
#define INOCACHE_HASHSIZE_MIN 128
#define INOCACHE_HASHSIZE_MAX 1024
/* PAGE_CACHE_SIZE = size of max not compressed file data stored in one node,
 * the bigger the better, but it also must be alocated to uncompress file data on read */
#define PAGE_CACHE_SHIFT 10
#define PAGE_CACHE_SIZE (1UL << PAGE_CACHE_SHIFT)


/********************* Printing *********************/
#define KERN_DEBUG
#define pr_notice(...) main_printf(ATTR_ERROR, __VA_ARGS__)
#define pr_warn(...)   main_printf(ATTR_ERROR, __VA_ARGS__)
#define pr_err(...)    main_printf(ATTR_ERROR, __VA_ARGS__)
#define pr_crit(...)   main_printf(ATTR_ERROR, __VA_ARGS__)
#define pr_cont(...)   main_printf(ATTR_ERROR, __VA_ARGS__)

#define pr_debug(...)  main_printf(ATTR_DEBUG, __VA_ARGS__)
#define printk(...)    main_printf(ATTR_DEBUG, __VA_ARGS__)
#define pr_info(...)   main_printf(ATTR_INFO, __VA_ARGS__)

#define BUG_ON(x) assert(!(x))
#define WARN_ON(x) assert(!(x))
#define BUG() assert(0 != 0)


/************* Threads synchronization *************/
#define cond_resched() hal_cpuReschedule()

typedef mutex_t os_mutex_t;
#define mutex_init(x)    proc_mutexCreate(x)
#define mutex_lock(x)    proc_mutexLock(x)
#define mutex_unlock(x)  proc_mutexUnlock(x)
#define mutex_destroy(x) proc_mutexTerminate(x)
#define mutex_lock_interruptible(x) (mutex_lock(x), 0)

typedef semaphore_t os_spin_t;
#define spin_init(x)    proc_semaphoreCreate(x, 1)
#define spin_lock(x)    proc_semaphoreDown(x)
#define spin_unlock(x)  proc_semaphoreUp(x)
#define spin_destroy(x) proc_semaphoreTerminate(x)

typedef thq_t wait_queue_head_t;
#define init_waitqueue_head(wq) proc_thqCreate(wq)
/* sleep_on_spinunlock puts process to sleep on queque and then releses its' spinlock */
/* the sleepeng proces is uninterruptible (whatever this means, propably no signals) */
#define sleep_on_spinunlock(wq, spin) ({         \
	int ret;                                     \
	ret = proc_condWait(wq, spin, 0);            \
	if (ret == EOK)                              \
		spin_unlock(spin);                       \
	ret;                                         \
	})
#define signal_pending(x) (0)
/* wake_up wakes all processes on queque */
#define wake_up(wq) proc_threadWakeAll(wq)
#define destroy_waitqueue_head(wq) {}


/*********************** Lists **********************/
typedef LIST2_HEAD  os_eraseblock_list_t;
typedef LIST2_ENTRY os_eraseblock_list_el_t;
typedef LIST2_HEAD  os_compressor_list_t;
typedef LIST2_ENTRY os_compressor_list_el_t;
typedef LIST2_HEAD  os_xattr_list_el_t;
typedef LIST2_ENTRY os_xattr_list_head_t;

#define INIT_LIST_HEAD(list_head) LIST2_HEAD_INIT(list_head)
#define INIT_LIST_EL(el) LIST2_ENTRY_INIT(el)

#define list_empty(list_head) LIST2_IS_EMPTY(list_head)
#define list_add(el, el_list, list_head) LIST2_ADD(list_head, el, el_list)
#define list_add_before(el_to_add, el_list, el) LIST2_ADD_BEFORE((el), (el_to_add), el_list)
#define list_add_tail(el, el_list, list_head) LIST2_ADD_TAIL(list_head, el, el_list)
#define list_del(el, el_list, list_head) LIST2_REMOVE(el, el_list)

#define list_entry(entry, type, el_list) LIST2_ENTRY2ELEM(entry, el_list, type)
#define list_first(list_head) ((list_head)->next)
#define on_list(el, el_list, list_head) ({                        \
	LIST2_ENTRY *_entry;                                           \
	LIST2_FIND_ENTRY(list_head, _entry, _entry == &((el)->el_list)); \
	_entry == &((el)->el_list); })

#define list_pop_first(el_type, el_list, list_head) ({                  \
	el_type *_el = list_entry(list_first(list_head), el_type, el_list); \
	list_del(_el, el_list, list_head);                                  \
	_el; })

#define list_move_tail(el, el_list, list_head_from , list_head_to) ({ \
	list_del(el, el_list, list_head_from);                            \
	list_add_tail(el, el_list, list_head_to);})

#define list_for_each_entry(iter, list_head, el_list) LIST2_FOR_EACH(list_head, iter, el_list)

#define rotate_list(list_head, el_list, count) ({   \
	int _count = count;                             \
	LIST2_ENTRY *_first = list_head->next;           \
	LIST2_REMOVE_ENTRY(list_head);                  \
	while(_count--)                                 \
		_first = _first->next;                      \
	LIST2_ADD_ENTRY_BEFORE(_first, list_head);      \
})

#define count_list(list_head, el_list) ({          \
	int _i = 0;                                    \
	LIST2_ENTRY *_tmp;                             \
	LIST2_FOR_EACH_ENTRY(list_head, _tmp) _i++;         \
	_i;                                            \
})


/********************* RB Tree *********************/
typedef tree_rb_entry_t os_tree_node_t;
typedef tree_rb_root_t os_tree_root_t;

#define OS_TREE_ROOT ((os_tree_root_t) TREE_RB_ROOT_INITIALIZER)
#define rb_node(root)   TREE_RB_ROOT_ENTRY(root)
#define rb_right(node)  TREE_RB_RIGHT_ENTRY(node)
#define rb_left(node)   TREE_RB_LEFT_ENTRY(node)
#define rb_parent(node) TREE_RB_PARENT_ENTRY(node)
#define rb_first(root)  TREE_RB_MIN_ENTRY(root)
#define rb_last(root)   TREE_RB_MAX_ENTRY(root)
#define rb_prev(node)   TREE_RB_PREV_ENTRY(node)
#define rb_next(node)   TREE_RB_NEXT_ENTRY(node)

#define rb_entry(ptr, type, member) TREE_RB_ENTRY2ELEM_SAFE(ptr, member, type)

#define rbtree_postorder_for_each_entry_safe(pos, n, root, field) \
	TREE_RB_FOR_EACH_POST_SAFE(root, pos, n, field)

#define rb_replace_node(old, new, root) TREE_RB_REPLACE_ENTRY(root, old, new)

/* Trivial function to remove the last node in the tree. Which by definition
   has no right-hand child — so can be removed just by making its left-hand
   child (if any) take its place under its parent. Since this is only done
   when we're consuming the whole tree, there's no need to use rb_erase()
   and let it worry about adjusting colours and balancing the tree. That
   would just be a waste of time. */
#define eat_last(root, node) ({      \
	os_tree_node_t *__node = (node); \
	TREE_BST_REMOVE_ENTRY_WITH_ONE_SUBTREE((tree_bst_root_t *)root, &__node->bst, &TREE_RB_LEFT_ENTRY(__node)->bst);})
#define rb_erase(node, root) TREE_RB_REMOVE_ENTRY(root, node)

#define rb_link_node(node, parent, rb_link) TREE_RB_INSERT_ENTRY(node, parent, rb_link)
#define rb_insert_color(node, root) TREE_RB_FIX_BALANCE(root, node)


/*********************** Memory **********************/
#define malloc(size) vm_kmalloc(size)
#define free(size) vm_kfree(size)
#define mtd_kmalloc_up_to(mtd, size) vm_kmalloc(*(size))

static inline void *os_kzalloc(size_t size)
{
	void *ret = vm_kmalloc(size);
	if (ret != NULL) {
		memset(ret, 0, size);
	}
	return ret;
}
#define kzalloc(size, flags) os_kzalloc(size)
#define kmalloc(size, flags) os_kzalloc(size)
#define kfree(size) vm_kfree(size)


/*********************** Other **********************/
#define jffs2_is_readonly(c) (((os_priv_data *)((c)->os_priv))->isReadonly)
#define crc32(seed, data, datalen) main_crc32(seed, (unsigned char *) (data), datalen)
#define jiffies timesys_getJiffies()
/* capable determines if this is root or privilleged user
 * and allows writting to near full flash */
#define capable(x) (0)

#define DT_UNKNOWN      000
#define DT_FIFO         (S_IFIFO >> 12)
#define DT_CHR          (S_IFCHR >> 12)
#define DT_DIR          (S_IFDIR >> 12)
#define DT_BLK          (S_IFBLK >> 12)
#define DT_REG          (S_IFREG >> 12)
#define DT_LNK          (S_IFLNK >> 12)
#define DT_SOCK         (S_IFSOCK >> 12)   
#define DT_WHT          (S_IFMT >> 12)
#define os_mode_to_type(mode) ((mode & S_IFMT) >> 12)


/********************** Threads **********************/
#define jffs2_garbage_collect_trigger(c) ({wake_up(&(c)->gc_task);})


#endif
