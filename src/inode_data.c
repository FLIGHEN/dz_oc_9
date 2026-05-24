#include "ext2_reader.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "endian.h"

struct dump_ctx {
    struct ext2_fs *fs;
    uint64_t remaining;
    void *block_buf;
};

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

static int write_all(int fd, const void *buf, size_t size)
{
    const char *p = buf;
    size_t done = 0;

    while (done < size) {
        ssize_t n = write(fd, p + done, size - done);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }

            return -1;
        }

        done += (size_t)n;
    }

    return 0;
}

static int dump_block(uint32_t logical_block, uint32_t physical_block, void *arg)
{
    (void)logical_block;

    struct dump_ctx *ctx = arg;

    if (ctx->remaining == 0) {
        return 0;
    }

    size_t to_write = ctx->fs->block_size;

    if (ctx->remaining < to_write) {
        to_write = (size_t)ctx->remaining;
    }

    if (physical_block == 0) {
        char zeros[4096] = {0};
        size_t left = to_write;

        while (left > 0) {
            size_t chunk = left < sizeof(zeros) ? left : sizeof(zeros);

            if (write_all(STDOUT_FILENO, zeros, chunk) < 0) {
                perror("write zeros");
                return -1;
            }

            left -= chunk;
        }
    } else {
        if (ext2_read_block(ctx->fs, physical_block, ctx->block_buf) < 0) {
            perror("read data block");
            return -1;
        }

        if (write_all(STDOUT_FILENO, ctx->block_buf, to_write) < 0) {
            perror("write stdout");
            return -1;
        }
    }

    ctx->remaining -= to_write;
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

    void *block_buf = malloc(fs.block_size);
    if (block_buf == NULL) {
        perror("malloc");
        ext2_close(&fs);
        return 1;
    }

    struct dump_ctx ctx = {
        .fs = &fs,
        .remaining = ext2_inode_size(&inode),
        .block_buf = block_buf
    };

    uint64_t file_size = ext2_inode_size(&inode);

    if (ext2_walk_inode_blocks(&fs, &inode, file_size, dump_block, &ctx) < 0) {
        free(block_buf);
        ext2_close(&fs);
        return 1;
    }

    free(block_buf);
    ext2_close(&fs);
    return 0;
}
