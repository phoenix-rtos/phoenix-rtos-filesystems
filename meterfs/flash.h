/*
 * Phoenix-RTOS
 *
 * Operating system kernel
 *
 * Meterfs - SST25VF016B SPI flash routines
 *
 * Copyright 2017 Phoenix Systems
 * Author: Aleksander Kaminski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _METERFS_FLASH_H_
#define _METERFS_FLASH_H_


extern int flash_eraseSector(unsigned int sector);


extern int flash_read(unsigned int addr, void *buff, size_t bufflen);


extern int flash_write(unsigned int addr, void *buff, size_t bufflen);


extern int flash_regionIsBlank(unsigned int addr, size_t len);


extern void flash_init(size_t flashsz, size_t sectorsz);


#endif
