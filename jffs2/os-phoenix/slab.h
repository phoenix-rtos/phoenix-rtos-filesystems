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


#ifndef _OS_PHOENIX_SLAB_H_
#define _OS_PHOENIX_SLAB_H_

/* definitions taken from Linux kernel */

struct kmem_cache {
	unsigned int object_size;/* The original size of the object */
	unsigned int size;	/* The aligned/padded/added on size  */
	unsigned int align;	/* Alignment as calculated */
	slab_flags_t flags;	/* Active flags on the slab */
	size_t useroffset;	/* Usercopy region offset */
	size_t usersize;	/* Usercopy region size */
	const char *name;	/* Slab name for sysfs */
	int refcount;		/* Use counter */
	void (*ctor)(void *);	/* Called on object slot creation */
	struct list_head list;	/* List of all slab caches on the system */
};

#define SLAB_HWCACHE_ALIGN	((slab_flags_t)0x00002000U)
#define SLAB_ACCOUNT 0
#define SLAB_MEM_SPREAD 0
#define SLAB_RECLAIM_ACCOUNT 0

extern struct kmem_cache *kmem_cache_create(const char *name, size_t size,
			size_t align, slab_flags_t flags,
			void (*ctor)(void *));

extern void kmem_cache_destroy(struct kmem_cache *kmem_cache);

extern void kmem_cache_free(struct kmem_cache *kmem_cache, void *ptr);

extern void *kmem_cache_alloc(struct kmem_cache *kmem_cache, gfp_t flags);

#endif /* _OS_PHOENIX_SLAB_H_ */

