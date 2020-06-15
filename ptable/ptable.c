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

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

#include "ptable.h"


static int ptable_checkPartitionType(uint8_t type)
{
	int res = 0;

	switch (type) {
		case ptable_raw:
		case ptable_meterfs:
			break;

		default:
			res = -EINVAL;
			break;
	}

	return res;
}


static int ptable_verifyPartition(int id, const ptable_partition_t *pHeaders, const memory_properties_t *mem)
{
	int i;

	/* Verify size and offset */
	if (pHeaders[id].size % mem->sectorSize)
		return -EINVAL;

	if (pHeaders[id].offset % mem->sectorSize)
		return -EINVAL;

	if ((pHeaders[id].size + pHeaders[id].offset) > mem->memSize)
		return -EINVAL;

	/* Check partitions overlapping according to order in partition table */
	for (i = 0; i < id; ++i) {
		if (pHeaders[id].offset == pHeaders[i].offset)
			return -EINVAL;

		if ((pHeaders[id].offset > pHeaders[i].offset) && (pHeaders[id].offset < (pHeaders[i].offset + pHeaders[i].size)))
			return -EINVAL;

		if (((pHeaders[id].offset + pHeaders[id].size) > pHeaders[i].offset) && ((pHeaders[id].offset + pHeaders[id].size) < (pHeaders[i].offset + pHeaders[i].size)))
			return -EINVAL;

		if ((pHeaders[id].offset <= pHeaders[i].offset) && ((pHeaders[id].offset + pHeaders[id].size) >= (pHeaders[i].offset + pHeaders[i].size)))
			return -EINVAL;
	}

	/* Verify partition type */
	if (ptable_checkPartitionType(pHeaders[id].type) < 0)
		return -EINVAL;

	/* Check partition name */
	for (i = 0; i < sizeof(pHeaders[id].name); ++i) {
		if (!isalnum(pHeaders[id].name[i])) {
			if (pHeaders[id].name[i] == '\0')
				break;

			return -EINVAL;
		}
	}

	/* Check names repetitions */
	for (i = 0; i < id; ++i) {
		if (strcmp((const char *)pHeaders[id].name, (const char *)pHeaders[i].name) == 0)
			return -EINVAL;
	}

	return 0;
}


ptable_partition_t *ptable_readPartitions(uint32_t *pCnt, const memory_properties_t *mem)
{
	int i;
	uintptr_t ptabAddr;
	uintptr_t partsAddr;
	uintptr_t magicAddr;
	uint32_t maxPartCnt;

	ptable_header_t tHeader;
	ptable_partition_t *pHeaders;
	uint8_t buff[sizeof(pt_magicBytes)];

	*pCnt = 0;
	pHeaders = NULL;

	/* Read and verify partition table header */
	ptabAddr = mem->memSize - mem->sectorSize;

	if (mem->read(ptabAddr, (void *)&tHeader, sizeof(ptable_header_t)) != sizeof(ptable_header_t))
		return NULL;

	maxPartCnt = (mem->sectorSize - sizeof(ptable_header_t) - sizeof(pt_magicBytes)) / sizeof(ptable_partition_t);
	if (tHeader.pCnt > maxPartCnt)
		return NULL;

	/* Read and verify magic bytes */
	magicAddr = ptabAddr + sizeof(ptable_header_t) + tHeader.pCnt * sizeof(ptable_partition_t);
	if (mem->read(magicAddr, (void *)buff, sizeof(pt_magicBytes)) != sizeof(pt_magicBytes))
		return NULL;

	if ((memcmp((void *)(buff), (void *)pt_magicBytes, sizeof(pt_magicBytes)) != 0))
		return NULL;

	/* Allocate and read partitions array */
	if ((pHeaders = (ptable_partition_t *)calloc(tHeader.pCnt, sizeof(ptable_partition_t))) == NULL)
		return NULL;

	partsAddr = ptabAddr + sizeof(ptable_header_t);
	if (mem->read(partsAddr, (void *)pHeaders, tHeader.pCnt * sizeof(ptable_partition_t)) != (tHeader.pCnt * sizeof(ptable_partition_t))) {
		free(pHeaders);
		return NULL;
	}

	/* Verify partition attributes */
	for (i = 0; i < tHeader.pCnt; ++i) {
		if (ptable_verifyPartition(i, pHeaders, mem) < 0) {
			free(pHeaders);
			return NULL;
		}
	}
	*pCnt = tHeader.pCnt;

	return pHeaders;
}


ssize_t ptable_writePartitions(ptable_partition_t *pHeaders, uint32_t pCnt, const memory_properties_t *mem)
{
	int i;
	char *buff;
	ssize_t res;
	uint32_t buffSize, offs;
	ptable_header_t tHeader = {0};

	offs = 0;
	buffSize = sizeof(ptable_header_t) + pCnt * sizeof(ptable_partition_t) + sizeof(pt_magicBytes);

	if ((buff = (char *)calloc(buffSize, sizeof(char))) == NULL)
		return -1;

	memset(buff, 0xff, buffSize);

	/* Verify partition attributes */
	for (i = 0; i < pCnt; ++i) {
		if (ptable_verifyPartition(i, pHeaders, mem) < 0) {
			free(buff);
			return -1;
		}
	}

	tHeader.pCnt = pCnt;

	memcpy(buff + offs, &tHeader, sizeof(ptable_header_t));
	offs += sizeof(ptable_header_t);

	memcpy(buff + offs, pHeaders, tHeader.pCnt * sizeof(ptable_partition_t));
	offs += tHeader.pCnt * sizeof(ptable_partition_t);

	memcpy(buff + offs, pt_magicBytes, sizeof(pt_magicBytes));

	res = mem->write(mem->memSize - mem->sectorSize, buff, buffSize);
	free(buff);

	return res;
}
