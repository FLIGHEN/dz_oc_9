#ifndef EXT2_ENDIAN_H
#define EXT2_ENDIAN_H

#include <stdint.h>

uint16_t ext2_le16_to_cpu(uint16_t x);
uint32_t ext2_le32_to_cpu(uint32_t x);

#endif