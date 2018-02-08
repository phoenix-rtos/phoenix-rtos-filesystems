/*
 * Phoenix-RTOS
 *
 * libphoenix
 *
 * stdio.h
 *
 * Copyright 2017 Phoenix Systems
 * Author: Kamil Amanowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */


#ifndef _MBR_H_
#define _MBR_H_ /* mbr.h */

#define PENTRY_LINUX 0x83       /* any native linux partition */
#define PENTRY_PROTECTIVE 0xEE  /* protective mbr mode for gpt partition table */

/* partition entry structure */
struct pentry_t {
	u8      status;
	u8      first_sect[3]; /* chs */
	u8      type;
	u8      last_sect[3];  /* chs */
	u32     first_sect_lba;
	u32     sector_count;
} __attribute__((packed));

#define MBR_SIGNATURE 0xAA55

/* master boot record structure */
struct mbr_t {
	char            bca[446];   /* bootstrap code area */
	struct pentry_t pent[4];    /* partition entries */
	u16             boot_sign;  /* mbr signature */
} __attribute__((packed));

struct mbr_t *read_mbr(void);
#endif /* mbr.h */
