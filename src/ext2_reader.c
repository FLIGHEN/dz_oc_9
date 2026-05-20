#include "ext2_reader.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "endian.h"

static int read_exact_at(int fd, void *buf, size_t size, off_t offset)
{
    char *p = buf;
    size_t done = 0;

    while (done < size) {
        ssize_t n = pread(fd, p + done, size - done, offset + (off_t)done);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }

            return -1;
        }

        if (n == 0) {
            errno = EIO;
            return -1;
        }

        done += (size_t)n;
    }

    return 0;
}

static uint32_t div_round_up_u32(uint32_t a, uint32_t b)
{
    return (a + b - 1) / b;
}

int ext2_open(struct ext2_fs *fs, const char *path)
{
    memset(fs, 0, sizeof(*fs));
    fs->fd = -1;

    fs->fd = open(path, O_RDONLY);
    if (fs->fd < 0) {
        perror("open");
        return -1;
    }

    if (read_exact_at(fs->fd, &fs->sb, sizeof(fs->sb), EXT2_SUPERBLOCK_OFFSET) < 0) {
        perror("read superblock");
        ext2_close(fs);
        return -1;
    }

    uint16_t magic = ext2_le16_to_cpu(fs->sb.s_magic);
    if (magic != EXT2_SUPER_MAGIC) {
        fprintf(stderr, "not an ext2 filesystem: bad magic 0x%04x\n", magic);
        ext2_close(fs);
        return -1;
    }

    uint32_t log_block_size = ext2_le32_to_cpu(fs->sb.s_log_block_size);
    if (log_block_size > 6) {
        fprintf(stderr, "unsupported block size log: %u\n", log_block_size);
        ext2_close(fs);
        return -1;
    }

    fs->block_size = 1024u << log_block_size;

    uint32_t rev_level = ext2_le32_to_cpu(fs->sb.s_rev_level);
    if (rev_level == 0) {
        fs->inode_size = 128;
    } else {
        fs->inode_size = ext2_le16_to_cpu(fs->sb.s_inode_size);
    }

    if (fs->inode_size < sizeof(struct ext2_inode)) {
        fprintf(stderr, "unsupported inode size: %u\n", fs->inode_size);
        ext2_close(fs);
        return -1;
    }

    uint32_t blocks_count = ext2_le32_to_cpu(fs->sb.s_blocks_count);
    uint32_t blocks_per_group = ext2_le32_to_cpu(fs->sb.s_blocks_per_group);

    if (blocks_per_group == 0) {
        fprintf(stderr, "invalid ext2: blocks_per_group is zero\n");
        ext2_close(fs);
        return -1;
    }

    fs->group_count = div_round_up_u32(blocks_count, blocks_per_group);

    size_t groups_size = (size_t)fs->group_count * sizeof(struct ext2_group_desc);
    fs->groups = calloc(fs->group_count, sizeof(struct ext2_group_desc));
    if (fs->groups == NULL) {
        perror("calloc group descriptors");
        ext2_close(fs);
        return -1;
    }

    off_t group_desc_offset;

    if (fs->block_size == 1024) {
        group_desc_offset = 2 * 1024;
    } else {
        group_desc_offset = fs->block_size;
    }

    if (read_exact_at(fs->fd, fs->groups, groups_size, group_desc_offset) < 0) {
        perror("read group descriptors");
        ext2_close(fs);
        return -1;
    }

    return 0;
}

void ext2_close(struct ext2_fs *fs)
{
    if (fs == NULL) {
        return;
    }

    free(fs->groups);
    fs->groups = NULL;

    if (fs->fd >= 0) {
        close(fs->fd);
        fs->fd = -1;
    }
}

int ext2_read_block(struct ext2_fs *fs, uint32_t block_no, void *buf)
{
    off_t offset = (off_t)block_no * (off_t)fs->block_size;

    return read_exact_at(fs->fd, buf, fs->block_size, offset);
}

int ext2_read_inode(struct ext2_fs *fs, uint32_t ino, struct ext2_inode *inode)
{
    if (ino == 0) {
        fprintf(stderr, "invalid inode number: 0\n");
        return -1;
    }

    uint32_t inodes_count = ext2_le32_to_cpu(fs->sb.s_inodes_count);
    uint32_t inodes_per_group = ext2_le32_to_cpu(fs->sb.s_inodes_per_group);

    if (ino > inodes_count) {
        fprintf(stderr, "inode %u out of range, max is %u\n", ino, inodes_count);
        return -1;
    }

    if (inodes_per_group == 0) {
        fprintf(stderr, "invalid ext2: inodes_per_group is zero\n");
        return -1;
    }

    uint32_t index = ino - 1;
    uint32_t group = index / inodes_per_group;
    uint32_t local_index = index % inodes_per_group;

    if (group >= fs->group_count) {
        fprintf(stderr, "inode group %u out of range\n", group);
        return -1;
    }

    uint32_t inode_table_block =
        ext2_le32_to_cpu(fs->groups[group].bg_inode_table);

    off_t inode_offset =
        (off_t)inode_table_block * (off_t)fs->block_size +
        (off_t)local_index * (off_t)fs->inode_size;

    return read_exact_at(fs->fd, inode, sizeof(*inode), inode_offset);
}

