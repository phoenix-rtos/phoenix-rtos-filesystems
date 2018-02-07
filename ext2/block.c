/*
 * Phoenix-RTOS
 *
 * ext2
 *
 * block.c
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
#include <string.h>
#include <errno.h>
#include <sys/msg.h>

#include "ext2.h"
#include "block.h"
#include "sb.h"
#include "pc-ata.h"


int write_block(u32 block, void *data)
{
    int ret;

    if(!block)
        return EOK;

    ret = ata_write(block_offset(block), data, ext2->block_size);

    if (ret == ext2->block_size)
        return EOK;
    return -EINVAL;
}

int read_block(u32 block, void *data)
{
    int ret;


    if(!block) {
        memset(data, 0, ext2->block_size);
        return EOK;
    }

    ret = ata_read(block_offset(block), data, ext2->block_size);

    if (ret == ext2->block_size)
        return EOK;
    return ret;
}

/* reads from starting block */
int read_blocks(u32 block, void *data, u32 size)
{
    int len = 0;
    if (size % ext2->block_size)
        return -EINVAL;

    while(len < size) {
        if(!read_block(block, data + len)) {
            len += ext2->block_size;
            block++;
        }
        else return -EINVAL;
    }
    return EOK;
}

/* search block for a given file name */
u32 search_block(void *data, const char *name, u8 len)
{
    ext2_dir_entry_t *entry;
    int off = 0;

    while (off < ext2->block_size) {
        entry = data + off;

        if (entry->rec_len == 0)
            return 0;
        if (len == entry->name_len
                && !strncmp(name, entry->name, len)) {
            return entry->inode;
        }
        off += entry->rec_len;
    }
    return 0;
}

/* calculates the offset inside indirection blocks, returns depth of the indirection */
static int block_off(long block_no, u32 off[4])
{
    int addr = 256 << ext2->sb->log_block_size;
    int addr_bits = 8 + ext2->sb->log_block_size;
    int n = 0;
    long dir_blocks = EXT2_NDIR_BLOCKS;
    long ind_blocks = addr;
    long dind_blocks = addr << addr_bits;
    if (block_no < 0)
        return -EINVAL;
    else if (block_no < dir_blocks) {
        off[n++] = block_no;
    } else if ((block_no -= dir_blocks) < ind_blocks) {
        off[n++] = block_no;
        off[n++] = EXT2_IND_BLOCK;
    } else if ((block_no -= ind_blocks) < dind_blocks) {
        off[n++] = block_no & (addr - 1);
        off[n++] = block_no >> addr_bits;
        off[n++] = EXT2_DIND_BLOCK;
    } else if ((block_no -= dind_blocks) >= 0) {
        off[n++] = block_no & (addr - 1);
        off[n++] = (block_no >> addr_bits) & (addr - 1);
        off[n++] = block_no >> (addr_bits * 2);
        off[n++] = EXT2_TIND_BLOCK;
    }
    return n;
}

u32 get_block_no(ext2_inode_t *inode, u32 block)
{
    u32 off[4];
    u32 *buff[3];
    buff[0] = malloc(ext2->block_size);
    buff[1] = malloc(ext2->block_size);
    buff[2] = malloc(ext2->block_size);
    u32 ret;
    int depth = block_off(block, off);
    //printf("off 1 = %u 2 = %u 3 = %u 4 = %u\n", off[0], off[1], off[2], off[3]);
    if (depth >= 4)
        read_block(inode->block[off[3]], buff[2]);
    if (depth >= 3) {
        if (depth >= 4)
            read_block(*(buff[2] + off[2]), buff[1]);
        else
            read_block(inode->block[off[2]], buff[1]);
    }
    if (depth >= 2) {
        if (depth >= 3)
            read_block(*(buff[1] + off[1]), buff[0]);
        else
            read_block(inode->block[off[1]], buff[0]);
    }

    ret = depth > 1 ? *(buff[0] + off[0]) : inode->block[off[0]];
    free(buff[0]);
    free(buff[1]);
    free(buff[2]);
    return ret;
}

/*
int set_block_no(ext2_inode_t *inode, u32 boff, u32 bno)
{
    u32 off[4];
    int depth = 0;

    depth = block_off(boff, off);
    buff[0] = malloc(ext2->block_size);
    buff[1] = malloc(ext2->block_size);
    buff[2] = malloc(ext2->block_size);
    u32 ret;
    int depth = block_off(block, off);
    //printf("off 1 = %u 2 = %u 3 = %u 4 = %u\n", off[0], off[1], off[2], off[3]);
    if (depth >= 4) {
        read_block(inode->block[off[3]], buff[2]);
    }
    if (depth >= 3) {
        if (depth >= 4)
            read_block(*(buff[2] + off[2]), buff[1]);
        else
            read_block(inode->block[off[2]], buff[1]);
    }
    if (depth >= 2) {
        if (depth >= 3)
            read_block(*(buff[1] + off[1]), buff[0]);
        else
            read_block(inode->block[off[1]], buff[0]);
    }

    ret = depth > 1 ? *(buff[0] + off[0]) : inode->block[off[0]];
    free(buff[0]);
    free(buff[1]);
    free(buff[2]);
    return ret;

}*/

