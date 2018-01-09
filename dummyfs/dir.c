/*
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * dummyfs - directory operations
 *
 * Copyright 2018 Phoenix Systems
 * Author: Pawel Pisarczyk
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/msg.h>

#include "dummyfs.h"


int dir_find(dummyfs_object_t *dir, const char *name, oid_t *res)
{

	dummyfs_dirent_t *e = dir->entries;
	/* Iterate over all entries to find the matching one */
	do {
		if (!strcmp(e->name, (char *)name)) {
			memcpy(res, &e->oid, sizeof(oid_t));
			return EOK;
		}

		e = e->next;
	} while (e != dir->entries);

	return -ENOENT;
}

int dir_add(dummyfs_object_t *dir, const char *name, oid_t *oid)
{
	oid_t res;
	dummyfs_dirent_t *n;
	dummyfs_dirent_t *e = dir->entries;

	if (dir_find(dir, name, &res) == EOK)
		return -EEXIST;

	n = malloc(sizeof(dummyfs_dirent_t));

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

	n->len = strlen(name);
	n->name = malloc(n->len);
	memcpy(n->name, name, n->len);
	memcpy(&n->oid, oid, sizeof(oid_t));
	dir->size += sizeof(dummyfs_dirent_t) + n->len;

	return EOK;
}

int dir_remove(dummyfs_object_t *dir, const char *name)
{
	dummyfs_dirent_t *e = dir->entries;

	if (e == NULL)
		return -ENOENT;

	if (e == e->next) {
		if (!strcmp(e->name, (char *)name)) {
			dir->entries = NULL;
			free(e->name);
			free(e);
			dir->size = 0;
			return EOK;
		}
		return -ENOENT;
	}

	do {
		if (!strcmp(e->name, (char *)name)) {
			e->prev->next = e->next;
			e->next->prev = e->prev;
			dir->size -= sizeof(dummyfs_dirent_t) + e->len;
			free(e->name);
			free(e);
			return EOK;
		}
		e = e->next;
	} while (e != dir->entries);

	return -ENOENT;
}
