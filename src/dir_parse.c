#include "ext2.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "endian.h"

static const char *dir_file_type_name(uint8_t type)
{
    switch (type) {
    case 0:
        return "unknown";
    case 1:
        return "regular";
    case 2:
        return "directory";
    case 3:
        return "char";
    case 4:
        return "block";
    case 5:
        return "fifo";
    case 6:
        return "socket";
    case 7:
        return "symlink";
    default:
        return "invalid";
    }
}

static int read_all_stdin(uint8_t **out_buf, size_t *out_size)
{
    size_t capacity = 4096;
    size_t size = 0;

    uint8_t *buf = malloc(capacity);
    if (buf == NULL) {
        perror("malloc");
        return -1;
    }

    for (;;) {
        if (size == capacity) {
            size_t new_capacity = capacity * 2;
            uint8_t *new_buf = realloc(buf, new_capacity);

            if (new_buf == NULL) {
                perror("realloc");
                free(buf);
                return -1;
            }

            buf = new_buf;
            capacity = new_capacity;
        }

        ssize_t n = read(STDIN_FILENO, buf + size, capacity - size);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }

            perror("read stdin");
            free(buf);
            return -1;
        }

        if (n == 0) {
            break;
        }

        size += (size_t)n;
    }

    *out_buf = buf;
    *out_size = size;
    return 0;
}

int main(void)
{
    uint8_t *buf = NULL;
    size_t size = 0;

    if (read_all_stdin(&buf, &size) < 0) {
        return 1;
    }

    printf("%-12s %-12s %s\n", "inode", "type", "name");

    size_t offset = 0;

    while (offset + 8 <= size) {
        struct ext2_dir_entry *entry = (struct ext2_dir_entry *)(void *)(buf + offset);

        uint32_t inode = ext2_le32_to_cpu(entry->inode);
        uint16_t rec_len = ext2_le16_to_cpu(entry->rec_len);
        uint8_t name_len = entry->name_len;
        uint8_t file_type = entry->file_type;

        if (rec_len == 0) {
            fprintf(stderr, "invalid directory entry: rec_len is zero at offset %zu\n", offset);
            free(buf);
            return 1;
        }

        if (offset + rec_len > size) {
            fprintf(stderr, "invalid directory entry: rec_len goes past input size at offset %zu\n", offset);
            free(buf);
            return 1;
        }

        if (rec_len < 8) {
            fprintf(stderr, "invalid directory entry: rec_len too small at offset %zu\n", offset);
            free(buf);
            return 1;
        }

        if ((size_t)name_len > (size_t)rec_len - 8) {
            fprintf(stderr, "invalid directory entry: name_len too large at offset %zu\n", offset);
            free(buf);
            return 1;
        }

        if (inode != 0) {
            printf("%-12u %-12s %.*s\n",
                   inode,
                   dir_file_type_name(file_type),
                   name_len,
                   entry->name);
        }

        offset += rec_len;
    }

    free(buf);
    return 0;
}
