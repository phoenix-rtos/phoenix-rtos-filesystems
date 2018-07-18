/*
 * Phoenix-RTOS
 *
 * Meterfs - SST25VF016B SPI flash routines
 *
 * Copyright 2017, 2018 Phoenix Systems
 * Author: Aleksander Kaminski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _METERFS_FLASH_H_
#define _METERFS_FLASH_H_


extern void (*flash_write)(unsigned int, void *, size_t);


extern void flash_chipErase(void);


extern void flash_eraseSector(unsigned int addr);


extern int flash_read(unsigned int addr, void *buff, size_t bufflen);


extern void flash_init(size_t *flashsz, size_t *sectorsz);


#endif
