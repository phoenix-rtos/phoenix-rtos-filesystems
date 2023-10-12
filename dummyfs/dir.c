/*
 * Phoenix-RTOS
 *
 * dummyfs - directory operations
 *
 * Copyright 2018, 2023 Phoenix Systems
 * Author: Pawel Pisarczyk, Kamil Amanowicz, Aleksander Kaminski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/list.h>
#include <string.h>
#include <dirent.h>
#include <assert.h>

#include "dummyfs_internal.h"
#include "memory.h"
#include "dir.h"


static int dummyfs_dir_compare(rbnode_t *n1, rbnode_t *n2)
{
	dummyfs_dirent_t *i1 = lib_treeof(dummyfs_dirent_t, linkage, n1);
	dummyfs_dirent_t *i2 = lib_treeof(dummyfs_dirent_t, linkage, n2);

	if (i1->key > i2->key) {
		return 1;
	}
	else if (i1->key < i2->key) {
		return -1;
	}
	else {
		return 0;
	}
}


static uint32_t dummyfs_dir_hash(const char *name, size_t len)
{
	TRACE();
	uint32_t key = 0;
	size_t i = 0;
	const char *end = name + len;

	for (const char *s = name; s != end; ++s) {
		key += (uint32_t)(*s) << (i & 0xf);
		++i;
	}

	return key;
}


static dummyfs_dirent_t *dummyfs_dir_get(dummyfs_object_t *dir, const char *name)
{
	TRACE();
	size_t len = strchrnul(name, '/') - name;
	dummyfs_dirent_t t = { .key = dummyfs_dir_hash(name, len) };
	dummyfs_dirent_t *e = lib_treeof(dummyfs_dirent_t, linkage, lib_rbFind(&dir->dir.tree, &t.linkage));

	while (e != NULL) {
		if ((e->len == len) && (strncmp(e->name, name, len) == 0)) {
			break;
		}
		e = e->next;
	}

	return e;
}


int dummyfs_dir_find(dummyfs_object_t *dir, const char *name, oid_t *res)
{
	TRACE();
	if (!S_ISDIR(dir->mode)) {
		return -ENOTDIR;
	}

	dummyfs_dirent_t *e = dummyfs_dir_get(dir, name);
	if (e != NULL) {
		*res = e->oid;
	}

	return (e == NULL) ? -ENOENT : (int)e->len;
}


int dummyfs_dir_replace(dummyfs_object_t *dir, const char *name, oid_t *new)
{
	TRACE();
	if (!S_ISDIR(dir->mode)) {
		return -ENOTDIR;
	}

	dummyfs_dirent_t *e = dummyfs_dir_get(dir, name);
	if (e != NULL) {
		e->oid = *new;
	}

	return (e == NULL) ? -ENOENT : 0;
}


int dummyfs_dir_add(dummyfs_t *ctx, dummyfs_object_t *dir, const char *name, uint32_t mode, oid_t *oid)
{
	TRACE();
	size_t len;
	char *dname = dummyfs_strdup(ctx, name, &len);
	if (dname == NULL) {
		return -ENOMEM;
	}

	uint32_t key = dummyfs_dir_hash(name, len);
	dummyfs_dirent_t t = { .key = key };
	dummyfs_dirent_t *e = lib_treeof(dummyfs_dirent_t, linkage, lib_rbFind(&dir->dir.tree, &t.linkage));
	dummyfs_dirent_t *prev = NULL;

	while (e != NULL) {
		prev = e;
		if ((e->len == len) && (strcmp(e->name, name) == 0)) {
			return -EEXIST;
		}
		e = e->next;
	}

	dummyfs_dirent_t *n = dummyfs_malloc(ctx, sizeof(dummyfs_dirent_t));
	if (n == NULL) {
		dummyfs_free(ctx, dname, len + 1);
		return -ENOMEM;
	}

	n->oid = *oid;
	n->key = key;
	n->next = NULL;
	n->prev = prev;
	n->name = dname;
	n->len = len;

	if (prev != NULL) {
		prev->next = n;
	}
	else {
		lib_rbInsert(&dir->dir.tree, &n->linkage);
	}

	if (S_ISDIR(mode)) {
		n->type = DT_DIR;
	}
	else if (S_ISREG(mode)) {
		n->type = DT_REG;
	}
	else if (S_ISCHR(mode)) {
		n->type = DT_CHR;
	}
	else if (S_ISBLK(mode)) {
		n->type = DT_BLK;
	}
	else if (S_ISLNK(mode)) {
		n->type = DT_LNK;
	}
	else {
		n->type = DT_UNKNOWN;
	}
	dir->size += sizeof(dummyfs_dirent_t) + n->len + 1;

	dir->dir.entries++;

	/* Invalidate ls hint */
	dir->dir.hint.entry = NULL;

	return 0;
}


static void dummyfs_dir_removeEntry(dummyfs_t *ctx, dummyfs_object_t *dir, dummyfs_dirent_t *e)
{
	TRACE();
	if (e->prev == NULL) {
		lib_rbRemove(&dir->dir.tree, &e->linkage);
		if (e->next != NULL) {
			lib_rbInsert(&dir->dir.tree, &e->next->linkage);
			e->next->prev = NULL;
		}
	}
	else {
		e->prev->next = e->next;
		if (e->next != NULL) {
			e->next->prev = e->prev;
		}
	}

	dir->size -= sizeof(dummyfs_dirent_t) + e->len + 1;

	dummyfs_free(ctx, e->name, e->len + 1);
	dummyfs_free(ctx, e, sizeof(dummyfs_dirent_t));

	assert(dir->dir.entries > 0);
	dir->dir.entries--;

	/* Invalidate ls hint */
	dir->dir.hint.entry = NULL;
}


int dummyfs_dir_remove(dummyfs_t *ctx, dummyfs_object_t *dir, const char *name)
{
	TRACE();
	dummyfs_dirent_t *e = dummyfs_dir_get(dir, name);
	if (e != NULL) {
		dummyfs_dir_removeEntry(ctx, dir, e);
		return 0;
	}

	return -ENOENT;
}


int dummyfs_dir_empty(dummyfs_t *ctx, dummyfs_object_t *dir)
{
	TRACE();
	(void)ctx;
	return (dir->dir.entries <= 2) ? 0 : -EBUSY;
}


void dummyfs_dir_destroy(dummyfs_t *ctx, dummyfs_object_t *dir)
{
	TRACE();
	assert(dummyfs_dir_empty(ctx, dir) == 0);

	for (;;) {
		dummyfs_dirent_t *e = lib_treeof(dummyfs_dirent_t, linkage, lib_rbMinimum(dir->dir.tree.root));
		if (e == NULL) {
			break;
		}

		dummyfs_dir_removeEntry(ctx, dir, e);
	}
}


int dummyfs_dir_init(dummyfs_t *ctx, dummyfs_object_t *dir)
{
	TRACE();
	(void)ctx;
	lib_rbInit(&dir->dir.tree, dummyfs_dir_compare, NULL);
	dir->dir.entries = 0;
	dir->dir.hint.entry = NULL;
	dir->dir.hint.offs = 0;
	return 0;
}
