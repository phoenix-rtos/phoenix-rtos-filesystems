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

#include <stdlib.h>
#include "../phoenix-rtos.h"
#include "slab.h"


struct kmem_cache *kmem_cache_create(const char *name, size_t size,
			size_t align, slab_flags_t flags,
			void (*ctor)(void *))
{
	struct kmem_cache *kmem_cache = malloc(sizeof(struct kmem_cache));

	kmem_cache->object_size = size;
	kmem_cache->align = align;

	if (align)
		kmem_cache->size = (size + align - 1) & (align - 1);
	else
		kmem_cache->size = size;

	kmem_cache->flags = flags;
	kmem_cache->refcount = 1;
	kmem_cache->ctor = ctor;

	return kmem_cache;
}


void kmem_cache_destroy(struct kmem_cache *kmem_cache)
{
	free(kmem_cache);
}


void kmem_cache_free(struct kmem_cache *kmem_cache, void *ptr)
{
	free(ptr);
}


void *kmem_cache_alloc(struct kmem_cache *kmem_cache, gfp_t flags)
{
	void *object = malloc(kmem_cache->size);
	memset(object, 0, kmem_cache->size);

	if (kmem_cache->ctor != NULL)
		kmem_cache->ctor(object);

	return object;
}
