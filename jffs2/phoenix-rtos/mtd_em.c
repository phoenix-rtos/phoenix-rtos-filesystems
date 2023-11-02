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

#include <fcntl.h>
#include <sys/mman.h>

#include "mtd.h"

typedef struct _ecc_t {
	char meta[16];
} ecc_t;

jffs2_common_t jffs2_common;

static void *nand_em;
static ecc_t ecc[256];

#define NAND_SIZE (4096 * 256)

int mtd_read(struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen,
			     u_char *buf)
{

	*retlen = 0;
	memset(buf, 0, len);

	if (!len)
		return 0;

	memcpy(buf, nand_em + from, len);
	*retlen = len;
	return 0;
}


int mtd_write(struct mtd_info *mtd, loff_t to, size_t len, size_t *retlen,
	      const u_char *buf)
{
	if (!len)
		return 0;

	memcpy(nand_em + to, buf, len);
	*retlen = len;
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
	printf("mtd_read_oob offs %lu ooblen %lu\n", from, ops->ooblen);
	memset(ops->oobbuf, 0xff, ops->ooblen);
	ops->oobretlen = ops->ooblen;
	return 0;
}


int mtd_write_oob(struct mtd_info *mtd, loff_t to, struct mtd_oob_ops *ops)
{
	printf("mtd_write offs %lu ooblen %lu\n", to, ops->ooblen);
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
	memset(nand_em + instr->addr, 0xffffffff, instr->len);
	instr->state = MTD_ERASE_DONE;
	instr->callback(instr);
	return 0;
}


int mtd_block_markbad(struct mtd_info *mtd, loff_t ofs)
{
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
	return 0;
}


struct dentry *mount_mtd(struct file_system_type *fs_type, int flags, 
		const char *dev_name, void *data,
		int (*fill_super)(struct super_block *, void *, int))
{

	struct mtd_info *mtd;
	int imgfd;
	int ret;
	int offs = 0;

	printf("mmaping device\n");
	nand_em = mmap(NULL, NAND_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);

	if (nand_em == NULL) {
		printf("mtd mount mmap error\n");
		return NULL;
	}

	imgfd = open("/init/jffs2_ram.img", 'r');

	while ((ret = read(imgfd, nand_em + offs, 1024)) > 0) {

		offs += ret;
		if (offs == 1048576)
			break;
	}
	mtd = malloc(sizeof(struct mtd_info));

	mtd->name = "nand emulator";
	mtd->type = MTD_NANDFLASH;
	mtd->erasesize = 16 * 4096;
	mtd->writesize = 4096;
	mtd->flags = MTD_WRITEABLE;
	mtd->size = 256 * 4096;
	mtd->oobsize = 32;
	mtd->oobavail = 16;

	struct super_block *sb = malloc(sizeof(struct super_block));
	sb->s_mtd = mtd;

	printf("fill super\n");
	fill_super(sb, NULL, 0);
	jffs2_common.sb = sb;
	return sb->s_root;
}


void kill_mtd_super(struct super_block *sb)
{
}
