/*
 * Phoenix-RTOS
 *
 * dummyfs - directory operations
 *
 * Copyright 2018 Phoenix Systems
 * Author: Pawel Pisarczyk, Kamil Amanowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/msg.h>
#include <phoenix/stat.h>

#include "dummyfs.h"

static int dir_find(const dummyfs_object_t *dir, const char *name, const size_t len, dummyfs_object_t **o)
{

	dummyfs_dirent_t *e = dir->entries;

	if (!S_ISDIR(dir->mode) || o == NULL)
		return -EINVAL;

	if (e == NULL)
		return -ENOENT;

	/* Iterate over all entries to find the matching one */
	do {
		if (e->len - 1 == len && !strncmp(e->name, name, len) && !e->deleted) {
			*o = e->o;
			return EOK;
		}

		e = e->next;
	} while (e != dir->entries);

	return -ENOENT;
}


int dir_findId(const dummyfs_object_t *dir, const char *name, const size_t len, id_t *resId, mode_t *mode)
{

	dummyfs_object_t *o;
	int err = -ENOENT;

	err = dir_find(dir, name, len, &o);

	if (err == EOK) {
		*resId = o->id;
		if (mode) {
			*mode = o->mode;
			if (OBJ_IS_MOUNT(o) || OBJ_IS_MNTPOINT(o))
				*mode |= S_IFMNT;
		}
	}

	return err;
}



int dir_replace(dummyfs_object_t *dir, const char *name, const size_t len, dummyfs_object_t *o)
{

	dummyfs_dirent_t *e = dir->entries;

	if (!S_ISDIR(dir->mode) || o == NULL)
		return -EINVAL;

	/* Iterate over all entries to find the matching one */
	do {
		if ((e->len - 1 == len) && !strncmp(e->name, name, len)) {
			e->o = o;
			return EOK;
		}

		e = e->next;
	} while (e != dir->entries);

	return -ENOENT;
}


int dir_add(dummyfs_object_t *dir, const char *name, const size_t len, dummyfs_object_t *o)
{
	id_t resId;
	dummyfs_dirent_t *n;
	dummyfs_dirent_t *e = dir->entries;

	if (dir == NULL)
		return -EINVAL;

	if (dir_findId(dir, name, len, &resId, NULL) == EOK)
		return -EEXIST;

	if (dummyfs_incsz(sizeof(dummyfs_dirent_t)) != EOK)
		return -ENOMEM;

	n = malloc(sizeof(dummyfs_dirent_t));

	if (n == NULL) {
		dummyfs_decsz(sizeof(dummyfs_dirent_t));
		return -ENOMEM;
	}

	n->len = len + 1;
	n->deleted = 0;

	if (dummyfs_incsz(n->len) != EOK) {
		dummyfs_decsz(sizeof(dummyfs_dirent_t));
		free(n);
		return -ENOMEM;
	}

	n->name = calloc(1, n->len);

	if (n->name == NULL) {
		dummyfs_decsz(sizeof(dummyfs_dirent_t) + n->len);
		free(n);
		return -ENOMEM;
	}

	if (e == NULL) {
		dir->entries = n;
		n->next = n;
		n->prev = n;
	} else if (e == e->prev) {
		e->prev = n;
		e->next = n;
		n->next = e;
		n->prev = e;
	} else {
		e->prev->next = n;
		n->prev = e->prev;
		n->next = e;
		e->prev = n;
	}

	memcpy(n->name, name, len);
	n->o = o;
	dir->size += n->len;

	return EOK;
}


int dir_remove(dummyfs_object_t *dir, const char *name, const size_t len)
{
	dummyfs_dirent_t *e = dir->entries;

	if (e == NULL)
		return -ENOENT;

	do {
		if ((e->len - 1 == len) && !strncmp(e->name, (char *)name, len) && !e->deleted) {
			dir->size -= e->len;
			e->deleted = 1;
			OBJ_SET_DIRTY(dir);
			return EOK;
		}
		e = e->next;
	} while (e != dir->entries);

	return -ENOENT;
}


void dir_clean(dummyfs_object_t *dir)
{
	dummyfs_dirent_t *v;
	dummyfs_dirent_t *e = dir->entries;

	if (e == NULL)
		return;

	do {
		if (e->deleted) {
			e->prev->next = e->next;
			e->next->prev = e->prev;
			if (dir->entries == e)
				dir->entries = e->next;
			dummyfs_decsz(e->len + sizeof(dummyfs_dirent_t));
			v = e;
			e = e->next;

			free(v->name);
			free(v);
		} else
			e = e->next;
	} while (e != dir->entries);
	OBJ_CLR_DIRTY(dir);
}


int dir_empty(dummyfs_object_t *dir)
{
	/* clean dir before checking */
	dir_clean(dir);

	if (dir->entries->next->next != dir->entries)
		return -EBUSY;

	return EOK;
}


void dir_destroy(dummyfs_object_t *dir)
{
	if (dir_empty(dir) == EOK) {
		free(dir->entries->next->name);
		free(dir->entries->next);

		free(dir->entries->name);
		free(dir->entries);
		dir->entries = NULL;
	}
}
