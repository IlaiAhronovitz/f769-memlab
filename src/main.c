/* main.c - f769-memlab: measure the Cortex-M7 memory system of the
 * STM32F769I-EVAL, configuration by configuration, into g_res (DTCM).
 *
 * Per cache configuration and per memory region:
 *   - pointer chase over 1 KiB .. region size: cycles per dependent load
 *   - sequential read and write: bytes per cycle
 *   - flash read through AXIM and through ITCM + ART
 * Then the DMA coherence test (dmatest.c). The host reads the block over SWD.
 */
#include "regs.h"
#include "libmin.h"
#include "system.h"
#include "cache.h"
#include "bench.h"
#include "bench_chain.h"
#include "dmatest.h"
#include "results.h"

extern uint32_t g_vectors[16];          /* startup.s: 16 core vectors */

#define CHASE_LOADS  16384U
#define SEQ_BYTES    (256U * 1024U)      /* bytes moved per sequential test */

typedef struct {
    uint8_t  id;
    void    *buf;      /* read as words or as a pointer chain: DEV-MEM-03 */
    uint32_t max;
} region_t;

/* The working sets: each alone in its linker section (one MPU region).
 * They and k_regions stay at file scope, in this order, on purpose
 * (deviation DEV-MEM-10, Rule 8.9): the measurement reads k_regions from
 * flash through the D-cache, and near a cache-size working set the result
 * depends on which cache set that flash line falls in. Moving the tables
 * into main() changed 10 published figures, the largest 3.97 -> 7.01 cycles
 * per load (SRAM2, 16 KiB, write-through), with byte-identical timed code
 * (board, 2026-11-20). The layout of the measured system is frozen. */
static uint8_t s_dtcm[64U * 1024U]  __attribute__((section(".dtcm_buf"),  aligned(32)));
static uint8_t s_sram1[128U * 1024U] __attribute__((section(".sram1_buf"), aligned(32)));
static uint8_t s_sram2[16U * 1024U] __attribute__((section(".sram2_buf"), aligned(32)));
static uint8_t s_itcm[8U * 1024U]   __attribute__((section(".itcm_buf"),  aligned(32)));

static const region_t k_regions[] = {
    { R_DTCM,  s_dtcm,  sizeof s_dtcm  },
    { R_SRAM1, s_sram1, sizeof s_sram1 },
    { R_SRAM2, s_sram2, sizeof s_sram2 },
    { R_ITCM,  s_itcm,  sizeof s_itcm  },
};


#define SRAM1_BENCH_BASE 0x20040000U     /* 128 KiB -> MPU size 2^17 */
#define SRAM2_BENCH_BASE 0x2007C000U     /*  16 KiB -> MPU size 2^14 */

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
        uint32_t attr = (cfg == (uint8_t)C_ID_WT) ? MPU_ATTR_WT : MPU_ATTR_NC;
        mpu_region(0, SRAM1_BENCH_BASE, 17U, attr);
        mpu_region(1, SRAM2_BENCH_BASE, 14U, attr);
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
    for (uint32_t size = 1024U; size <= r->max; size *= 2U) {
        void *buf = r->buf;
        void *const *start = chase_build(buf, size, 0x1234567U + size);
        uint32_t c;
        uint32_t warm = ((size / CACHE_LINE) + 7U) & ~7U;
        (void)bench_chase(start, warm, &c);                /* warm-up pass */
        uintptr_t end = bench_chase(start, CHASE_LOADS, &c);
        res_add(T_CHASE, r->id, cfg, size, CHASE_LOADS, c, (uint32_t)end);
        g_res.step++;
    }
}

static void run_seq(const region_t *r, uint8_t cfg)
{
    uint32_t sizes[2] = { 8U * 1024U, r->max };
    uint32_t n = (r->max > sizes[0]) ? 2U : 1U;
    if (n == 1U) {
        sizes[0] = r->max;
    }

    for (uint32_t k = 0; k < n; k++) {
        uint32_t size = sizes[k];
        uint32_t words = size / 4U;
        uint32_t reps = SEQ_BYTES / size;
        void *raw = r->buf;
        uint32_t *buf = (uint32_t *)raw;
        uint32_t c;

        bench_seq_write(buf, words, 1U, 0x5A5A5A5AU, &c);  /* warm-up */
        bench_seq_write(buf, words, reps, 0xA5A5A5A5U, &c);
        res_add(T_SEQ_WR, r->id, cfg, size, words * reps, c, 0);

        (void)bench_seq_read(buf, words, 1U, &c);           /* warm-up */
        uint32_t sum = bench_seq_read(buf, words, reps, &c);
        res_add(T_SEQ_RD, r->id, cfg, size, words * reps, c, sum);
        g_res.step++;
    }
}

static void run_flash(uint8_t cfg)
{
    static const struct { uint8_t id; uintptr_t base; } k_flash[] = {
        { R_FLASH_AXIM, 0x08000000U },
        { R_FLASH_ITCM, 0x00200000U },
    };
    const uint32_t size = 64U * 1024U;
    const uint32_t words = size / 4U;
    const uint32_t reps = 4U;

    for (uint32_t k = 0; k < 2U; k++) {
        const uint32_t *buf = (const uint32_t *)k_flash[k].base;   /* DEV-MEM-01 */
        uint32_t c;
        (void)bench_seq_read(buf, words, 1U, &c);
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
    SCB_CSSELR = 0U;
    dsb();
    g_res.ccsidr_d = SCB_CCSIDR;
    SCB_CSSELR = 1U;
    dsb();
    g_res.ccsidr_i = SCB_CCSIDR;
    SCB_CSSELR = 0U;
    g_res.mpu_type = MPU_TYPE;
    g_res.sysclk_hz = (clock_rc == 0) ? SYSCLK_HZ : 16000000U;
    g_res.pllcfgr  = RCC_PLLCFGR;
    g_res.cfgr     = RCC_CFGR;
    g_res.acr      = FLASH_ACR;
    g_res.pwr_csr1 = PWR_CSR1;
    g_res.reserved[0] = (uint32_t)clock_rc;
}

int main(void)
{
    static const uint8_t k_cfgs[] = { C_OFF, C_I, C_ID_WB, C_ID_WT, C_ID_NC };

    SCB_VTOR = (uint32_t)(uintptr_t)g_vectors;
    (void)memset(&g_res, 0, sizeof g_res);  /* NOLINT(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling): DEV-MEM-04 */
    g_res.magic = RES_MAGIC;
    g_res.version = RES_VERSION;
    g_res.status = ST_RUNNING;

    DBGMCU_CR |= 7U;                    /* debugger stays attached through WFI */
    dwt_init();
    int rc = clock_216_init();
    record_ids(rc);

    for (uint32_t i = 0; i < sizeof k_cfgs; i++) {
        apply_cfg(k_cfgs[i]);
        for (uint32_t r = 0; r < (sizeof(k_regions) / sizeof(k_regions[0])); r++) {
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
    for (;;) {
        wfi();
    }
}
