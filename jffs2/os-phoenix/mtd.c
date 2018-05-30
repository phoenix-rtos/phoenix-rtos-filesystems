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

#include "../os-phoenix.h"

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

	memset(mtd->data_buf, 0, mtd->writesize);
	if (from % mtd->writesize) {
		if ((ret = flashdrv_read(mtd->dma, (from / mtd->writesize) + mtd->start, mtd->data_buf, mtd->meta_buf)) && ret != 0xff10)
				return -1;

		*retlen += mtd->writesize - (from % mtd->writesize);
		if (*retlen > len)
			*retlen = len;

		memcpy(buf, mtd->data_buf + (from % mtd->writesize), *retlen);
		len -= *retlen;
		from += *retlen;
	}

	if (!len)
		return 0;

	while (len >= mtd->writesize) {
		if ((ret = flashdrv_read(mtd->dma, (from / mtd->writesize) + mtd->start, mtd->data_buf, mtd->meta_buf)) && ret != 0xff10)
			return -1;

		memcpy(buf + *retlen, mtd->data_buf, mtd->writesize);
		len -= mtd->writesize;
		*retlen += mtd->writesize;
		from += mtd->writesize;
	}

	memset(mtd->data_buf, 0, mtd->writesize);
	if (len > 0) {
		if ((ret = flashdrv_read(mtd->dma, (from / mtd->writesize) + mtd->start, mtd->data_buf, mtd->meta_buf)) && ret != 0xff10)
			return -1;

		memcpy(buf + *retlen, mtd->data_buf, len);
		*retlen += len;
	}
	return 0;
}


int mtd_write(struct mtd_info *mtd, loff_t to, size_t len, size_t *retlen,
		const u_char *buf)
{
	int ret;
	*retlen = 0;

	if (!len)
		return 0;

	while (len) {

		if ((ret = flashdrv_read(mtd->dma, (to / mtd->writesize) + mtd->start, NULL, mtd->meta_buf)) && ret != 0xff10)
			return -1;

		memcpy(mtd->data_buf, buf + *retlen, mtd->writesize);

		if (flashdrv_write(mtd->dma, (to / mtd->writesize) + mtd->start, mtd->data_buf, mtd->meta_buf))
			return -1;

		len -= mtd->writesize;
		*retlen += mtd->writesize;
		to += mtd->writesize;
	}

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

	while (ops->oobretlen < ops->ooblen) {
		if ((ret = flashdrv_read(mtd->dma, (from / mtd->writesize) + mtd->start, NULL, mtd->meta_buf)) && ret != 0xff10)
				return -1;

		memcpy(ops->oobbuf + ops->oobretlen, mtd->meta_buf, ops->ooblen > mtd->oobsize ? mtd->oobsize : ops->ooblen);
		ops->oobretlen += ops->ooblen > mtd->oobsize ? mtd->oobsize : ops->ooblen;
		from += mtd->writesize;
	}

	return 0;
}


int mtd_write_oob(struct mtd_info *mtd, loff_t to, struct mtd_oob_ops *ops)
{
	if (ops->ooblen > mtd->oobsize)
		return -1;

	memset(mtd->meta_buf, 0xff, sizeof(flashdrv_meta_t));
	memcpy(mtd->meta_buf, ops->oobbuf, ops->ooblen);

	if (flashdrv_write(mtd->dma, (to / mtd->writesize) + mtd->start, NULL, mtd->meta_buf))
		return -1;

	ops->oobretlen = ops->ooblen;
	return 0;
}


// not supported in nand
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
	if (instr->len != mtd->erasesize || instr->addr % mtd->erasesize)
		return -1;

	if (flashdrv_erase(mtd->dma, (instr->addr / mtd->writesize) + mtd->start))
		return -1;

	instr->state = MTD_ERASE_DONE;
	instr->callback(instr);

	return 0;
}


int mtd_block_markbad(struct mtd_info *mtd, loff_t ofs)
{
	memset(mtd->data_buf, 0xff, mtd->writesize);
	memset(mtd->meta_buf, 0xff, mtd->writesize);
	memset(mtd->data_buf, 0, 2);

	if (flashdrv_writeraw(mtd->dma, (ofs / mtd->writesize) + mtd->start, mtd->data_buf, mtd->writesize))
		return -1;

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

	if (flashdrv_readraw(mtd->dma, (ofs / mtd->writesize) + mtd->start, mtd->data_buf, mtd->writesize))
		return -1;

	if(!((char *)mtd->data_buf)[0])
		return 1;

	return 0;
}


struct dentry *mount_mtd(struct file_system_type *fs_type, int flags, 
		const char *dev_name, void *data,
		int (*fill_super)(struct super_block *, void *, int))
{
	struct mtd_info *mtd;
	flashdrv_dma_t *dma;

	mtd = malloc(sizeof(struct mtd_info));

	mtd->data_buf = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_UNCACHED, OID_NULL, 0);
	mtd->meta_buf = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_UNCACHED, OID_NULL, 0);

	if (mtd->data_buf == NULL || mtd->meta_buf == NULL) {
		free(mtd);
		printf("jffs2: failed to map read/write buffers\n");
		return NULL;
	}

	flashdrv_init();

	dma = flashdrv_dmanew();

	if (dma == NULL)
		return NULL;

	mtd->name = "micron";
	mtd->dma = dma;
	mtd->type = MTD_NANDFLASH;
	mtd->erasesize = MTD_BLOCK_SIZE;
	mtd->writesize = MTD_PAGE_SIZE;
	mtd->flags = MTD_WRITEABLE;
	mtd->size = MTD_BLOCK_SIZE * jffs2_common.size;
	mtd->oobsize = 16;
	mtd->oobavail = 16;
	mtd->start = jffs2_common.start_block * 64;

	struct super_block *sb = malloc(sizeof(struct super_block));
	sb->s_mtd = mtd;

	fill_super(sb, NULL, 0);
	jffs2_common.sb = sb;

	return sb->s_root;
}


void kill_mtd_super(struct super_block *sb)
{
}
