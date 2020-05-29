/*
 * Phoenix-RTOS
 *
 * Meterfs - STM32L1x SPI routines
 *
 * Copyright 2017, 2018 Phoenix Systems
 * Author: Aleksander Kaminski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */

#ifndef _METERFS_SPI_H_
#define _METERFS_SPI_H_

#include <stm32-multi.h>



enum { cmd_wrsr = 0x01, cmd_write, cmd_read, cmd_wrdi, cmd_rdsr, cmd_wren, cmd_hsread = 0x0b, cmd_sector_erase = 0x20,
	cmd_ewsr = 0x50, cmd_32erase = 0x52, cmd_chip_erase = 0x60, cmd_ebsy = 0x70, cmd_dbsy = 0x80, cmd_rdid = 0x90,
	cmd_jedecid = 0x9f, cmd_aai_write = 0xad, cmd_64erase = 0xd8 };


void spi_read(unsigned char cmd, unsigned int addr, unsigned char flags, void *buff, size_t bufflen);


void spi_write(unsigned char cmd, unsigned int addr, unsigned char flags, const void *buff, size_t bufflen);


void spi_powerCtrl(int state);


void spi_csControl(int state);


void spi_init(void);


#endif
