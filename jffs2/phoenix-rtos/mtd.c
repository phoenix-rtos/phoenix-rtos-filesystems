/*
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * Jffs2 FileSystem - system specific information.
 *
 * Copyright 2018, 2022 Phoenix Systems
 * Author: Kamil Amanowicz, Hubert Buczynski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#include "../phoenix-rtos.h"


struct dentry *mount_mtd(struct file_system_type *fs_type, int flags,
		const char *dev_name, void *data,
		int (*fill_super)(struct super_block *, void *, int))
{
	struct mtd_info *mtd;
	struct super_block *sb;
	jffs2_partition_t *p = (jffs2_partition_t *)data;

	if (p->strg->dev->mtd == NULL)
		return NULL;

	mtd = malloc(sizeof(struct mtd_info));
	if (mtd == NULL)
		return NULL;

	sb = malloc(sizeof(struct super_block));
	if (sb == NULL) {
		free(mtd);
		return NULL;
	}

	/* Initialize MTD */
	if (p->strg->dev->mtd->type == mtd_norFlash) {
		mtd->type = MTD_NORFLASH;
	}
	else if (p->strg->dev->mtd->type == mtd_nandFlash) {
		mtd->type = MTD_NANDFLASH;
	}
	else {
		return NULL;
	}

	mtd->name = p->strg->dev->mtd->name;
	mtd->erasesize = p->strg->dev->mtd->erasesz;
	mtd->writesize = p->strg->dev->mtd->writesz;
	mtd->flags = MTD_WRITEABLE;
	mtd->size = p->strg->size;

	mtd->index = 0;
	mtd->oobsize = p->strg->dev->mtd->oobSize;
	mtd->oobavail = p->strg->dev->mtd->oobAvail;
	mtd->storage = p->strg;

	/* Inialize superblock */
	sb->s_mtd = mtd;
	sb->s_part = p;
	sb->s_flags = p->flags;
	p->sb = sb;

	if (fill_super(sb, NULL, 0) < 0) {
		free(mtd);
		free(sb);
		return NULL;
	}

	return sb->s_root;
}


void kill_mtd_super(struct super_block *sb)
{
	struct jffs2_sb_info *c;

	if (sb != NULL) {
		c = JFFS2_SB_INFO(sb);

		/* Destroy subperblock info and sync fs */
		sb->s_op->put_super(sb);

		/* Destroy superblock locks */
		spin_lock_destroy(&c->inocache_lock);
		spin_lock_destroy(&c->erase_completion_lock);
		destroy_waitqueue_head(&c->inocache_wq);
		destroy_waitqueue_head(&c->erase_wait);
		mutex_destroy(&c->erase_free_sem);
		mutex_destroy(&c->alloc_sem);

		free(sb->s_mtd);
		free(sb);
	}
}
