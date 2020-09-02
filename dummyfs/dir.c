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
#include <sys/file.h>
#include <sys/stat.h>
#include <string.h>

#include "dummyfs.h"


int dir_find(dummyfs_object_t *dir, const char *name, oid_t *res)
{

	dummyfs_dirent_t *e = dir->entries;
	char *dirname = strdup(name);
	char *end = strchr(dirname, '/');
	int len;

	if (!S_ISDIR(dir->mode))
		return -EINVAL;

	if (e == NULL)
		return -ENOENT;

	if (end != NULL)
		*end = 0;

	len = strlen(dirname);

	/* Iterate over all entries to find the matching one */
	do {
		if (!strcmp(e->name, dirname) && !e->deleted) {
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

	if (!S_ISDIR(dir->mode))
		return -EINVAL;

	if (e == NULL)
		return -ENOENT;

	if (end != NULL)
		*end = 0;

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


int dir_add(dummyfs_object_t *dir, const char *name, uint32_t mode, oid_t *oid)
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

	n->len = strlen(name) + 1;
	n->deleted = 0;

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

	memcpy(n->name, name, n->len);
	n->name[n->len - 1] = '\0';
	memcpy(&n->oid, oid, sizeof(oid_t));
	if (S_ISDIR(mode))
		n->type = otDir;
	else if (S_ISREG(mode))
		n->type = otFile;
	else if (S_ISCHR(mode) || S_ISBLK(mode))
		n->type = otDev;
	else if (S_ISLNK(mode))
		n->type = otSymlink;
	else
		n->type = otUnknown;
	dir->size += strlen(name);

	return EOK;
}


int dir_remove(dummyfs_object_t *dir, const char *name)
{
	dummyfs_dirent_t *e = dir->entries;

	if (e == NULL)
		return -ENOENT;

	do {
		if (!strcmp(e->name, (char *)name) && !e->deleted) {
			dir->size -= strlen(e->name);
			e->deleted = 1;
			dir->dirty = 1;
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
	dir->dirty = 0;
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
