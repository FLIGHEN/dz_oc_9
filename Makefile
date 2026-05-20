CC := gcc

CFLAGS := -Wall -Wextra -g -std=c11 -D_DEFAULT_SOURCE -D_FILE_OFFSET_BITS=64 -Iinclude

BUILD_DIR := build

COMMON_OBJS := $(BUILD_DIR)/endian.o $(BUILD_DIR)/ext2_reader.o

INODE_INFO_OBJS := $(COMMON_OBJS) $(BUILD_DIR)/inode_info.o

INODE_DATA_OBJS := $(COMMON_OBJS) $(BUILD_DIR)/inode_data.o

DIR_PARSE_OBJS := $(BUILD_DIR)/endian.o $(BUILD_DIR)/dir_parse.o

.PHONY: all clean test valgrind

all: inode_info inode_data dir_parse

inode_info: $(INODE_INFO_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

inode_data: $(INODE_DATA_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

dir_parse: $(DIR_PARSE_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

test: all
	./tests/run_tests.sh

valgrind: all
	valgrind --leak-check=full ./inode_info ext2.img 2

	valgrind \
		--leak-check=full \
		./inode_data ext2.img 2 >/dev/null

	./inode_data ext2.img 2 | valgrind \
		--leak-check=full \
		./dir_parse >/dev/null

clean:
	rm -rf $(BUILD_DIR)

	rm -f inode_info inode_data dir_parse
