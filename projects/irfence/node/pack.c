#include "pack.h"

uint8_t pack_u16(uint8_t *ptr, uint16_t val) {
    memcpy(ptr, &val, 2);
}

static uint8_t pack(uint8_t *ptr, uint8_t *val)
{
    uint8_t count = 0;
    for (i = 0; i < len; ++i)
        ptr[count++] = val;
    return len;
}
