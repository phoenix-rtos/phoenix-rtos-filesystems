/*
 * Phoenix-RTOS
 *
 * Partition table
 *
 * Copyright 2020 Phoenix Systems
 * Author: Hubert Buczynski
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */


#ifndef _LIB_PARTITION_TABLE_H_
#define _LIB_PARTITION_TABLE_H_


/* Structure of partition table:
*  _________________________________________________________________________________
* |        28 B     |                      32 B * n                     |    4 B    |
*  ---------------------------------------------------------------------------------
* | ptable_header_t | ptable_partition_t 0 | ... | ptable_partition_t n | magicBytes|
*  ---------------------------------------------------------------------------------
*
*    NOTE: Data in the partition table should be stored in little endian.
*
*/


#include <stdint.h>


static const uint8_t pt_magicBytes[] = {0xde, 0xad, 0xfc, 0xbe};

/* Based on MBR types */
enum { ptable_raw = 0x51, ptable_meterfs = 0x75 };


typedef struct {
	unsigned char name[8];

	uint32_t offset;
	uint32_t size;

	uint8_t type;
	uint8_t reserved[15];     /* default value should be 0 */
} ptable_partition_t;


typedef struct {
	uint32_t pCnt;            /* number of partitions */
	uint8_t reserved[24];     /* default value should be 0 */

	ptable_partition_t parts[0];
} ptable_header_t;


typedef struct {
	uint32_t memSize;
	uint32_t sectorSize;

	ssize_t (*read)(unsigned int addr, void *buff, size_t bufflen);
	ssize_t (*write)(unsigned int addr, const void *buff, size_t bufflen);
} memory_properties_t;



/* Function returns pointer to an array of partitions and writes to pCnt number of partitions. */
ptable_partition_t *ptable_readPartitions(uint32_t *pCnt, const memory_properties_t *mem);


/* Function wrtie pHeaders to memory */
ssize_t ptable_writePartitions(ptable_partition_t *pHeaders, uint32_t pCnt, const memory_properties_t *mem);


#endif
