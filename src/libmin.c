/* libmin.c - the two functions GCC may call on its own in a -nostdlib build.
 * Standard names on purpose (DEV-MEM-04); void * parameters by the standard
 * signature (DEV-MEM-03). Byte loops, no word tricks: nothing here is timed. */
#include <stdint.h>
#include "libmin.h"

void *memset(void *dst, int c, size_t n)
{
    uint8_t *d = dst;
    for (size_t i = 0U; i < n; i++) {
        d[i] = (uint8_t)c;
    }
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    for (size_t i = 0U; i < n; i++) {
        d[i] = s[i];
    }
    return dst;
}
