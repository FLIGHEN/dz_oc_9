#ifndef EXT2_READER_H
#define EXT2_READER_H

#include <stdint.h>

#include "ext2.h"

struct ext2_fs {
    int fd;

    struct ext2_super_block sb;
    struct ext2_group_desc *groups;

    uint32_t block_size;
    uint32_t group_count;
    uint32_t inode_size;
};

typedef int (*ext2_block_callback)(
    uint32_t logical_block,
    uint32_t physical_block,
    void *arg
);

int ext2_open(struct ext2_fs *fs, const char *path);
void ext2_close(struct ext2_fs *fs);

int ext2_read_block(struct ext2_fs *fs, uint32_t block_no, void *buf);
int ext2_read_inode(struct ext2_fs *fs, uint32_t ino, struct ext2_inode *inode);

uint64_t ext2_inode_size(const struct ext2_inode *inode);
int ext2_walk_inode_blocks(struct ext2_fs *fs, const struct ext2_inode *inode, uint64_t file_size, ext2_block_callback cb, void *arg);

#endif
