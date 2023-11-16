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
	char *buff;

	if (!dir->inode->size)
		return EOK;

	if (dir->inode->size > fs->blocksz)
		return -EBUSY;

	if ((buff = (char *)malloc(fs->blocksz)) == NULL)
		return -ENOMEM;

	if ((ret = _ext2_file_read(fs, dir, offs, buff, fs->blocksz)) != fs->blocksz) {
		free(buff);
		return (ret < 0) ? (int)ret : -EINVAL;
	}

	entry = (ext2_dirent_t *)buff;

	if ((entry->len != 1) || strncmp(entry->name, ".", 1)) {
		free(buff);
		return -EINVAL;
	}

	offs += entry->size;
	entry = (ext2_dirent_t *)(buff + offs);

	if ((entry->len != 2) || strncmp(entry->name, "..", 2)) {
		free(buff);
		return -EINVAL;
	}

	offs += entry->size;
	free(buff);

	return (offs == fs->blocksz);
}


static int _ext2_dir_find(ext2_t *fs, ext2_obj_t *dir, const char *name, size_t len, char *buff, uint32_t *offs)
{
	ext2_dirent_t *entry;
	uint32_t boffs;
	ssize_t ret;

	for (boffs = *offs; boffs < dir->inode->size; boffs += fs->blocksz) {
		if ((ret = _ext2_file_read(fs, dir, boffs, buff, fs->blocksz)) != fs->blocksz)
			return (ret < 0) ? (int)ret : -EINVAL;

		for (*offs = 0; *offs < fs->blocksz; *offs += entry->size) {
			entry = (ext2_dirent_t *)(buff + *offs);

			if (!entry->size)
				break;

			if (((size_t)entry->len == len) && !strncmp(entry->name, name, len))
				return boffs;
		}
	}

	return -ENOENT;
}


int _ext2_dir_search(ext2_t *fs, ext2_obj_t *dir, const char *name, size_t len, id_t *res)
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

	ret = _ext2_file_read(fs, dir, offs, (char *)entry, len);

	if (ret < (ssize_t)sizeof(ext2_dirent_t)) {
		free(entry);
		return (ret < 0) ? (int)ret : -ENOENT;
	}

	if (ret < (ssize_t)(sizeof(ext2_dirent_t) + entry->len)) {
		free(entry);
		return -ENOENT;
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
			res->d_type = DT_DIR;
			break;

		case DIRENT_CHRDEV:
			res->d_type = DT_CHR;
			break;

		case DIRENT_BLKDEV:
			res->d_type = DT_BLK;
			break;

		default:
			res->d_type = DT_REG;
			break;
	}

	res->d_ino = entry->ino;
	res->d_reclen = entry->size;
	res->d_namlen = entry->len;
	memcpy(res->d_name, entry->name, entry->len);
	res->d_name[entry->len] = '\0';
	free(entry);

	dir->inode->atime = time(NULL);

	return res->d_reclen;
}


int _ext2_dir_add(ext2_t *fs, ext2_obj_t *dir, const char *name, size_t len, uint16_t mode, uint32_t ino)
{
	uint32_t offs, size = 0;
	ext2_dirent_t *entry;
	char *buff;
	ssize_t ret;

	if (len > MAX_NAMELEN) {
		return -ENAMETOOLONG;
	}

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

				size = (len + sizeof(ext2_dirent_t) + 3) & ~3;
				if (size >= fs->blocksz - offs) {
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
	else if (S_ISFIFO(mode))
		entry->type = DIRENT_FIFO;
	else if (S_ISSOCK(mode))
		entry->type = DIRENT_SOCK;
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


int _ext2_dir_remove(ext2_t *fs, ext2_obj_t *dir, const char *name, size_t len)
{
	ext2_dirent_t *entry, *tmp;
	uint32_t prev, boffs, offs = 0;
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
	boffs = err;

	/* Entry in the middle of the block => expand previous entry size */
	if (offs) {
		for (prev = 0, tmp = (ext2_dirent_t *)buff; prev + tmp->size < offs;) {
			prev += tmp->size;
			tmp = (ext2_dirent_t *)(buff + prev);
		}
		tmp->size += entry->size;

		if ((ret = _ext2_file_write(fs, dir, boffs, buff, fs->blocksz)) != fs->blocksz)
			err = (ret < 0) ? (int)ret : -EINVAL;
		else
			err = EOK;
	}
	/* Entry takes entire block */
	else if (entry->size == fs->blocksz) {
		/* Last block => truncate */
		if (boffs + fs->blocksz >= dir->inode->size) {
			err = _ext2_file_truncate(fs, dir, dir->inode->size - fs->blocksz);
		}
		/* Middle block => copy last block and truncate */
		else {
			do {
				if ((err = ext2_block_init(fs, dir, dir->inode->size / fs->blocksz - 1, buff)) < 0)
					break;

				if ((err = ext2_block_syncone(fs, dir, boffs / fs->blocksz, buff)) < 0)
					break;

				err = _ext2_file_truncate(fs, dir, dir->inode->size - fs->blocksz);
			} while (0);
		}
	}
	/* Entry at the start of the block => move next entry to the start of the block */
	else {
		tmp = (ext2_dirent_t *)((char *)buff + entry->size);
		entry->ino = tmp->ino;
		entry->size += tmp->size;
		entry->type = tmp->type;
		entry->len = tmp->len;
		memcpy(entry->name, tmp->name, tmp->len);

		if ((ret = _ext2_file_write(fs, dir, boffs, buff, fs->blocksz)) != fs->blocksz)
			err = (ret < 0) ? (int)ret : -EINVAL;
		else
			err = EOK;
	}

	free(buff);

	return err;
}
