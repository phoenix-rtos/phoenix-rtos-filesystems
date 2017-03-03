/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2001-2003 Free Software Foundation, Inc.
 *
 * Created by David Woodhouse <dwmw2@cambridge.redhat.com>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 * $Id: malloc-ecos.c,v 1.4 2003/11/26 15:55:35 dwmw2 Exp $
 *
 */

#include "nodelist.h"
#include "os-phoenix.h"

#ifdef JFFS2_DEBUG_MEMORY
	int jffs2_tmp_dnode_info_count = 0;
	int jffs2_raw_node_ref_count   = 0;
	int jffs2_inode_cache_count    = 0;
	int jffs2_full_dirent_count    = 0;
	int jffs2_inode_info_count     = 0;
	int jffs2_full_dnode_count     = 0;
	int jffs2_raw_dirent_count     = 0;
	int jffs2_raw_inode_count      = 0;
	int jffs2_node_frag_count      = 0;
	int jffs2_refblock_count       = 0;
	int jffs2_sb_info_count        = 0;

void jffs2_debug_dump_memory_status(void) {
	pr_debug("sb=%d, tmp=%d, raw_dir=%d, raw_i=%d, f_dir=%d, f_dnode=%d, ic=%d, ii=%d, frag=%d, ref=%d, refbl=%d",
	jffs2_sb_info_count,       
	jffs2_tmp_dnode_info_count,
	jffs2_raw_dirent_count,    
	jffs2_raw_inode_count,    
	jffs2_full_dirent_count,   
	jffs2_full_dnode_count,    
	jffs2_inode_cache_count,   
	jffs2_inode_info_count,    
	jffs2_node_frag_count,     
	jffs2_raw_node_ref_count,  
	jffs2_refblock_count);
}
#endif

struct jffs2_sb_info *jffs2_alloc_sb_info(void)
{
	struct jffs2_sb_info *c;
	int ret;

	c = kzalloc(sizeof(struct jffs2_sb_info), PG_KERNEL);
	if (c == NULL)
		return NULL;

	if ((ret = mutex_init(&c->alloc_sem)) != 0)
		goto alloc_sem_failed;
	if ((ret = mutex_init(&c->erase_free_sem)) != 0)
		goto erase_free_sem_failed;
	if ((ret = spin_init(&c->erase_completion_lock)) != 0)
		goto erase_completion_failed;
	if ((ret = spin_init(&c->inocache_lock)) != 0)
		goto inocache_lock_failed;
	
	init_waitqueue_head(&c->erase_wait);
	init_waitqueue_head(&c->inocache_wq);
	init_waitqueue_head(&c->gc_task);

	c->inocache_list = NULL;

#ifdef JFFS2_DEBUG_MEMORY
	jffs2_sb_info_count++;
#endif
	return c;

inocache_lock_failed:
	spin_destroy(&c->erase_completion_lock);
erase_completion_failed:
	mutex_destroy(&c->erase_free_sem);
erase_free_sem_failed:
	mutex_destroy(&c->alloc_sem);
alloc_sem_failed:
	free(c);

	return NULL;
}

void jffs2_free_sb_info(struct jffs2_sb_info *c)
{
	if (c->inocache_list != NULL)
		free(c->inocache_list);
	spin_destroy(&c->inocache_lock);
	spin_destroy(&c->erase_completion_lock);
	destroy_waitqueue_head(&c->inocache_wq);
	destroy_waitqueue_head(&c->erase_wait);
	destroy_waitqueue_head(&c->gc_task);
	mutex_destroy(&c->erase_free_sem);
	mutex_destroy(&c->alloc_sem);
	free(c);
#ifdef JFFS2_DEBUG_MEMORY
	jffs2_sb_info_count--;
#endif
}

struct jffs2_inode_info *jffs2_alloc_inode_info(void)
{
	struct jffs2_inode_info *res;
	int ret;

	if ((res = kzalloc(sizeof(struct jffs2_inode_info), PG_KERNEL)) == NULL) {
		return NULL;
	}
	if ((ret = mutex_init(&res->sem)) != EOK) {
		free(res);
		return NULL;
	}
#ifdef JFFS2_DEBUG_MEMORY
	jffs2_inode_info_count++;
#endif
	return res;
}

void jffs2_free_inode_info(struct jffs2_inode_info *x)
{
	mutex_destroy(&x->sem);
	free(x);
#ifdef JFFS2_DEBUG_MEMORY
	jffs2_inode_info_count--;
#endif
}

struct jffs2_full_dirent *jffs2_alloc_full_dirent(int namesize)
{
#ifdef JFFS2_DEBUG_MEMORY
	jffs2_full_dirent_count++;
#endif
	return malloc(sizeof(struct jffs2_full_dirent) + namesize);
}

void jffs2_free_full_dirent(struct jffs2_full_dirent *x)
{
	free(x);
#ifdef JFFS2_DEBUG_MEMORY
	jffs2_full_dirent_count--;
#endif
}

struct jffs2_full_dnode *jffs2_alloc_full_dnode(void)
{
#ifdef JFFS2_DEBUG_MEMORY
	jffs2_full_dnode_count++;
#endif
	return malloc(sizeof(struct jffs2_full_dnode));
}

void jffs2_free_full_dnode(struct jffs2_full_dnode *x)
{
	free(x);
#ifdef JFFS2_DEBUG_MEMORY
	jffs2_full_dnode_count--;
#endif
}

