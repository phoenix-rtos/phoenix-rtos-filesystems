/*
 * Phoenix-RTOS
 *
 * EXT2 filesystem
 *
 * Directory operations
 *
 * Copyright 2017, 2020 Phoenix Systems
 * Author: Kamil Amanowicz, Lukasz Kosinski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sys/stat.h>

#include "block.h"
#include "dir.h"
#include "file.h"


int _ext2_dir_empty(ext2_t *fs, ext2_obj_t *dir)
{
	ext2_dirent_t *entry;
	uint32_t offs = 0;
	ssize_t ret;

	if (!dir->inode->size)
		return EOK;

	if (dir->inode->size > fs->blocksz)
		return -EBUSY;

	if ((entry = (ext2_dirent_t *)malloc(fs->blocksz)) == NULL)
		return -ENOMEM;

	if ((ret = _ext2_file_read(fs, dir, offs, (char *)entry, fs->blocksz)) != fs->blocksz) {
		free(entry);
		return (ret < 0) ? (int)ret : -EINVAL;
	}

	if ((entry->len != 1) || strncmp(entry->name, ".", 1)) {
		free(entry);
		return -EINVAL;
	}

	offs += entry->size;
	entry = (ext2_dirent_t *)((char *)entry + offs);

	if ((entry->len != 2) || strncmp(entry->name, "..", 2)) {
		free(entry);
		return -EINVAL;
	}

	offs += entry->size;
	free(entry);

	return (offs == fs->blocksz);
}


static int _ext2_dir_find(ext2_t *fs, ext2_obj_t *dir, const char *name, uint8_t len, char *buff, uint32_t *offs)
{
	ext2_dirent_t *entry;
	uint32_t i;
	ssize_t ret;

	for (i = *offs; i < dir->inode->size; i += fs->blocksz) {
		if ((ret = _ext2_file_read(fs, dir, i, buff, fs->blocksz)) != fs->blocksz)
			return (ret < 0) ? (int)ret : -EINVAL;

		for (*offs = 0; *offs < fs->blocksz; *offs += entry->size) {
			entry = (ext2_dirent_t *)(buff + *offs);

			if (!entry->size)
				break;

			if ((entry->len == len) && !strncmp(entry->name, name, len))
				return EOK;
		}
	}

	return -ENOENT;
}


int _ext2_dir_search(ext2_t *fs, ext2_obj_t *dir, const char *name, uint8_t len, id_t *res)
{
	uint32_t offs = 0;
	char *buff;
	int err;

	if ((buff = (char *)malloc(fs->blocksz)) == NULL)
		return -ENOMEM;

	do {
		if ((err = _ext2_dir_find(fs, dir, name, len, buff, &offs)) < 0)
			break;

		*res = ((ext2_dirent_t *)(buff + offs))->ino;
	} while (0);

	free(buff);

	return err;
}


int _ext2_dir_read(ext2_t *fs, ext2_obj_t *dir, offs_t offs, struct dirent *res, size_t len)
{
	ext2_dirent_t *entry;
	ssize_t ret;

	if (!dir->inode->size || !dir->inode->links)
		return -ENOENT;

	if (len < sizeof(ext2_dirent_t))
		return -EINVAL;

	if ((entry = (ext2_dirent_t *)malloc(len)) == NULL)
		return -ENOMEM;

	if ((ret = _ext2_file_read(fs, dir, offs, (char *)entry, len)) != len) {
		free(entry);
		return (ret < 0) ? (int)ret : -ENOENT;
	}

	if (!entry->len) {
		free(entry);
		return -ENOENT;
	}

	if (len <= entry->len + sizeof(struct dirent)) {
		free(entry);
		return -EINVAL;
	}

	switch (entry->type) {
	case DIRENT_DIR:
		res->d_type = dtDir;
		break;

	case DIRENT_CHRDEV:
	case DIRENT_BLKDEV:
		res->d_type = dtDev;
		break;

	default:
		res->d_type = dtFile;
	}

	res->d_ino = entry->ino;
	res->d_reclen = entry->size;
	res->d_namlen = entry->len;
	memcpy(res->d_name, entry->name, entry->len);
	res->d_name[entry->len] = '\0';
	free(entry);

	if ((dir->flags & OFLAG_MOUNTPOINT) && !strncmp(res->d_name, "..", 2))
		res->d_ino = (ino_t)dir->mnt.id;

	dir->inode->atime = time(NULL);

	return res->d_reclen;
}


int _ext2_dir_add(ext2_t *fs, ext2_obj_t *dir, const char *name, uint8_t len, uint16_t mode, uint32_t ino)
{
	uint32_t offs, size = 0;
	ext2_dirent_t *entry;
	char *buff;
	ssize_t ret;

	if ((buff = (char *)malloc(fs->blocksz)) == NULL)
		return -ENOMEM;

	if (!dir->inode->size) {
		offs = fs->blocksz;
	}
	else {
		if ((ret = _ext2_file_read(fs, dir, dir->inode->size - fs->blocksz, buff, fs->blocksz)) != fs->blocksz) {
			free(buff);
			return (ret < 0) ? (int)ret : -EINVAL;
		}

		for (offs = 0; offs < fs->blocksz; offs += entry->size) {
			entry = (ext2_dirent_t *)(buff + offs);

			if (!entry->size)
				break;

			if (offs + entry->size == fs->blocksz) {
				entry->size = (entry->len) ? (entry->len + sizeof(ext2_dirent_t) + 3) & ~3 : 0;
				offs += entry->size;

				if ((size = (len + sizeof(ext2_dirent_t) + 3) & ~3) >= fs->blocksz - offs) {
					entry->size += fs->blocksz - offs;
					offs = fs->blocksz;
					break;
				}

				size = fs->blocksz - offs;
				break;
			}
		}
	}

	/* No space in this block => alloc new one */
	if (offs >= fs->blocksz) {
		dir->inode->size += fs->blocksz;
		memset(buff, 0, fs->blocksz);
		size = fs->blocksz;
		offs = 0;
	}

	entry = (ext2_dirent_t *)(buff + offs);
	entry->ino = ino;
	entry->size = size;
	entry->len = len;
	memcpy(entry->name, name, len);

	if (S_ISDIR(mode))
		entry->type = DIRENT_DIR;
	else if (S_ISCHR(mode))
		entry->type = DIRENT_CHRDEV;
	else if (S_ISBLK(mode))
		entry->type = DIRENT_BLKDEV;
	else if (S_ISREG(mode))
		entry->type = DIRENT_FILE;
	else
		entry->type = DIRENT_UNKNOWN;

	offs = (dir->inode->size > fs->blocksz) ? dir->inode->size - fs->blocksz : 0;

	if ((ret = _ext2_file_write(fs, dir, offs, buff, fs->blocksz)) != fs->blocksz) {
		free(buff);
		return (ret < 0) ? (int)ret : -EINVAL;
	}

	free(buff);

	return EOK;
}


