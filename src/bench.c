/* bench.c - the timed loops (ITCM) and the chain builder (flash).
 *
 * Timing: DWT_CYCCNT around the loop, with a compiler barrier after the
 * first read and a DSB before the second, so no load of the loop is moved
 * outside the window by the compiler or still in flight when it closes.
 * The fixed cost of that window is a few cycles against >= 16384 operations.
 *
 * These three functions are the measurement. Their machine code must not
 * change, or the published numbers stop being reproducible: compare the
 * disassembly (tools/disasm_check.py) after any edit here.
 */
#include "regs.h"
#include "bench.h"

#define BARRIER() __asm volatile("" ::: "memory")

ITCM_FN uintptr_t bench_chase(void *const *start, uint32_t loads, uint32_t *cycles)
{
    void *const *p = start;
    uint32_t n = loads / 8U;
    uint32_t t0 = DWT_CYCCNT;
    BARRIER();
    while (n-- != 0U) {
        p = (void *const *)*p;
        p = (void *const *)*p;
        p = (void *const *)*p;
        p = (void *const *)*p;
        p = (void *const *)*p;
        p = (void *const *)*p;
        p = (void *const *)*p;
        p = (void *const *)*p;
    }
    __asm volatile("dsb 0xF" ::: "memory");
    *cycles = DWT_CYCCNT - t0;
    return (uintptr_t)p;
}

ITCM_FN uint32_t bench_seq_read(const uint32_t *buf, uint32_t words, uint32_t reps,
                                uint32_t *cycles)
{
    uint32_t s0 = 0U;
    uint32_t s1 = 0U;
    uint32_t s2 = 0U;
    uint32_t s3 = 0U;
    uint32_t t0 = DWT_CYCCNT;
    BARRIER();
    while (reps-- != 0U) {
        const volatile uint32_t *p = buf;
        uint32_t n = words / 8U;
        while (n-- != 0U) {
            s0 += p[0];
            s1 += p[1];
            s2 += p[2];
            s3 += p[3];
            s0 += p[4];
            s1 += p[5];
            s2 += p[6];
            s3 += p[7];
            p += 8;                                  /* DEV-MEM-03 */
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
    while (reps-- != 0U) {
        volatile uint32_t *p = buf;
        uint32_t n = words / 8U;
        while (n-- != 0U) {
            p[0] = pattern;
            p[1] = pattern;
            p[2] = pattern;
            p[3] = pattern;
            p[4] = pattern;
            p[5] = pattern;
            p[6] = pattern;
            p[7] = pattern;
            p += 8;                                  /* DEV-MEM-03 */
        }
    }
    __asm volatile("dsb 0xF" ::: "memory");          /* stores drained */
    *cycles = DWT_CYCCNT - t0;
}