struct jffs2_raw_dirent *jffs2_alloc_raw_dirent(void)
{
#ifdef JFFS2_DEBUG_MEMORY
	jffs2_raw_dirent_count++;
#endif
	return malloc(sizeof(struct jffs2_raw_dirent));
}

void jffs2_free_raw_dirent(struct jffs2_raw_dirent *x)
{
#ifdef JFFS2_DEBUG_MEMORY
	jffs2_raw_dirent_count--;
#endif
	free(x);
}

struct jffs2_raw_inode *jffs2_alloc_raw_inode(void)
{
#ifdef JFFS2_DEBUG_MEMORY
	jffs2_raw_inode_count++;
#endif
	return malloc(sizeof(struct jffs2_raw_inode));
}

void jffs2_free_raw_inode(struct jffs2_raw_inode *x)
{
	free(x);
#ifdef JFFS2_DEBUG_MEMORY
	jffs2_raw_inode_count--;
#endif
}

struct jffs2_tmp_dnode_info *jffs2_alloc_tmp_dnode_info(void)
{
#ifdef JFFS2_DEBUG_MEMORY
	jffs2_tmp_dnode_info_count++;
#endif
	return malloc(sizeof(struct jffs2_tmp_dnode_info));
}

void jffs2_free_tmp_dnode_info(struct jffs2_tmp_dnode_info *x)
{
#ifdef JFFS2_DEBUG_MEMORY
	jffs2_tmp_dnode_info_count--;
#endif
	free(x);
}

struct jffs2_node_frag *jffs2_alloc_node_frag(void)
{
#ifdef JFFS2_DEBUG_MEMORY
	jffs2_node_frag_count++;
#endif
	return malloc(sizeof(struct jffs2_node_frag));
}

void jffs2_free_node_frag(struct jffs2_node_frag *x)
{
	free(x);
#ifdef JFFS2_DEBUG_MEMORY
	jffs2_node_frag_count--;
#endif
}

int jffs2_create_slab_caches(void)
{
	return 0;
}

void jffs2_destroy_slab_caches(void)
{
}

struct jffs2_raw_node_ref *jffs2_alloc_raw_node_ref(void)
{
#ifdef JFFS2_DEBUG_MEMORY
	jffs2_raw_node_ref_count++;
#endif
	return malloc(sizeof(struct jffs2_raw_node_ref));
}

void jffs2_free_raw_node_ref(struct jffs2_raw_node_ref *x)
{
	free(x);
#ifdef JFFS2_DEBUG_MEMORY
	jffs2_raw_node_ref_count--;
#endif
}

static struct jffs2_raw_node_ref *jffs2_alloc_refblock(void)
{
	struct jffs2_raw_node_ref *ret;

	ret = malloc(sizeof(struct jffs2_raw_node_ref) * REFS_PER_BLOCK);
	if (ret) {
		int i = 0;
		for (i=0; i < REFS_PER_BLOCK; i++) {
			ret[i].flash_offset = REF_EMPTY_NODE;
			ret[i].next_in_ino = NULL;
		}
		ret[i].flash_offset = REF_LINK_NODE;
		ret[i].next_in_ino = NULL;
	}
#ifdef JFFS2_DEBUG_MEMORY
	jffs2_refblock_count++;
#endif
	return ret;
}

void jffs2_free_refblock(struct jffs2_raw_node_ref *x)
{
	free(x);
#ifdef JFFS2_DEBUG_MEMORY
	jffs2_refblock_count--;
#endif
}

int jffs2_prealloc_raw_node_refs(struct jffs2_sb_info *c,
                                 struct jffs2_eraseblock *jeb, int nr)
{                       
	struct jffs2_raw_node_ref **p, *ref;
	int i = nr;
                
	dbg_memalloc("%d\n", nr);
                
	p = &jeb->last_node;
	ref = *p;
                        
	dbg_memalloc("Reserving %d refs for block @0x%x\n", nr, jeb->offset);

	/* If jeb->last_node is really a valid node then skip over it */
	if (ref && ref->flash_offset != REF_EMPTY_NODE)
		ref++;
        
	while (i) {
		if (!ref) {
			dbg_memalloc("Allocating new refblock linked from 0x%x\n", p);
			ref = *p = jffs2_alloc_refblock();
			if (!ref) 
				return -ENOMEM;
		}
		if (ref->flash_offset == REF_LINK_NODE) {
			p = &ref->next_in_ino;
			ref = *p;
			continue;
		}       
		i--;
		ref++;
	}
	jeb->allocated_refs = nr;
                        
	dbg_memalloc("Reserved %d refs for block @0x%x, last_node is 0x%x (%x,0x%x)\n",
		nr, jeb->offset, jeb->last_node, jeb->last_node->flash_offset,
		jeb->last_node->next_in_ino);

	return 0;
}

struct jffs2_inode_cache *jffs2_alloc_inode_cache(void)
{
	struct jffs2_inode_cache *ret = malloc(sizeof(struct jffs2_inode_cache));
	dbg_memalloc("Allocated inocache at 0x%x\n", ret);
#ifdef JFFS2_DEBUG_MEMORY
	jffs2_inode_cache_count++;
#endif
	return ret;
}

void jffs2_free_inode_cache(struct jffs2_inode_cache *x)
{
	dbg_memalloc("Freeing inocache at 0x%x\n", x);
	free(x);
#ifdef JFFS2_DEBUG_MEMORY
	jffs2_inode_cache_count--;
#endif
}
