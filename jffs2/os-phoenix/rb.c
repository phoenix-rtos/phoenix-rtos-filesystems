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
	return NULL;
}

struct rb_node *rb_last(struct rb_root *root)
{
	return NULL;
}

struct rb_node *rb_next(const struct rb_node *node)
{
	struct rb_node *parent;

	if (RB_EMPTY_NODE(node))
		return NULL;

	/*
	 * If we have a right-hand child, go down and then left as far
	 * as we can.
	 */
	if (node->rb_right) {
		node = node->rb_right;
		while (node->rb_left)
			node=node->rb_left;
		return (struct rb_node *)node;
	}

	/*
	 * No right-hand children. Everything down and left is smaller than us,
	 * so any 'next' node must be in the general direction of our parent.
	 * Go up the tree; any time the ancestor is a right-hand child of its
	 * parent, keep going up. First time it's a left-hand child of its
	 * parent, said parent is our 'next' node.
	 */
	while ((parent = rb_parent(node)) && node == parent->rb_right)
		node = parent;

	return parent;
}

struct rb_node *rb_prev(const struct rb_node *node)
{
	struct rb_node *parent;

	if (RB_EMPTY_NODE(node))
		return NULL;

	/*
	 * If we have a left-hand child, go down and then right as far
	 * as we can.
	 */
	if (node->rb_left) {
		node = node->rb_left;
		while (node->rb_right)
			node=node->rb_right;
		return (struct rb_node *)node;
	}

	/*
	 * No left-hand children. Go up till we find an ancestor which
	 * is a right-hand child of its parent.
	 */
	while ((parent = rb_parent(node)) && node == parent->rb_left)
		node = parent;

	return parent;
}


void rb_erase(struct rb_node *node, struct rb_root *root)
{
		//struct rb_node *rebalance;
		//rebalance = __rb_erase_augmented(node, root, NULL, &dummy_callbacks);
		//if (rebalance)
		//	____rb_erase_color(rebalance, root, dummy_rotate);
}

void rb_insert_color(struct rb_node *rb_node, struct rb_root *rb_root)
{
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
