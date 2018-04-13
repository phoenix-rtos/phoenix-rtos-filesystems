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

#include "../os-phoenix.h"
#include "slab.h"

struct kmem_cache *kmem_cache_create(const char *name, size_t size,
			size_t align, slab_flags_t flags,
			void (*ctor)(void *))
{
	return NULL;
}

void kmem_cache_destroy(struct kmem_cache *kmem_cache)
{
}

void kmem_cache_free(struct kmem_cache *kmem_cache, void *ptr)
{
}

void *kmem_cache_alloc(struct kmem_cache *kmem_cache, gfp_t flags)
{
	return NULL;
}
