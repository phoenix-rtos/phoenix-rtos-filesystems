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
	rb_node->n.color = RB_RED;
	lib_rbInsertBalance(&rb_root->t, &rb_node->n);
}


static inline void rb_set_parent(struct rb_node *node, struct rb_node *parent)
{
	node->n.parent = &parent->n;
}

/* functions below are taken from Linux kernel */
void rb_replace_node(struct rb_node *victim, struct rb_node *new,
		     struct rb_root *root)
{
	struct rb_node *parent = rb_parent(victim);

	/* Copy the pointers/colour from the victim to the replacement */
	*new = *victim;

	/* Set the surrounding nodes to point to the replacement */
	if (rb_left(victim)) {
		rb_left(victim)->n.parent = &new->n;
		rb_set_parent(victim->rb_left, new);
	}
	if (rb_right(victim)) {
		rb_right(victim)->n.parent = &new->n;
		rb_set_parent(victim->rb_right, new);
	}

	if (parent) {
		if (parent->rb_left == victim)
			parent->rb_left = new;
		else
			parent->rb_right = new;
	} else
		root->rb_node = new;
}


static struct rb_node *rb_left_deepest_node(const struct rb_node *node)
{
	for (;;) {
		if (rb_left(node))
			node = rb_left(node);
		else if (rb_right(node))
			node = rb_right(node);
		else
			return (struct rb_node *)node;
	}
}


struct rb_node *rb_next_postorder(const struct rb_node *node)
{
	const struct rb_node *parent;
	if (!node)
		return NULL;
	parent = rb_parent(node);

	/* If we're sitting on node, we've already seen our children */
	if (parent && node == rb_left(parent) && rb_right(parent)) {
		/* If we are the parent's left node, go to the parent's right
		 * node then all the way down to the left */
		return rb_left_deepest_node(rb_right(parent));
	} else
		/* Otherwise we are the parent's right node, and the parent
		 * should be next */
		return (struct rb_node *)parent;
}


struct rb_node *rb_first_postorder(const struct rb_root *root)
{
	if (root->rb_node == NULL)
		return NULL;

	return rb_left_deepest_node(root->rb_node);
}