int _ext2_dir_remove(ext2_t *fs, ext2_obj_t *dir, const char *name, uint8_t len)
{
	ext2_dirent_t *entry, *tmp;
	uint32_t prev, offs = 0;
	ssize_t ret;
	char *buff;
	int err;

	if ((buff = (char *)malloc(fs->blocksz)) == NULL)
		return -ENOMEM;

	if ((err = _ext2_dir_find(fs, dir, name, len, buff, &offs)) < 0) {
		free(buff);
		return err;
	}
	entry = (ext2_dirent_t *)(buff + offs);

	/* Entry in the middle of the block => expand previous entry size */
	if (offs) {
		for (prev = 0, tmp = (ext2_dirent_t *)buff; prev + tmp->size < offs;) {
			prev += tmp->size;
			tmp = (ext2_dirent_t *)(buff + prev);
		}
		tmp->size += entry->size;

		if ((ret = _ext2_file_write(fs, dir, offs, buff, fs->blocksz)) != fs->blocksz)
			err = (ret < 0) ? (int)ret : -EINVAL;
		else
			err = EOK;
	}
	/* Last entry at the start of the block => truncate */
	else if (entry->size == fs->blocksz) {
		if (offs + fs->blocksz >= dir->inode->size) {
			err =_ext2_file_truncate(fs, dir, dir->inode->size - fs->blocksz);
		}
		else {
			do {
				if ((err = ext2_block_init(fs, dir, dir->inode->size / fs->blocksz, buff)) < 0)
					break;

				if ((err = ext2_block_syncone(fs, dir, (offs & ~(fs->blocksz - 1)) / fs->blocksz, buff)) < 0)
					break;

				err = _ext2_file_truncate(fs, dir, dir->inode->size - fs->blocksz);
			} while (0);
		}
	/* Entry at the start of the block => move next entry to the start of the block */
	} else {
		tmp = (ext2_dirent_t *)((char *)buff + entry->size);
		entry->ino = tmp->ino;
		entry->size += tmp->size;
		entry->type = tmp->type;
		entry->len = tmp->len;
		memcpy(entry->name, tmp->name, tmp->len);

		if ((ret = _ext2_file_write(fs, dir, offs, buff, fs->blocksz)) != fs->blocksz)
			err = (ret < 0) ? (int)ret : -EINVAL;
		else
			err = EOK;
	}

	free(buff);

	return err;
}
