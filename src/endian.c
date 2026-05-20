#include "endian.h"

#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && defined(__ORDER_BIG_ENDIAN__)
#  if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#    define HOST_LITTLE_ENDIAN 1
#  elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#    define HOST_BIG_ENDIAN 1
#  else
#    error "Unsupported byte order"
#  endif
#else
#  error "Compiler does not define byte order macros"
#endif

uint16_t ext2_le16_to_cpu(uint16_t x)
{
#if defined(HOST_LITTLE_ENDIAN)
    return x;
#else
    return (uint16_t)((x >> 8) | (x << 8));
#endif
}

uint32_t ext2_le32_to_cpu(uint32_t x)
{
#if defined(HOST_LITTLE_ENDIAN)
    return x;
#else
    return ((x & 0x000000FFu) << 24) |
           ((x & 0x0000FF00u) << 8)  |
           ((x & 0x00FF0000u) >> 8)  |
           ((x & 0xFF000000u) >> 24);
#endif
}