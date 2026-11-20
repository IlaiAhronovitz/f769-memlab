/* bench_chain.c - the pointer chain for bench_chase (built in flash, never
 * timed).
 *
 * Each 32-byte line of the buffer holds, in its first word, the address of
 * the next line to visit. Sattolo's algorithm makes that order one single
 * cycle through every line, so a chase of any length touches every line
 * before it repeats one.
 *
 * Layout constraint: this file links first in .text (build.py links the C files of src/
 * in name order), right before cache.c and dmatest.c, whose code runs from
 * flash inside the DMA test's timing windows. chase_build must keep its
 * published size (124 bytes) so those functions keep their addresses and
 * their alignment to the 32-byte I-cache lines: check with
 * tools/disasm_check.py after any edit here.
 */
#include <stddef.h>
#include "cache.h"
#include "bench_chain.h"

#define CHASE_MAX_LINES (CHASE_MAX_BYTES / CACHE_LINE)
#define LINE_SLOTS      (CACHE_LINE / sizeof(void *))   /* pointers per line */
#define DEFAULT_SEED    0x9E3779B9U                     /* any non-zero value */

/* xorshift32: small, no division, good enough to shuffle cache lines. */
static uint32_t xorshift32(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13U;
    x ^= x >> 17U;
    x ^= x << 5U;
    *state = x;
    return x;
}

void *const *chase_build(void *buf, uint32_t bytes, uint32_t seed)
{
    static uint16_t s_perm[CHASE_MAX_LINES];
    /* The bench buffer is reused as an array of pointers: DEV-MEM-03. */
    void **slots = (void **)buf;
    uint32_t n = bytes / CACHE_LINE;
    uint32_t state = (seed != 0U) ? seed : DEFAULT_SEED;

    for (uint32_t i = 0U; i < n; i++) {
        s_perm[i] = (uint16_t)i;
    }
    for (uint32_t i = n - 1U; i > 0U; i--) {        /* Sattolo: one n-cycle */
        uint32_t j = xorshift32(&state) % i;
        uint16_t t = s_perm[i];  /* NOLINT(clang-analyzer-security.ArrayBound): DEV-MEM-09 */
        s_perm[i] = s_perm[j];
        s_perm[j] = t;
    }
    for (uint32_t i = 0U; i < n; i++) {
        slots[(size_t)i * LINE_SLOTS] = (void *)&slots[(size_t)s_perm[i] * LINE_SLOTS];
    }
    return (void *const *)slots;
}