uint64_t ext2_inode_size(const struct ext2_inode *inode)
{
    uint64_t size = ext2_le32_to_cpu(inode->i_size);
    uint16_t mode = ext2_le16_to_cpu(inode->i_mode);

    if ((mode & EXT2_S_IFMT) == EXT2_S_IFREG) {
        size |= (uint64_t)ext2_le32_to_cpu(inode->i_dir_acl) << 32;
    }

    return size;
}

static int walk_data_block(
    uint32_t *logical,
    uint32_t physical,
    ext2_block_callback cb,
    void *arg
)
{
    int rc = cb(*logical, physical, arg);
    if (rc < 0) {
        return -1;
    }

    (*logical)++;
    return 0;
}

static int walk_indirect_level(
    struct ext2_fs *fs,
    uint32_t block_no,
    int level,
    uint32_t *logical,
    uint64_t max_blocks,
    ext2_block_callback cb,
    void *arg
)
{
    uint32_t pointers_per_block = fs->block_size / sizeof(uint32_t);

    if (*logical >= max_blocks) {
        return 0;
    }

    if (block_no == 0) {
        if (level == 1) {
            for (uint32_t i = 0; i < pointers_per_block; i++) {
                if (*logical >= max_blocks) {
                    return 0;
                }

                if (walk_data_block(logical, 0, cb, arg) < 0) {
                    return -1;
                }
            }
        } else {
            for (uint32_t i = 0; i < pointers_per_block; i++) {
                if (*logical >= max_blocks) {
                    return 0;
                }

                if (walk_indirect_level(fs, 0, level - 1, logical, max_blocks, cb, arg) < 0) {
                    return -1;
                }
            }
        }

        return 0;
    }

    uint32_t *buf = malloc(fs->block_size);
    if (buf == NULL) {
        perror("malloc indirect block");
        return -1;
    }

    if (ext2_read_block(fs, block_no, buf) < 0) {
        perror("read indirect block");
        free(buf);
        return -1;
    }

    for (uint32_t i = 0; i < pointers_per_block; i++) {
        if (*logical >= max_blocks) {
            free(buf);
            return 0;
        }

        uint32_t child = ext2_le32_to_cpu(buf[i]);

        if (level == 1) {
            if (walk_data_block(logical, child, cb, arg) < 0) {
                free(buf);
                return -1;
            }
        } else {
            if (walk_indirect_level(fs, child, level - 1, logical, max_blocks, cb, arg) < 0) {
                free(buf);
                return -1;
            }
        }
    }

    free(buf);
    return 0;
}

int ext2_walk_inode_blocks(
    struct ext2_fs *fs,
    const struct ext2_inode *inode,
    uint64_t file_size,
    ext2_block_callback cb,
    void *arg
)
{
    uint32_t logical = 0;

    uint64_t max_blocks = 0;

    if (file_size > 0) {
        max_blocks = (file_size + fs->block_size - 1) / fs->block_size;
    }

    for (int i = 0; i < 12; i++) {
        uint32_t physical = ext2_le32_to_cpu(inode->i_block[i]);

        if (logical >= max_blocks) {
            return 0;
        }
        
        if (walk_data_block(&logical, physical, cb, arg) < 0) {
            return -1;
        }
    }

    uint32_t single_indirect = ext2_le32_to_cpu(inode->i_block[12]);
    if (walk_indirect_level(fs, single_indirect, 1, &logical, max_blocks, cb, arg) < 0) {
        return -1;
    }

    uint32_t double_indirect = ext2_le32_to_cpu(inode->i_block[13]);
    if (walk_indirect_level(fs, double_indirect, 2, &logical, max_blocks, cb, arg) < 0) {
        return -1;
    }

    uint32_t triple_indirect = ext2_le32_to_cpu(inode->i_block[14]);
    if (walk_indirect_level(fs, triple_indirect, 3, &logical, max_blocks, cb, arg) < 0) {
        return -1;
    }

    return 0;
}
