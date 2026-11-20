/* dmatest.c - what goes wrong when DMA and a write-back D-cache share
 * memory, and what each fix costs.
 *
 * A (source) and B (destination) are 8 KiB each in SRAM1, together exactly the
 * 16 KiB D-cache, so in the stale case every line can still be in the cache:
 *   1. A and B hold old patterns in memory, nothing cached.
 *   2. The CPU reads B: B's old lines are now cached (clean).
 *   3. The CPU writes A's new pattern: it sits dirty in the cache.
 *   4. DMA2 copies A -> B in memory: it reads A's OLD memory, and B's memory
 *      changes behind B's cached lines.
 *   5. The CPU reads B: count words that are not A's new pattern.
 * Fix 1: clean A before the DMA, invalidate B after it.
 * Fix 2: an MPU region makes the buffers non-cacheable.
 */
#include "regs.h"
#include "cache.h"
#include "results.h"
#include "dmatest.h"

#define BUF_BYTES   8192u
#define BUF_WORDS   (BUF_BYTES / 4u)
#define DMABUF_BASE 0x20060000u     /* 64 KiB, aligned: one MPU region */

static uint32_t s_a[BUF_WORDS] __attribute__((section(".dma_buf"), aligned(32)));
static uint32_t s_b[BUF_WORDS] __attribute__((section(".dma_buf"), aligned(32)));

static uint32_t cyc(void) { return DWT_CYCCNT; }

/* DMA2 stream 0, channel 0, memory-to-memory, 32-bit, FIFO mode.
 * In M2M the peripheral port is the source. Returns cycles, or 0 on error. */
static uint32_t dma_copy(const uint32_t *src, uint32_t *dst, uint32_t words)
{
    DMA2_S0CR = 0;
    while (DMA2_S0CR & DMA_CR_EN) {}
    DMA2_LIFCR  = DMA_LIFCR_S0_ALL;
    DMA2_S0PAR  = (uint32_t)(uintptr_t)src;
    DMA2_S0M0AR = (uint32_t)(uintptr_t)dst;
    DMA2_S0NDTR = words;
    DMA2_S0FCR  = DMA_FCR_DMDIS | DMA_FCR_FTH_FULL;
    DMA2_S0CR   = DMA_CR_DIR_M2M | DMA_CR_PINC | DMA_CR_MINC
                | DMA_CR_PSIZE_W | DMA_CR_MSIZE_W;
    dsb();
    uint32_t t0 = cyc();
    DMA2_S0CR |= DMA_CR_EN;
    for (;;) {
        uint32_t isr = DMA2_LISR;
        if (isr & DMA_LISR_TCIF0) break;
        if (isr & DMA_LISR_S0_ERR) return 0;
    }
    uint32_t t = cyc() - t0;
    DMA2_LIFCR = DMA_LIFCR_S0_ALL;
    return t;
}

static void fill(uint32_t *buf, uint32_t tag)
{
    for (uint32_t i = 0; i < BUF_WORDS; i++) buf[i] = tag | i;
}

static uint32_t mismatches(const volatile uint32_t *buf, uint32_t tag)
{
    uint32_t bad = 0;
    for (uint32_t i = 0; i < BUF_WORDS; i++) bad += (buf[i] != (tag | i));
    return bad;
}

static uint32_t touch(const volatile uint32_t *buf)
{
    uint32_t s = 0;
    for (uint32_t i = 0; i < BUF_WORDS; i++) s += buf[i];
    return s;
}

/* Steps 1-3: old data in memory, B cached clean, A's new data dirty.
 * Returns the cycles the CPU took to write A. */
static uint32_t prepare(void)
{
    fill(s_a, 0xA0000000u);
    fill(s_b, 0xB0000000u);
    dcache_clean_invalidate_all();
    (void)touch(s_b);
    uint32_t t0 = cyc();
    fill(s_a, 0xC0000000u);
    dsb();
    return cyc() - t0;
}

void dmatest_run(void)
{
    RCC_AHB1ENR |= AHB1ENR_DMA2EN;
    (void)RCC_AHB1ENR;

    /* No maintenance: the failure this test exists to show. */
    uint32_t fill_c = prepare();
    uint32_t dma_c = dma_copy(s_a, s_b, BUF_WORDS);
    res_add(T_CPU_FILL, R_DMABUF, C_ID_WB, BUF_BYTES, BUF_WORDS, fill_c, 0);
    res_add(T_DMA_COPY, R_DMABUF, C_ID_WB, BUF_BYTES, BUF_WORDS, dma_c, dma_c != 0u);
    res_add(T_DMA_STALE, R_DMABUF, C_ID_WB, BUF_BYTES, BUF_WORDS, dma_c,
            mismatches(s_b, 0xC0000000u));

    /* Fix 1: clean the source before, invalidate the destination after. */
    (void)prepare();
    uint32_t t0 = cyc();
    dcache_clean_range(s_a, BUF_BYTES);
    uint32_t clean_c = cyc() - t0;
    dma_c = dma_copy(s_a, s_b, BUF_WORDS);
    t0 = cyc();
    dcache_invalidate_range(s_b, BUF_BYTES);
    uint32_t inval_c = cyc() - t0;
    res_add(T_CLEAN, R_DMABUF, C_ID_WB, BUF_BYTES, BUF_BYTES / CACHE_LINE, clean_c, 0);
    res_add(T_INVAL, R_DMABUF, C_ID_WB, BUF_BYTES, BUF_BYTES / CACHE_LINE, inval_c, 0);
    res_add(T_DMA_MAINT, R_DMABUF, C_ID_WB, BUF_BYTES, BUF_WORDS, clean_c + inval_c,
            mismatches(s_b, 0xC0000000u));

    /* Fix 2: the whole DMA area non-cacheable, no maintenance at all. */
    dcache_clean_invalidate_all();
    mpu_region(0, DMABUF_BASE, 16u, MPU_ATTR_NC);
    mpu_enable();
    fill_c = prepare();
    dma_c = dma_copy(s_a, s_b, BUF_WORDS);
    res_add(T_CPU_FILL, R_DMABUF, C_ID_NC, BUF_BYTES, BUF_WORDS, fill_c, 0);
    res_add(T_DMA_NC, R_DMABUF, C_ID_NC, BUF_BYTES, BUF_WORDS, dma_c,
            mismatches(s_b, 0xC0000000u));
    dcache_clean_invalidate_all();
    mpu_disable();
}
