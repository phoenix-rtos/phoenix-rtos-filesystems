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

jffs2_common_t jffs2_common;

int mtd_read(struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen,
			     u_char *buf)
{

	int err;
	*retlen = 0;

	if (!len)
		return 0;

	if (from % mtd->writesize) {
		if ((err = flashdrv_read(mtd->dma, from / mtd->writesize, mtd->data_buf, mtd->meta_buf))) {
			if (err != 0xff10)
				printf("flash read err code 0x%x\n", err);
		}


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
		if ((err = flashdrv_read(mtd->dma, from / mtd->writesize, mtd->data_buf, mtd->meta_buf))) {
			if (err != 0xff10)
				return -1;
		}

		memcpy(buf + *retlen, mtd->data_buf, mtd->writesize);
		len -= mtd->writesize;
		*retlen += mtd->writesize;
		from += mtd->writesize;
	}

	if (len > 0) {
		if ((err = flashdrv_read(mtd->dma, from / mtd->writesize, mtd->data_buf, mtd->meta_buf))) {
			if (err != 0xff10)
				return -1;
		}
		memcpy(buf + *retlen, mtd->data_buf, len);
		*retlen += len;
	}
	return 0;
}


int mtd_write(struct mtd_info *mtd, loff_t to, size_t len, size_t *retlen,
	      const u_char *buf)
{
	*retlen = 0;

	if (!len)
		return 0;

	while (len) {
		memset(mtd->meta_buf, 0xff, sizeof(flashdrv_meta_t));
		memcpy(mtd->data_buf, buf + *retlen, mtd->writesize);
		if (flashdrv_write(mtd->dma, to / mtd->writesize, mtd->data_buf, mtd->meta_buf))
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
		if ((ret = flashdrv_read(mtd->dma, from / mtd->writesize, mtd->data_buf, mtd->meta_buf))) {
			if (ret != 0xff10)
				printf("some error 0x%x\n", ret);;
		}

		memcpy(ops->oobbuf + ops->oobretlen, mtd->meta_buf, ops->ooblen > mtd->oobsize ? mtd->oobsize : ops->ooblen);
		ops->oobretlen += ops->ooblen > mtd->oobsize ? mtd->oobsize : ops->ooblen;
		from += mtd->writesize;
	}

	return 0;
}


int mtd_write_oob(struct mtd_info *mtd, loff_t to, struct mtd_oob_ops *ops)
{
	if (ops->ooblen > mtd->oobsize)
		printf("OOB TOO LONG\n");

	memset(mtd->data_buf, 0xff, mtd->writesize);
	memset(mtd->meta_buf, 0xff, sizeof(flashdrv_meta_t));
	memcpy(mtd->meta_buf, ops->oobbuf, ops->ooblen);

	if (flashdrv_write(mtd->dma, to / mtd->writesize, mtd->data_buf, mtd->meta_buf))
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

	if (flashdrv_erase(mtd->dma, instr->addr / mtd->writesize))
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

	if (flashdrv_writeraw(mtd->dma, ofs / mtd->writesize, mtd->data_buf, mtd->writesize + 224))
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

	if (flashdrv_readraw(mtd->dma, ofs / mtd->writesize, mtd->data_buf, mtd->writesize + 224))
		return -1;

	if(!((char *)mtd->data_buf)[0]) {
	//	printf("block is bad\n");
		return 1;
	}

	return 0;
}


struct dentry *mount_mtd(struct file_system_type *fs_type, int flags, 
		const char *dev_name, void *data,
		int (*fill_super)(struct super_block *, void *, int))
{
	struct mtd_info *mtd;
	flashdrv_dma_t *dma;

	flashdrv_init();

	dma = flashdrv_dmanew();

	if (dma == NULL)
		return NULL;

	mtd = malloc(sizeof(struct mtd_info));

	mtd->name = "micron";
	mtd->dma = dma;
	mtd->type = MTD_NANDFLASH;
	mtd->erasesize = 64 * 4096;
	mtd->writesize = 4096;
	mtd->flags = MTD_WRITEABLE;
	mtd->size = 64 * 4096 * 5;
	mtd->oobsize = 16;
	mtd->oobavail = 16;

	mtd->data_buf = mmap(NULL, 2 * PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_UNCACHED, OID_PHYSMEM, 0x900000);

	if (mtd->data_buf == NULL)
		return NULL;

	mtd->meta_buf = mtd->data_buf + PAGE_SIZE;

	struct super_block *sb = malloc(sizeof(struct super_block));
	sb->s_mtd = mtd;

	fill_super(sb, NULL, 0);
	jffs2_common.sb = sb;

	return sb->s_root;
}


void kill_mtd_super(struct super_block *sb)
{
}
