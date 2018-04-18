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
#include "rb.h"

struct rb_node *rb_first(struct rb_root *root)
{
	return container_of(lib_rbMinimum(&root->rb_node->n), struct rb_node, n);
}

struct rb_node *rb_last(struct rb_root *root)
{
	return container_of(lib_rbMaximum(&root->rb_node->n), struct rb_node, n);
}

struct rb_node *rb_next(const struct rb_node *node)
{
	return container_of(lib_rbNext(&node->n), struct rb_node, n);
}

struct rb_node *rb_prev(const struct rb_node *node)
{
	return container_of(lib_rbPrev(&node->n), struct rb_node, n);
}


void rb_erase(struct rb_node *rb_node, struct rb_root *rb_root)
{
	lib_rbRemove(&rb_root->t, &rb_node->n);
}

void rb_insert_color(struct rb_node *rb_node, struct rb_root *rb_root)
{
	lib_rbInsert(&rb_root->t, &rb_node->n);
}

void rb_replace_node(struct rb_node *victim, struct rb_node *new, struct rb_root *root)
{
}

struct rb_node *rb_next_postorder(const struct rb_node *node)
{
	return NULL;
}


struct rb_node *rb_first_postorder(const struct rb_root *root)
{
	return NULL;
}
