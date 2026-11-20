/* main.c - f769-memlab: measure the Cortex-M7 memory system of the
 * STM32F769I-EVAL, configuration by configuration, into g_res (DTCM).
 *
 * Per cache configuration and per memory region:
 *   - pointer chase over 1 KiB .. region size: cycles per dependent load
 *   - sequential read and write: bytes per cycle
 *   - flash read through AXIM and through ITCM + ART
 * Then the DMA coherence test (dmatest.c). The host reads the block over SWD.
 */
#include <string.h>
#include "regs.h"
#include "system.h"
#include "cache.h"
#include "bench.h"
#include "dmatest.h"
#include "results.h"

extern uint32_t g_vectors[];

#define CHASE_LOADS  16384u
#define SEQ_BYTES    (256u * 1024u)      /* bytes moved per sequential test */

static uint8_t s_dtcm[64u * 1024u]  __attribute__((section(".dtcm_buf"),  aligned(32)));
static uint8_t s_sram1[128u * 1024u] __attribute__((section(".sram1_buf"), aligned(32)));
static uint8_t s_sram2[16u * 1024u] __attribute__((section(".sram2_buf"), aligned(32)));
static uint8_t s_itcm[8u * 1024u]   __attribute__((section(".itcm_buf"),  aligned(32)));

typedef struct {
    uint8_t  id;
    uint8_t *buf;
    uint32_t max;
} region_t;

static const region_t k_regions[] = {
    { R_DTCM,  s_dtcm,  sizeof s_dtcm  },
    { R_SRAM1, s_sram1, sizeof s_sram1 },
    { R_SRAM2, s_sram2, sizeof s_sram2 },
    { R_ITCM,  s_itcm,  sizeof s_itcm  },
};

#define SRAM1_BENCH_BASE 0x20040000u     /* 128 KiB -> MPU size 2^17 */
#define SRAM2_BENCH_BASE 0x2007C000u     /*  16 KiB -> MPU size 2^14 */

static void apply_cfg(uint8_t cfg)
{
    dcache_disable();
    icache_disable();
    mpu_disable();
    art_enable(1);

    switch (cfg) {
    case C_OFF:
        break;
    case C_I:
        icache_enable();
        break;
    case C_ID_WB:
        icache_enable();
        dcache_enable();
        break;
    case C_ID_WT:
    case C_ID_NC: {
        uint32_t attr = (cfg == C_ID_WT) ? MPU_ATTR_WT : MPU_ATTR_NC;
        mpu_region(0, SRAM1_BENCH_BASE, 17u, attr);
        mpu_region(1, SRAM2_BENCH_BASE, 14u, attr);
        mpu_enable();
        icache_enable();
        dcache_enable();
        break;
    }
    case C_OFF_NOART:
        art_enable(0);
        break;
    case C_ID_NOART:
        art_enable(0);
        icache_enable();
        dcache_enable();
        break;
    default:
        break;
    }
}

static void run_chase(const region_t *r, uint8_t cfg)
{
    for (uint32_t size = 1024u; size <= r->max; size *= 2u) {
        void *const *start = chase_build(r->buf, size, 0x1234567u + size);
        uint32_t c, warm = ((size / CACHE_LINE) + 7u) & ~7u;
        (void)bench_chase(start, warm, &c);                /* warm-up pass */
        uintptr_t end = bench_chase(start, CHASE_LOADS, &c);
        res_add(T_CHASE, r->id, cfg, size, CHASE_LOADS, c, (uint32_t)end);
        g_res.step++;
    }
}

static void run_seq(const region_t *r, uint8_t cfg)
{
    uint32_t sizes[2] = { 8u * 1024u, r->max };
    uint32_t n = (r->max > sizes[0]) ? 2u : 1u;
    if (n == 1u) sizes[0] = r->max;

    for (uint32_t k = 0; k < n; k++) {
        uint32_t size = sizes[k], words = size / 4u, reps = SEQ_BYTES / size;
        uint32_t *buf = (uint32_t *)(void *)r->buf;
        uint32_t c;

        bench_seq_write(buf, words, 1u, 0x5A5A5A5Au, &c);  /* warm-up */
        bench_seq_write(buf, words, reps, 0xA5A5A5A5u, &c);
        res_add(T_SEQ_WR, r->id, cfg, size, words * reps, c, 0);

        (void)bench_seq_read(buf, words, 1u, &c);           /* warm-up */
        uint32_t sum = bench_seq_read(buf, words, reps, &c);
        res_add(T_SEQ_RD, r->id, cfg, size, words * reps, c, sum);
        g_res.step++;
    }
}

static void run_flash(uint8_t cfg)
{
    static const struct { uint8_t id; uintptr_t base; } k_flash[] = {
        { R_FLASH_AXIM, 0x08000000u },
        { R_FLASH_ITCM, 0x00200000u },
    };
    const uint32_t size = 64u * 1024u, words = size / 4u, reps = 4u;

    for (uint32_t k = 0; k < 2u; k++) {
        const uint32_t *buf = (const uint32_t *)k_flash[k].base;
        uint32_t c;
        (void)bench_seq_read(buf, words, 1u, &c);
        uint32_t sum = bench_seq_read(buf, words, reps, &c);
        res_add(T_SEQ_RD, k_flash[k].id, cfg, size, words * reps, c, sum);
        g_res.step++;
    }
}

static void record_ids(int clock_rc)
{
    g_res.idcode   = DBGMCU_IDCODE;
    g_res.cpuid    = SCB_CPUID;
    g_res.clidr    = SCB_CLIDR;
    SCB_CSSELR = 0u;
    dsb();
    g_res.ccsidr_d = SCB_CCSIDR;
    SCB_CSSELR = 1u;
    dsb();
    g_res.ccsidr_i = SCB_CCSIDR;
    SCB_CSSELR = 0u;
    g_res.mpu_type = MPU_TYPE;
    g_res.sysclk_hz = (clock_rc == 0) ? SYSCLK_HZ : 16000000u;
    g_res.pllcfgr  = RCC_PLLCFGR;
    g_res.cfgr     = RCC_CFGR;
    g_res.acr      = FLASH_ACR;
    g_res.pwr_csr1 = PWR_CSR1;
    g_res.reserved[0] = (uint32_t)clock_rc;
}

int main(void)
{
    SCB_VTOR = (uint32_t)(uintptr_t)g_vectors;
    memset(&g_res, 0, sizeof g_res);
    g_res.magic = RES_MAGIC;
    g_res.version = RES_VERSION;
    g_res.status = ST_RUNNING;

    DBGMCU_CR |= 7u;                    /* debugger stays attached through WFI */
    dwt_init();
    int rc = clock_216_init();
    record_ids(rc);

    static const uint8_t k_cfgs[] = { C_OFF, C_I, C_ID_WB, C_ID_WT, C_ID_NC };
    for (uint32_t i = 0; i < sizeof k_cfgs; i++) {
        apply_cfg(k_cfgs[i]);
        for (uint32_t r = 0; r < sizeof k_regions / sizeof k_regions[0]; r++) {
            run_chase(&k_regions[r], k_cfgs[i]);
            run_seq(&k_regions[r], k_cfgs[i]);
        }
        run_flash(k_cfgs[i]);
    }
    apply_cfg(C_OFF_NOART);
    run_flash(C_OFF_NOART);
    apply_cfg(C_ID_NOART);
    run_flash(C_ID_NOART);

    apply_cfg(C_ID_WB);
    dmatest_run();

    dsb();
    g_res.status = ST_DONE;
    for (;;) __asm volatile("wfi");
}