u32 new_block(u32 ino, ext2_inode_t *inode, u32 bno)
{
    int group = 0, pgroup = 0;
    u32 off = 0;
    void *block_bmp = malloc(ext2->block_size);

    if (bno) {
        group = (bno - 1) / ext2->blocks_in_group;
        off = ((bno - 1) % ext2->blocks_in_group) + 1;
    }
    else
        group = (ino - 1) / ext2->inodes_in_group;

    pgroup = group;

again:
    read_block(ext2->gdt[group].block_bitmap, block_bmp);

    if (!off)
        off = find_zero_bit(block_bmp, ext2->blocks_in_group);
    else {
        if (!check_bit(block_bmp, off))
            goto found;
        off = find_zero_bit(block_bmp, ext2->blocks_in_group);
    }

    if (off <= 0 || off > ext2->blocks_in_group) {
        group = (group + 1) % ext2->gdt_size;
        if (group == pgroup)
            goto out;
        goto again;
    }

found:
    toggle_bit(block_bmp, off);
    write_block(ext2->gdt[group].block_bitmap, block_bmp);
    ext2->sb->free_blocks_count--;
    ext2_write_sb();
    free(block_bmp);
    return group * ext2->blocks_in_group + off;

out:
    free(block_bmp);
    return 0;
}

/* gets block of a given number (relative to inode) */
void get_block(ext2_inode_t *inode, u32 block, void *data,
               u32 off[4], u32 prev_off[4], u32 *buff[3])
{
    int depth = block_off(block, off);
    //printf("off 1 = %u 2 = %u 3 = %u 4 = %u\n", off[0], off[1], off[2], off[3]);
    if (depth >= 4 && prev_off[3] != off[3]) {
        read_block(inode->block[off[3]], buff[2]);
        prev_off[3] = off[3];
    }
    if (depth >= 3 && prev_off[2] != off[2]) {
        if (depth >= 4)
            read_block(*(buff[2] + off[2]), buff[1]);
        else
            read_block(inode->block[off[2]], buff[1]);
        prev_off[2] = off[2];
    }
    if (depth >= 2 && prev_off[1] != off[1]) {
        if (depth >= 3)
            read_block(*(buff[1] + off[1]), buff[0]);
        else
            read_block(inode->block[off[1]], buff[0]);
        prev_off[1] = off[1];
    }
    if (depth > 1)
        read_block(*(buff[0] + off[0]), data);
    else
        read_block(inode->block[off[0]], data);

    prev_off[0] = off[0];
}

/* sets block of a given number (relative to inode) */
void set_block(u32 ino, ext2_inode_t *inode, u32 block, void *data,
               u32 off[4], u32 prev_off[4], u32 *buff[3])
{
    int depth = block_off(block, off);


    //printf("off 1 = %u 2 = %u 3 = %u 4 = %u\n", off[0], off[1], off[2], off[3]);
    if (depth >= 4 && prev_off[3] != off[3]) {
        if(!inode->block[off[3]]) {
            inode->block[off[3]] = new_block(ino, inode, 0);
            read_block(0, buff[2]);
        } else read_block(inode->block[off[3]], buff[2]);
        prev_off[3] = off[3];
    }
    if (depth >= 3 && prev_off[2] != off[2]) {
        if (depth >= 4) {
            if(!*(buff[2] + off[2])) {
                *(buff[2] + off[2]) = new_block(ino, inode, 0);
                write_block(inode->block[off[3]], buff[2]);
                read_block(0, buff[1]);
            } else read_block(*(buff[2] + off[2]), buff[1]);
        } else if (!inode->block[off[2]]) {
            inode->block[off[2]] = new_block(ino, inode, 0);
            read_block(0, buff[1]);
        } else
            read_block(inode->block[off[2]], buff[1]);

        prev_off[2] = off[2];
    }
    if (depth >= 2 && prev_off[1] != off[1]) {
        if (depth >= 3) {
            if(!*(buff[1] + off[1])) {
                *(buff[1] + off[1]) = new_block(ino, inode, 0);
                write_block(*(buff[2] + off[2]), buff[1]);
                read_block(0, buff[0]);
            } else read_block(*(buff[1] + off[1]), buff[0]);
        } else if(!inode->block[off[1]]) {
            inode->block[off[1]] = new_block(ino, inode, 0);
            read_block(0, buff[0]);
        } else
            read_block(inode->block[off[1]], buff[0]);

        prev_off[1] = off[1];
    }

    if (depth > 1) {
        if (!*(buff[0] + off[0])) {
            *(buff[0] + off[0]) = new_block(ino, inode, 0);
            write_block(*(buff[1] + off[1]), buff[0]);
        }
        write_block(*(buff[0] + off[0]), data);
    } else {
        if(!inode->block[off[0]])
            inode->block[off[0]] = new_block(ino, inode, 0);
        write_block(inode->block[off[0]], data);
    }

    inode->blocks += ext2->block_size / 512;
    prev_off[0] = off[0];
}
