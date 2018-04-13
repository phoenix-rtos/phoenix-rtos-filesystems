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


#ifndef _OS_PHOENIX_RB_H_
#define _OS_PHOENIX_RB_H_

#define RB_EMPTY_NODE(node)  \
	((node)->__rb_parent_color == (unsigned long)(node))

#define RB_ROOT	(struct rb_root) { NULL, }

#define rb_parent(r)   ((struct rb_node *)((r)->__rb_parent_color & ~3))


struct rb_node {
	rbnode_t n;
	struct rb_node *rb_left;
	struct rb_node *rb_right;
	unsigned long  __rb_parent_color;
};


struct rb_root {
	rbtree_t t;
	struct rb_node *rb_node;
};


struct rb_node *rb_first(struct rb_root *root);

struct rb_node *rb_last(struct rb_root *root);


#define rb_entry(ptr, type, member) lib_treeof(type, member, &(ptr->n))


struct rb_node *rb_next(const struct rb_node *node);

struct rb_node *rb_prev(const struct rb_node *node);

void rb_erase(struct rb_node *node, struct rb_root *root);

static inline void rb_link_node(struct rb_node *node, struct rb_node *parent,
						struct rb_node **rb_link)
{
		node->__rb_parent_color = (unsigned long)parent;
		node->rb_left = node->rb_right = NULL;

		*rb_link = node;
}

void rb_insert_color(struct rb_node *rb_node, struct rb_root *rb_root);

void rb_replace_node(struct rb_node *victim, struct rb_node *new, struct rb_root *root);

struct rb_node *rb_next_postorder(const struct rb_node *node);

struct rb_node *rb_first_postorder(const struct rb_root *root);

#define rb_entry_safe(ptr, type, member) \
	({ typeof(ptr) ____ptr = (ptr); \
	   ____ptr ? rb_entry(____ptr, type, member) : NULL; \
	})


#define rbtree_postorder_for_each_entry_safe(pos, n, root, field) \
	for (pos = rb_entry_safe(rb_first_postorder(root), typeof(*pos), field); \
	     pos && ({ n = rb_entry_safe(rb_next_postorder(&pos->field), \
			typeof(*pos), field); 1; }); \
	     pos = n)


#endif /* _OS_PHOENIX_RB_H_ */
