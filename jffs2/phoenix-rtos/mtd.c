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

#include "../phoenix-rtos.h"

#include "mtd.h"


#define MTD_PAGE_SIZE 4096
#define MTD_BLOCK_SIZE (64 * MTD_PAGE_SIZE)


int mtd_read(struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen,
			     u_char *buf)
{
	int ret;
	*retlen = 0;

	if (!len)
		return 0;

	mutexLock(mtd->lock);
	if (from % mtd->writesize) {
		ret = flashdrv_read(mtd->dma, (from / mtd->writesize) + mtd->start, mtd->data_buf, mtd->meta_buf);
		if (((ret >> 8) & 0xff) == 0xfe || (ret & 0xff) == 0x4) {
				printf("jffs2: Flash read error 0x%x\n", ret);
				mutexUnlock(mtd->lock);
				return -1;
		}

		*retlen += mtd->writesize - (from % mtd->writesize);
		if (*retlen > len)
			*retlen = len;

		memcpy(buf, mtd->data_buf + (from % mtd->writesize), *retlen);
		len -= *retlen;
		from += *retlen;
	}

	if (!len) {
		mutexUnlock(mtd->lock);
		return 0;
	}

	while (len >= mtd->writesize) {
		ret = flashdrv_read(mtd->dma, (from / mtd->writesize) + mtd->start, mtd->data_buf, mtd->meta_buf);
		if (((ret >> 8) & 0xff) == 0xfe || (ret & 0xff) == 0x4) {
			printf("jffs2: Flash read error 0x%x\n", ret);
			mutexUnlock(mtd->lock);
			return -1;
		}

		memcpy(buf + *retlen, mtd->data_buf, mtd->writesize);
		len -= mtd->writesize;
		*retlen += mtd->writesize;
		from += mtd->writesize;
	}

	if (len > 0) {
		ret = flashdrv_read(mtd->dma, (from / mtd->writesize) + mtd->start, mtd->data_buf, mtd->meta_buf);
		if (((ret >> 8) & 0xff) == 0xfe || (ret & 0xff) == 0x4) {
			printf("jffs2: Flash read error 0x%x\n", ret);
			mutexUnlock(mtd->lock);
			return -1;
		}

		memcpy(buf + *retlen, mtd->data_buf, len);
		*retlen += len;
	}
	mutexUnlock(mtd->lock);
	return 0;
}


int mtd_write(struct mtd_info *mtd, loff_t to, size_t len, size_t *retlen,
		const u_char *buf)
{
	int ret;
	*retlen = 0;

	if (!len)
		return 0;

	mutexLock(mtd->lock);
	while (len) {
		ret = flashdrv_read(mtd->dma, (to / mtd->writesize) + mtd->start, NULL, mtd->meta_buf);
		if (((ret >> 8) & 0xff) == 0xfe || (ret & 0xff) == 0x4) {
			printf("jffs2: Oob flash read error 0x%x\n", ret);
			mutexUnlock(mtd->lock);
			return -1;
		}

		memcpy(mtd->data_buf, buf + *retlen, mtd->writesize);

		if ((ret = flashdrv_write(mtd->dma, (to / mtd->writesize) + mtd->start, mtd->data_buf, mtd->meta_buf))) {
			mutexUnlock(mtd->lock);
			printf("jffs2: Flash write error 0x%x\n", ret);
			return -1;
		}

		len -= mtd->writesize;
		*retlen += mtd->writesize;
		to += mtd->writesize;
	}

	mutexUnlock(mtd->lock);
	return 0;
}


int mtd_writev(struct mtd_info *mtd, const struct kvec *vecs,
		unsigned long count, loff_t to, size_t *retlen)
{
	unsigned long i;
	size_t writelen = 0;
	int ret = 0;

	*retlen = 0;

	for (i = 0; i < count; i++) {
		ret = mtd_write(mtd, to, vecs[i].iov_len, &writelen, vecs[i].iov_base);
		*retlen = writelen;

		if (ret || writelen != vecs[i].iov_len)
			return ret;
		to += writelen;
	}

	return ret;
}


int mtd_read_oob(struct mtd_info *mtd, loff_t from, struct mtd_oob_ops *ops)
{

	int ret = 0;

	mutexLock(mtd->lock);
	while (ops->oobretlen < ops->ooblen) {
		ret = flashdrv_read(mtd->dma, (from / mtd->writesize) + mtd->start, NULL, mtd->meta_buf);
		if (((ret >> 8) & 0xff) == 0xfe || (ret & 0xff) == 0x4) {
			printf("jffs2: Oob flash readerror 0x%x\n", ret);
			return -1;
		}

		memcpy(ops->oobbuf + ops->oobretlen, mtd->meta_buf, ops->ooblen > mtd->oobsize ? mtd->oobsize : ops->ooblen);
		ops->oobretlen += ops->ooblen > mtd->oobsize ? mtd->oobsize : ops->ooblen;
		from += mtd->writesize;
	}

	mutexUnlock(mtd->lock);
	return 0;
}


