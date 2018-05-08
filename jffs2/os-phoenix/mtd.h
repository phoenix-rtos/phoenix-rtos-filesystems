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


#ifndef _OS_PHOENIX_MTD_H_
#define _OS_PHOENIX_MTD_H_

#include "../../../phoenix-rtos-devices/storage/imx6ull-flash/flashdrv.h"

#define MTD_ERASE_PENDING		0x01
#define MTD_ERASING				0x02
#define MTD_ERASE_SUSPEND		0x04
#define MTD_ERASE_DONE			0x08
#define MTD_ERASE_FAILED		0x10

#define MTD_FAIL_ADDR_UNKNOWN -1LL

#define MTD_ABSENT				0
#define MTD_RAM					1
#define MTD_ROM					2
#define MTD_NORFLASH			3
#define MTD_NANDFLASH			4		/* SLC NAND */
#define MTD_DATAFLASH			6
#define MTD_UBIVOLUME			7
#define MTD_MLCNANDFLASH		8		/* MLC NAND (including TLC) */

#define MTD_WRITEABLE			0x400	/* Device is writeable */
#define MTD_BIT_WRITEABLE		0x800	/* Single bits can be flipped */
#define MTD_NO_ERASE			0x1000	/* No erase necessary */
#define MTD_POWERUP_LOCK		0x2000	/* Always locked after reset */

enum {
	MTD_OPS_PLACE_OOB = 0,
	MTD_OPS_AUTO_OOB = 1,
	MTD_OPS_RAW = 2,
};


struct mtd_oob_ops {
	unsigned int	mode;
	size_t			len;
	size_t			retlen;
	size_t			ooblen;
	size_t			oobretlen;
	uint32_t		ooboffs;
	uint8_t			*datbuf;
	uint8_t			*oobbuf;
};

struct erase_info {
	struct mtd_info *mtd;
	uint64_t addr;
	uint64_t len;
	uint64_t fail_addr;
	u_long time;
	u_long retries;
	unsigned dev;
	unsigned cell;
	void (*callback) (struct erase_info *self);
	u_long priv;
	u_char state;
	struct erase_info *next;
};

//struct _flashdrv_dma_t;
//typedef struct _flashdrv_dma_t flashdrv_dma_t;

struct mtd_info {
	int todo;
	u_char type;
	const char *name;
	uint32_t flags;
	int index;
	uint32_t erasesize;
	uint32_t writesize;
	uint64_t size;
	uint32_t oobsize;   // Amount of OOB data per block (e.g. 16)
	uint32_t oobavail;  // Available OOB bytes per block
	flashdrv_dma_t *dma;
};

int mtd_read(struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen,
			     u_char *buf);

int mtd_write(struct mtd_info *mtd, loff_t to, size_t len, size_t *retlen,
	      const u_char *buf);

int mtd_writev(struct mtd_info *mtd, const struct kvec *vecs,
	       unsigned long count, loff_t to, size_t *retlen);

int mtd_read_oob(struct mtd_info *mtd, loff_t from, struct mtd_oob_ops *ops);

int mtd_write_oob(struct mtd_info *mtd, loff_t to, struct mtd_oob_ops *ops);

int mtd_point(struct mtd_info *mtd, loff_t from, size_t len, size_t *retlen,
			      void **virt, resource_size_t *phys);

int mtd_block_markbad(struct mtd_info *mtd, loff_t ofs);

int mtd_erase(struct mtd_info *mtd, struct erase_info *instr);

int mtd_unpoint(struct mtd_info *mtd, loff_t from, size_t len);

void *mtd_kmalloc_up_to(const struct mtd_info *mtd, size_t *size);

static inline int mtd_is_bitflip(int err) {
	return err == -EUCLEAN;
}

int mtd_block_isbad(struct mtd_info *mtd, loff_t ofs);


extern struct dentry *mount_mtd(struct file_system_type *fs_type, int flags,
		      const char *dev_name, void *data,
		      int (*fill_super)(struct super_block *, void *, int));

extern void kill_mtd_super(struct super_block *sb);

static inline void mtd_sync(struct mtd_info *mtd)
{
//	if (mtd->_sync)
//		mtd->_sync(mtd);
}


#endif /* _OS_PHOENIX_MTD_H_ */

