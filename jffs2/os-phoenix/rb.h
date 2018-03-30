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


struct rb_root {
	rbtree_t t;
};


struct rb_node {
	rbnode_t n;
};


struct rb_node *rb_first(struct rb_root *root)
{
	return NULL;
}

struct rb_node *rb_last(struct rb_root *root)
{
	return NULL;
}


#define rb_entry(ptr, type, member) lib_treeof(type, member, &ptr->n)


#endif /* _OS_PHOENIX_RB_H_ */
