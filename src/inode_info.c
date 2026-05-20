#include "ext2_reader.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "endian.h"

static int parse_inode_number(const char *s, uint32_t *out)
{
    char *end = NULL;
    errno = 0;

    unsigned long value = strtoul(s, &end, 10);

    if (errno != 0 || end == s || *end != '\0' || value == 0 || value > UINT32_MAX) {
        return -1;
    }

    *out = (uint32_t)value;
    return 0;
}

static const char *inode_type_name(uint16_t mode)
{
    switch (mode & EXT2_S_IFMT) {
    case EXT2_S_IFREG:
        return "regular file";
    case EXT2_S_IFDIR:
        return "directory";
    case EXT2_S_IFLNK:
        return "symlink";
    case EXT2_S_IFCHR:
        return "character device";
    case EXT2_S_IFBLK:
        return "block device";
    case EXT2_S_IFIFO:
        return "fifo";
    case EXT2_S_IFSOCK:
        return "socket";
    default:
        return "unknown";
    }
}

static void print_block_pointers(const struct ext2_inode *inode)
{
    printf("block pointers:\n");

    for (int i = 0; i < EXT2_N_BLOCKS; i++) {
        uint32_t block = ext2_le32_to_cpu(inode->i_block[i]);

        if (i < 12) {
            printf("  direct[%2d]      = %u\n", i, block);
        } else if (i == 12) {
            printf("  single indirect = %u\n", block);
        } else if (i == 13) {
            printf("  double indirect = %u\n", block);
        } else {
            printf("  triple indirect = %u\n", block);
        }
    }
}

struct print_blocks_ctx {
    uint64_t file_blocks;
};

static int print_resolved_block(
    uint32_t logical_block,
    uint32_t physical_block,
    void *arg
)
{
    struct print_blocks_ctx *ctx = arg;

    if (logical_block >= ctx->file_blocks) {
        return 0;
    }

    if (physical_block == 0) {
        printf("  logical[%u] -> hole\n", logical_block);
    } else {
        printf("  logical[%u] -> physical %u\n", logical_block, physical_block);
    }

    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <ext2-image-or-device> <inode-number>\n", argv[0]);
        return 1;
    }

    uint32_t ino;
    if (parse_inode_number(argv[2], &ino) < 0) {
        fprintf(stderr, "invalid inode number: %s\n", argv[2]);
        return 1;
    }

    struct ext2_fs fs;
    if (ext2_open(&fs, argv[1]) < 0) {
        return 1;
    }

    struct ext2_inode inode;
    if (ext2_read_inode(&fs, ino, &inode) < 0) {
        ext2_close(&fs);
        return 1;
    }

    uint16_t mode = ext2_le16_to_cpu(inode.i_mode);

    printf("filesystem:\n");
    printf("  block size  = %u\n", fs.block_size);
    printf("  inode size  = %u\n", fs.inode_size);
    printf("  groups      = %u\n", fs.group_count);

    printf("\n");

    printf("inode %u:\n", ino);
    printf("  type        = %s\n", inode_type_name(mode));
    printf("  mode        = 0%o\n", mode);
    printf("  permissions = 0%o\n", mode & 0777);
    printf("  uid         = %u\n", ext2_le16_to_cpu(inode.i_uid));
    printf("  gid         = %u\n", ext2_le16_to_cpu(inode.i_gid));
    uint64_t file_size = ext2_inode_size(&inode);

    printf("  size        = %llu bytes\n", (unsigned long long)file_size);
    printf("  links       = %u\n", ext2_le16_to_cpu(inode.i_links_count));
    printf("  blocks      = %u sectors of 512 bytes\n", ext2_le32_to_cpu(inode.i_blocks));
    printf("  atime       = %u\n", ext2_le32_to_cpu(inode.i_atime));
    printf("  ctime       = %u\n", ext2_le32_to_cpu(inode.i_ctime));
    printf("  mtime       = %u\n", ext2_le32_to_cpu(inode.i_mtime));
    printf("  dtime       = %u\n", ext2_le32_to_cpu(inode.i_dtime));

    printf("\n");
    print_block_pointers(&inode);

    uint64_t file_blocks = 0;

    if (file_size > 0) {
        file_blocks = (file_size + fs.block_size - 1) / fs.block_size;
    }

    printf("\n");
    printf("resolved data blocks:\n");

    struct print_blocks_ctx ctx = {
        .file_blocks = file_blocks
    };

    if (ext2_walk_inode_blocks(&fs, &inode, file_size, print_resolved_block, &ctx) < 0) {
        ext2_close(&fs);
        return 1;
    }

    ext2_close(&fs);
    return 0;
}
