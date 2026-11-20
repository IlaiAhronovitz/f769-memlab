/* cache.c - L1 cache and MPU control (ARMv7-M ARM B2.2.7, Cortex-M7 TRM).
 *
 * The set/way loops read the geometry from CCSIDR instead of assuming it, so
 * the same code reports whatever this engineering sample actually carries.
 *
 * The barrier and register sequences here are part of what the benchmark
 * measures (dmatest.c times the by-address operations): their machine code
 * must not change. Compare the disassembly after any edit.
 */
#include "regs.h"
#include "cache.h"

#define LINE_MASK (~((uintptr_t)CACHE_LINE - 1U))   /* clears the offset in a line */

void icache_enable(void)
{
    dsb();
    isb();
    SCB_ICIALLU = 0;
    dsb();
    isb();
    SCB_CCR |= CCR_IC;
    dsb();
    isb();
}

void icache_disable(void)
{
    dsb();
    isb();
    SCB_CCR &= ~CCR_IC;
    SCB_ICIALLU = 0;
    dsb();
    isb();
}

/* Apply one set/way operation (invalidate, or clean + invalidate) to every
 * line of the L1 data cache. */
static void dcache_all(volatile uint32_t *op)
{
    SCB_CSSELR = 0;                               /* L1 data cache */
    dsb();
    uint32_t ccsidr = SCB_CCSIDR;
    uint32_t sets = (ccsidr >> 13) & 0x7FFFU;     /* NumSets - 1       */
    uint32_t ways = (ccsidr >> 3) & 0x3FFU;       /* Associativity - 1 */
    do {
        uint32_t w = ways;
        do {
            *op = ((sets << 5) & 0x3FE0U) | ((w << 30) & 0xC0000000U);
        } while (w-- != 0U);
    } while (sets-- != 0U);
    dsb();
}

void dcache_enable(void)
{
    dcache_all(&SCB_DCISW);                       /* contents are random at reset */
    SCB_CCR |= CCR_DC;
    dsb();
    isb();
}

void dcache_disable(void)
{
    /* Never clean a cache that was never enabled: until the first
     * dcache_enable() invalidates it, its lines (and dirty bits) are not
     * guaranteed to be anything, and a clean would write them to memory. */
    if ((SCB_CCR & CCR_DC) != 0U) {
        SCB_CSSELR = 0;
        dsb();
        SCB_CCR &= ~CCR_DC;
        dsb();
        dcache_all(&SCB_DCCISW);                  /* write back what was dirty */
        isb();
    }
}

void dcache_clean_invalidate_all(void)
{
    dcache_all(&SCB_DCCISW);
    isb();
}

void dcache_clean_range(const void *addr, uint32_t len)
{
    uintptr_t a = (uintptr_t)addr & LINE_MASK;   /* DEV-MEM-01: address as integer */
    uintptr_t end = (uintptr_t)addr + len;
    dsb();
    for (; a < end; a += CACHE_LINE) {
        SCB_DCCMVAC = (uint32_t)a;
    }
    dsb();
    isb();
}

void dcache_invalidate_range(const void *addr, uint32_t len)
{
    uintptr_t a = (uintptr_t)addr & LINE_MASK;   /* DEV-MEM-01: address as integer */
    uintptr_t end = (uintptr_t)addr + len;
    dsb();
    for (; a < end; a += CACHE_LINE) {
        SCB_DCIMVAC = (uint32_t)a;
    }
    dsb();
    isb();
}

void mpu_region(uint32_t region, uint32_t base, uint32_t size_log2, uint32_t attr)
{
    MPU_RNR  = region;
    MPU_RBAR = base;                              /* VALID = 0: RNR picks the region */
    MPU_RASR = ((uint32_t)3U << 24)                         /* AP = full access, XN = 0 */
             | attr
             | ((size_log2 - 1U) << 1)
             | 1U;                                /* ENABLE */
}

void mpu_enable(void)
{
    dsb();
    MPU_CTRL = MPU_CTRL_ENABLE | MPU_CTRL_PRIVDEFENA;  /* default map elsewhere */
    dsb();
    isb();
}

void mpu_disable(void)
{
    dsb();
    MPU_CTRL = 0;
    for (uint32_t r = 0; r < 8U; r++) {
        MPU_RNR = r;
        MPU_RASR = 0;
    }
    dsb();
    isb();
}
