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
#include <sys/msg.h>
#include <string.h>

#include "dummyfs.h"


int dir_find(dummyfs_object_t *dir, const char *name, oid_t *res)
{

	dummyfs_dirent_t *e = dir->entries;
	char *dirname = strdup(name);
	char *end = strchr(dirname, '/');
	int len;

	if (dir->type != otDir)
		return -EINVAL;

	if (e == NULL)
		return -ENOENT;

	if (end != NULL)
		*end = 0;

	len = strlen(dirname);

	/* Iterate over all entries to find the matching one */
	do {
		if (!strcmp(e->name, dirname)) {
			memcpy(res, &e->oid, sizeof(oid_t));
			free(dirname);
			return len;
		}

		e = e->next;
	} while (e != dir->entries);

	free(dirname);
	return -ENOENT;
}

int dir_replace(dummyfs_object_t *dir, const char *name, oid_t *new)
{

	dummyfs_dirent_t *e = dir->entries;
	char *dirname = strdup(name);
	char *end = strchr(dirname, '/');
	int len;

	if (dir->type != otDir)
		return -EINVAL;

	if (e == NULL)
		return -ENOENT;

	if (end != NULL)
		*end = 0;

	len = strlen(dirname);

	/* Iterate over all entries to find the matching one */
	do {
		if (!strcmp(e->name, dirname)) {
			memcpy(&e->oid, new, sizeof(oid_t));
			free(dirname);
			return EOK;
		}

		e = e->next;
	} while (e != dir->entries);

	free(dirname);
	return -ENOENT;
}

int dir_add(dummyfs_object_t *dir, const char *name, int type, oid_t *oid)
{
	oid_t res;
	dummyfs_dirent_t *n;
	dummyfs_dirent_t *e = dir->entries;

	if (dir == NULL)
		return -EINVAL;

	if (dir_find(dir, name, &res) >= 0)
		return -EEXIST;

	if (dummyfs_incsz(sizeof(dummyfs_dirent_t)) != EOK)
		return -ENOMEM;

	n = malloc(sizeof(dummyfs_dirent_t));

	if (n == NULL) {
		dummyfs_decsz(sizeof(dummyfs_dirent_t));
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

	n->len = strlen(name) + 1;

	if (dummyfs_incsz(n->len) != EOK) {
		dummyfs_decsz(sizeof(dummyfs_dirent_t));
		free(n);
		return -ENOMEM;
	}

	n->name = malloc(n->len);

	if (n->name == NULL) {
		dummyfs_decsz(sizeof(dummyfs_dirent_t) + n->len);
		free(n);
		return -ENOMEM;
	}

	memcpy(n->name, name, n->len);
	n->name[n->len - 1] = '\0';
	memcpy(&n->oid, oid, sizeof(oid_t));
	n->type = type;
	dir->size += strlen(name);

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
			return EOK;
		}
		return -ENOENT;
	}

	do {
		if (!strcmp(e->name, (char *)name)) {
			e->prev->next = e->next;
			e->next->prev = e->prev;
			dummyfs_decsz(e->len + sizeof(dummyfs_dirent_t));
			free(e->name);
			free(e);
			dir->size -= strlen(name);
			return EOK;
		}
		e = e->next;
	} while (e != dir->entries);

	return -ENOENT;
}

int dir_empty(dummyfs_object_t *dir)
{
	if (dir->entries == NULL)
		return EOK;

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
