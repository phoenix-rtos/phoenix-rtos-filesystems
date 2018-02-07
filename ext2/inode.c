/*
 * Phoenix-RTOS
 *
 * ext2
 *
 * inode.c
 *
 * Copyright 2017 Phoenix Systems
 * Author: Kamil Amanowicz
 *
 * This file is part of Phoenix-RTOS.
 *
 * %LICENSE%
 */
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/msg.h>

#include "ext2.h"
#include "inode.h"
#include "block.h"
#include "sb.h"

/*u32 find_inode(char *path, u32 len)
{
    u32 start, end;
    u32 ino = ROOTNODE_NO;
    start = 0;
    end = 0;

    if (!len || path[0] != '/')
        return 0;

    while (start != len)
    {
        split_path(path, &start, &end, len);
        if (start == end) {
            continue;
        }
        printf("path + start = %s start = %u end = %u\n", path + start, start, end);
        ino = search_dir(ino, path + start, end - start);

        if (!ino)
            return 0;
        start = end;
    }
    return ino;
}
*/

ext2_inode_t *inode_get(u32 ino)
{
    ext2_inode_t *inode_block;
	ext2_inode_t *inode;
    u32 group;
    u32 block;
    u32 inodes_in_block;

    if (ino < ROOTNODE_NO)
        return NULL;
    if (ino > ext2->inodes_count)
        return NULL;

    inode_block = malloc(ext2->block_size);
    group = (ino - 1) / ext2->inodes_in_group;

    inodes_in_block = (ext2->block_size / ext2->inode_size);
    block = ((ino - 1) % ext2->inodes_in_group) / inodes_in_block;

    read_block(ext2->gdt[group].ext2_inode_table + block, inode_block);

	inode = malloc(ext2->inode_size);

    memcpy(inode, inode_block + ((ino - 1) % inodes_in_block), ext2->inode_size);

    free(inode_block);
    return inode;
}

int inode_put(ext2_inode_t *inode) 
{
	free(inode);
	return EOK;
}

int inode_set(u32 ino, ext2_inode_t *inode)
{
    ext2_inode_t *inode_block;
    u32 group;
    u32 block;
    u32 inodes_in_block;

    if (ino < ROOTNODE_NO)
        return -EINVAL;
    if (ino > ext2->inodes_count)
        return -EINVAL;

    inode_block = malloc(ext2->block_size);

    group = (ino - 1) / ext2->inodes_in_group;

    inodes_in_block = (ext2->block_size / ext2->inode_size);
    block = ((ino - 1) % ext2->inodes_in_group) / inodes_in_block;

    read_block(ext2->gdt[group].ext2_inode_table + block, inode_block);

    memcpy(inode_block + ((ino - 1) % inodes_in_block), inode, ext2->inode_size);
    write_block(ext2->gdt[group].ext2_inode_table + block, inode_block);

    free(inode_block);
    return EOK;
}

static int find_group_dir(u32 pino)
{
    int i;
    int pgroup = (pino - 1) / ext2->inodes_in_group;
    int group = -1;
    int avefreei = ext2->sb->free_inodes_count / ext2->gdt_size;
    int avefreeb = ext2->sb->free_blocks_count / ext2->gdt_size;

    if(pino == ROOTNODE_NO)
        pgroup = rand() % ext2->gdt_size;
    for (i = 0; i < ext2->gdt_size; i++) {
        group = (pgroup + i) % ext2->gdt_size;
        if (ext2->gdt[group].free_inodes_count < avefreei)
            continue;
        if (ext2->gdt[group].free_blocks_count < avefreeb)
            continue;
    }
    if(group >= 0)
        goto out;

retry:
    for(i = 0; i < ext2->gdt_size; i++) {
        group = (pgroup + i) % ext2->gdt_size;
        if (ext2->gdt[group].free_inodes_count >= avefreei)
            goto out;
    }

    if (avefreei) {
        avefreei = 0;
        goto retry;
    }

out:
    return group;
}

static int find_group_file(u32 pino)
{
    int i;
    int ngroups = ext2->gdt_size;
    int pgroup = (pino - 1) / ext2->inodes_in_group;
    int group;

    if (ext2->gdt[pgroup].free_inodes_count && ext2->gdt[pgroup].free_blocks_count)
        return pgroup;

    group = (pgroup + pino) % ngroups;

    for (i = 0; i < ngroups; i <<= 1) {
        group += i;
        group = group % ngroups;
        if (ext2->gdt[group].free_inodes_count && ext2->gdt[group].free_blocks_count)
            return group;
    }

    group = pgroup;
    for (i = 0; i < ngroups; i++) {
        group++;
        group = group % ngroups;
        if (ext2->gdt[group].free_inodes_count)
            return group;
    }

    return -1;
}

u32 ext2_create_inode(ext2_inode_t *inode, u32 mode)
{
    u32 ino;
    int group;
    void *inode_bmp;

    if (mode & EXT2_S_IFDIR)
        group = find_group_dir(0);
    else
        group = find_group_file(0);

    if (group == -1)
        return 0;

    inode_bmp = malloc(ext2->block_size);

    printf("group = %u inode_bitmap = %u\n", group, ext2->gdt[group].inode_bitmap);
    read_block(ext2->gdt[group].inode_bitmap, inode_bmp);

    ino = find_zero_bit(inode_bmp, ext2->inodes_in_group);

    if (ino > ext2->inodes_in_group || ino <= 0)
        return 0;

    toggle_bit(inode_bmp, ino);

    ext2->gdt[group].free_inodes_count--;
    if (mode & EXT2_S_IFDIR)
        ext2->gdt[group].used_dirs_count++;
    ext2->sb->free_inodes_count--;

    memset(inode, 0, ext2->inode_size);
    inode->mode = mode | EXT2_S_IRUSR | EXT2_S_IWUSR;

    write_block(ext2->gdt[group].inode_bitmap, inode_bmp);
    ext2_write_sb();
    free(inode_bmp);
    printf("exit create %s\n", "");

	return group * ext2->inodes_in_group + ino;
}

int free_inode(u32 ino, ext2_inode_t *inode)
{

	u32 group = (ino - 1) / ext2->inodes_in_group;
	void *inode_bmp = malloc(ext2->block_size);

	ino = (ino - 1)% ext2->inodes_in_group;

	read_block(ext2->gdt[group].inode_bitmap, inode_bmp);

	toggle_bit(inode_bmp, ino);

	write_block(ext2->gdt[group].inode_bitmap, inode_bmp);

    return EOK;
}