int mtd_write_oob(struct mtd_info *mtd, loff_t to, struct mtd_oob_ops *ops)
{

	int ret;

	if (ops->ooblen > mtd->oobsize) {
		printf("jffs2: Oob flash write len larger than oob size %d > %d\n", ops->ooblen, mtd->oobsize);
		return -1;
	}

	mutexLock(mtd->lock);

	memset(mtd->meta_buf, 0xff, sizeof(flashdrv_meta_t));
	memcpy(mtd->meta_buf, ops->oobbuf, ops->ooblen);

	if ((ret = flashdrv_write(mtd->dma, (to / mtd->writesize) + mtd->start, NULL, mtd->meta_buf))) {
		printf("jffs2: Oob flash write error 0x%x\n", ret);
		return -1;
	}
	mutexUnlock(mtd->lock);

	ops->oobretlen = ops->ooblen;
	return 0;
}


/* not supported in nand */
int mtd_point(struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen,
		void **virt, resource_size_t *phys)
{
	return -EOPNOTSUPP;
}


int mtd_unpoint(struct mtd_info *mtd, loff_t from, size_t len)
{
	return -EOPNOTSUPP;
}


int mtd_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	int ret;

	if (instr->len != mtd->erasesize || instr->addr % mtd->erasesize) {
		printf("jffs2: Invalid flash erase parametrs\n");
		return -1;
	}

	mutexLock(mtd->lock);
	if ((ret = flashdrv_erase(mtd->dma, (u32)(instr->addr / mtd->writesize) + mtd->start))) {
		printf("jffs2: Flash erase error 0x%d\n", ret);
		return -1;
	}

	mutexUnlock(mtd->lock);

	instr->state = MTD_ERASE_DONE;
	instr->callback(instr);

	return 0;
}


int mtd_block_markbad(struct mtd_info *mtd, loff_t ofs)
{
	mutexLock(mtd->lock);

	memset(mtd->data_buf, 0xff, mtd->writesize);
	memset(mtd->data_buf, 0, 2);

	if (flashdrv_writeraw(mtd->dma, (ofs / mtd->writesize) + mtd->start, mtd->data_buf, mtd->writesize))
		return -1;
	mutexUnlock(mtd->lock);

	return 0;
}


void *mtd_kmalloc_up_to(const struct mtd_info *mtd, size_t *size)
{
	void *ret;

	if (*size < mtd->writesize)
		*size = mtd->writesize;

	while (*size > mtd->writesize) {
		ret = malloc(*size);

		if (ret != NULL)
			return ret;

		*size >>= 1;
		*size += mtd->writesize - 1;
		*size &= mtd->writesize - 1;
	}

	return malloc((size_t)size);
}


int mtd_block_isbad(struct mtd_info *mtd, loff_t ofs)
{

	int ret = 0;

	mutexLock(mtd->lock);

	if (flashdrv_readraw(mtd->dma, (ofs / mtd->writesize) + mtd->start, mtd->data_buf, mtd->writesize)) {
		mutexUnlock(mtd->lock);
		return -1;
	}

	if(!((char *)mtd->data_buf)[0])
		ret = 1;

	mutexUnlock(mtd->lock);

	return ret;
}


struct dentry *mount_mtd(struct file_system_type *fs_type, int flags,
		const char *dev_name, void *data,
		int (*fill_super)(struct super_block *, void *, int))
{
	struct mtd_info *mtd;
	flashdrv_dma_t *dma;
	jffs2_partition_t *p = (jffs2_partition_t *)data;

	mtd = malloc(sizeof(struct mtd_info));

	mtd->data_buf = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_UNCACHED, OID_NULL, 0);
	mtd->meta_buf = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_UNCACHED, OID_NULL, 0);

	if (mtd->data_buf == NULL || mtd->meta_buf == NULL) {
		free(mtd);
		printf("jffs2: Failed to map flash read/write buffers\n");
		return NULL;
	}

	dma = flashdrv_dmanew();

	if (dma == NULL)
		return NULL;

	mtd->name = "micron";
	mtd->dma = dma;
	mtd->type = MTD_NANDFLASH;
	mtd->erasesize = MTD_BLOCK_SIZE;
	mtd->writesize = MTD_PAGE_SIZE;
	mtd->flags = MTD_WRITEABLE;
	mtd->size = MTD_BLOCK_SIZE * p->size;
	mtd->oobsize = 16;
	mtd->oobavail = 16;
	mtd->start = p->start * 64;

	mutexCreate(&mtd->lock);

	struct super_block *sb = malloc(sizeof(struct super_block));
	sb->s_mtd = mtd;
	sb->s_part = p;
	sb->s_flags = p->flags;
	p->sb = sb;

	fill_super(sb, NULL, 0);

	return sb->s_root;
}


void kill_mtd_super(struct super_block *sb)
{
}
