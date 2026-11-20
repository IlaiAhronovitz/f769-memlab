/* bench.c - the timed loops (ITCM) and the chain builder (flash).
 *
 * Timing: DWT_CYCCNT around the loop, with a compiler barrier after the
 * first read and a DSB before the second, so no load of the loop is moved
 * outside the window by the compiler or still in flight when it closes.
 * The fixed cost of that window is a few cycles against >= 16384 operations.
 */
#include "regs.h"
#include "bench.h"
#include "cache.h"

#define BARRIER() __asm volatile("" ::: "memory")

ITCM_FN uintptr_t bench_chase(void *const *start, uint32_t loads, uint32_t *cycles)
{
    void *const *p = start;
    uint32_t n = loads / 8u;
    uint32_t t0 = DWT_CYCCNT;
    BARRIER();
    while (n--) {
        p = (void *const *)*p; p = (void *const *)*p;
        p = (void *const *)*p; p = (void *const *)*p;
        p = (void *const *)*p; p = (void *const *)*p;
        p = (void *const *)*p; p = (void *const *)*p;
    }
    __asm volatile("dsb 0xF" ::: "memory");
    *cycles = DWT_CYCCNT - t0;
    return (uintptr_t)p;
}

ITCM_FN uint32_t bench_seq_read(const uint32_t *buf, uint32_t words, uint32_t reps,
                                uint32_t *cycles)
{
    uint32_t s0 = 0, s1 = 0, s2 = 0, s3 = 0;
    uint32_t t0 = DWT_CYCCNT;
    BARRIER();
    while (reps--) {
        const volatile uint32_t *p = buf;
        uint32_t n = words / 8u;
        while (n--) {
            s0 += p[0]; s1 += p[1]; s2 += p[2]; s3 += p[3];
            s0 += p[4]; s1 += p[5]; s2 += p[6]; s3 += p[7];
            p += 8;
        }
    }
    __asm volatile("dsb 0xF" ::: "memory");
    *cycles = DWT_CYCCNT - t0;
    return s0 + s1 + s2 + s3;
}

ITCM_FN void bench_seq_write(uint32_t *buf, uint32_t words, uint32_t reps,
                             uint32_t pattern, uint32_t *cycles)
{
    uint32_t t0 = DWT_CYCCNT;
    BARRIER();
    while (reps--) {
        volatile uint32_t *p = buf;
        uint32_t n = words / 8u;
        while (n--) {
            p[0] = pattern; p[1] = pattern; p[2] = pattern; p[3] = pattern;
            p[4] = pattern; p[5] = pattern; p[6] = pattern; p[7] = pattern;
            p += 8;
        }
    }
    __asm volatile("dsb 0xF" ::: "memory");          /* stores drained */
    *cycles = DWT_CYCCNT - t0;
}

/* xorshift32: small, no division, good enough to shuffle cache lines. */
static uint32_t rng(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}

/* Scratch for the permutation: up to 128 KiB / 32 B = 4096 lines. */
static uint16_t s_perm[4096];

void *const *chase_build(void *buf, uint32_t bytes, uint32_t seed)
{
    uint32_t n = bytes / CACHE_LINE;
    uint8_t *base = (uint8_t *)buf;
    uint32_t s = seed ? seed : 0x9E3779B9u;

    for (uint32_t i = 0; i < n; i++) s_perm[i] = (uint16_t)i;
    for (uint32_t i = n - 1u; i > 0u; i--) {       /* Sattolo: one n-cycle */
        uint32_t j = rng(&s) % i;
        uint16_t t = s_perm[i];
        s_perm[i] = s_perm[j];
        s_perm[j] = t;
    }
    for (uint32_t i = 0; i < n; i++) {
        void **slot = (void **)(base + (uint32_t)i * CACHE_LINE);
        *slot = base + (uint32_t)s_perm[i] * CACHE_LINE;
    }
    return (void *const *)base;
}
