/*
 * Phoenix-RTOS
 *
 * EXT2 filesystem
 *
 * Library
 *
 * Copyright 2019, 2020 Phoenix Systems
 * Author: Kamil Amanowicz, Lukasz Kosinski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <sys/file.h>
#include <sys/stat.h>
#include <sys/threads.h>

#include "ext2.h"
#include "libext2.h"


int libext2_mount(oid_t *dev, unsigned int sectorsz, ssize_t (*read)(id_t, offs_t, char *, size_t), ssize_t (*write)(id_t, offs_t, const char *, size_t), void **data)
{
	ext2_t *fs;
	int err;

	if ((*data = fs = (ext2_t *)malloc(sizeof(ext2_t))) == NULL)
		return -ENOMEM;

	memcpy(&fs->dev, dev, sizeof(oid_t));
	fs->sectorsz = sectorsz;
	fs->read = read;
	fs->write = write;

	if ((err = ext2_sb_init(fs)) < 0) {
		free(fs);
		return err;
	}

	if ((err = ext2_gdt_init(fs)) < 0) {
		ext2_sb_destroy(fs);
		free(fs);
		return err;
	}

	if ((err = ext2_objs_init(fs)) < 0) {
		ext2_gdt_destroy(fs);
		ext2_sb_destroy(fs);
		free(fs);
		return err;
	}

	if ((fs->root = ext2_obj_get(fs, ROOT_INO)) == NULL) {
		ext2_objs_destroy(fs);
		ext2_gdt_destroy(fs);
		ext2_sb_destroy(fs);
		free(fs);
		return -EFAULT;
	}

	return ROOT_INO;
}


int libext2_unmount(void *data)
{
	ext2_t *fs = (ext2_t *)data;

	ext2_objs_destroy(fs);
	ext2_gdt_destroy(fs);
	ext2_sb_destroy(fs);
	free(fs);

	return EOK;
}


int libext2_handler(void *data, msg_t *msg)
{
	ext2_t *fs = (ext2_t *)data;
	ext2_obj_t *obj;
	uint16_t mode;
	oid_t dev;

	switch (msg->type) {
	case mtCreate:
		if (!ext2_lookup(fs, msg->i.create.dir.id, msg->i.data, (uint8_t)msg->i.size, &msg->o.create.oid.id, &dev, &mode)) {
			if ((obj = ext2_obj_get(fs, msg->o.create.oid.id)) == NULL) {
				msg->o.create.err = -EFAULT;
				break;
			}

			mutexLock(obj->lock);

			if ((S_ISCHR(obj->inode->mode) || S_ISBLK(obj->inode->mode)) && (dev.port == fs->dev.port) && (dev.id == msg->o.create.oid.id)) {
				mutexUnlock(obj->lock);
				ext2_obj_put(fs, obj);

				if (ext2_unlink(fs, msg->i.create.dir.id, msg->i.data, (uint8_t)msg->i.size) < 0) {
					msg->o.create.err = -EEXIST;
					break;
				}
			}
			else {
				mutexUnlock(obj->lock);
				ext2_obj_put(fs, obj);

				msg->o.create.err = -EEXIST;
				break;
			}
		}

		mode = (uint16_t)msg->i.create.mode;

		switch (msg->i.create.type) {
		case otDir:
			mode |= S_IFDIR;
			break;
		
		case otFile:
			mode |= S_IFREG;
			break;

		case otDev:
			mode |= (S_IFCHR & 0x1ff);
			break;
		}

		msg->o.create.oid.port = fs->dev.port;
		msg->o.create.err = ext2_create(fs, msg->i.create.dir.id, msg->i.data, (uint8_t)msg->i.size, &msg->i.create.dev, mode, &msg->o.create.oid.id);
		break;

	case mtDestroy:
		msg->o.io.err = ext2_destroy(fs, msg->i.destroy.oid.id);
		break;

	case mtLookup:
		msg->o.lookup.fil.port = fs->dev.port;
		msg->o.lookup.err = ext2_lookup(fs, msg->i.lookup.dir.id, msg->i.data, (uint8_t)msg->i.size, &msg->o.lookup.fil.id, &msg->o.lookup.dev, &mode);
		break;

	case mtOpen:
		msg->o.io.err = ext2_open(fs, msg->i.openclose.oid.id);
		break;

	case mtClose:
		msg->o.io.err = ext2_close(fs, msg->i.openclose.oid.id);
		break;

	case mtRead:
		msg->o.io.err = ext2_read(fs, msg->i.io.oid.id, msg->i.io.offs, msg->o.data, msg->o.size);
		break;

	case mtWrite:
		msg->o.io.err = ext2_write(fs, msg->i.io.oid.id, msg->i.io.offs, msg->i.data, msg->i.size);
		break;

	case mtTruncate:
		msg->o.io.err = ext2_truncate(fs, msg->i.io.oid.id, msg->i.io.len);
		break;

	case mtDevCtl:
		msg->o.io.err = -EINVAL;
		break;

	case mtGetAttr:
		msg->o.io.err = ext2_getattr(fs, msg->i.attr.oid.id, msg->i.attr.type, &msg->o.attr.val);
		break;

	case mtSetAttr:
		msg->o.io.err = ext2_setattr(fs, msg->i.attr.oid.id, msg->i.attr.type, msg->i.attr.val);
		break;

	case mtLink:
		msg->o.io.err = ext2_link(fs, msg->i.ln.dir.id, msg->i.data, (uint8_t)msg->i.size, msg->i.ln.oid.id);
		break;

	case mtUnlink:
		msg->o.io.err = ext2_unlink(fs, msg->i.ln.dir.id, msg->i.data, (uint8_t)msg->i.size);
		break;
	}

	return EOK;
}
