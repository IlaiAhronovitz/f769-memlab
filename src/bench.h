/* bench.h - timed memory loops. They run from ITCM and call nothing, so the
 * instruction side is identical in every cache configuration. */
#pragma once
#include <stdint.h>

/* long_call: the code runs from ITCM (0x00000100) and is called from flash
 * (0x08000000), out of BL range. clang has no such attribute (it would need
 * -mlong-calls); clang only analyses this code, it never builds the image. */
#if defined(__clang__)
#define ITCM_FN __attribute__((section(".itcm_text"), noinline))
#else
#define ITCM_FN __attribute__((section(".itcm_text"), noinline, long_call))
#endif

/* Follow `loads` dependent pointers starting at `start` (loads % 8 == 0).
 * Returns the last pointer so the chain cannot be optimised away. */
ITCM_FN uintptr_t bench_chase(void *const *start, uint32_t loads, uint32_t *cycles);

/* Sum `words` 32-bit words, `reps` times (words % 8 == 0). Returns the sum. */
ITCM_FN uint32_t bench_seq_read(const uint32_t *buf, uint32_t words, uint32_t reps,
                                uint32_t *cycles);

/* Store `words` 32-bit words, `reps` times (words % 8 == 0). */
ITCM_FN void bench_seq_write(uint32_t *buf, uint32_t words, uint32_t reps,
                             uint32_t pattern, uint32_t *cycles);
