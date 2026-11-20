/* results.h - the block the host reads over SWD at 0x20000000 (DTCM).
 *
 * The target never prints: the on-board ST-LINK of this EVAL enumerates as
 * an ST-LINK/V2 (USB 0483:3748), which has no virtual COM port. Every number
 * goes into this block; tools/results.py dumps and formats it.
 * Keep the layout in step with tools/results.py (FORMAT_VERSION).
 */
#pragma once
#include <stdint.h>

#define RES_MAGIC       0x4C4D374DU     /* "M7ML" in memory order */
#define RES_VERSION     1U
#define RES_MAX_ROWS    400U

enum { ST_RUNNING = 1, ST_DONE = 2, ST_FAULT = 3 };     /* status */

enum {              /* test: what a row measured */
    T_CHASE      = 1,   /* dependent loads, random order over cache lines   */
    T_SEQ_RD     = 2,   /* sequential 32-bit loads                          */
    T_SEQ_WR     = 3,   /* sequential 32-bit stores                         */
    T_DMA_STALE  = 10,  /* DMA copy with D-cache on, no maintenance         */
    T_DMA_MAINT  = 11,  /* same, clean source + invalidate destination      */
    T_DMA_NC     = 12,  /* same, buffers non-cacheable through the MPU      */
    T_CLEAN      = 13,  /* cycles to clean the source range                 */
    T_INVAL      = 14,  /* cycles to invalidate the destination range       */
    T_DMA_COPY   = 15,  /* cycles of the DMA transfer itself                */
    T_CPU_FILL   = 16,  /* cycles for the CPU to write the source buffer    */
};

enum {              /* region: where the working set lives */
    R_DTCM = 1, R_SRAM1 = 2, R_SRAM2 = 3, R_ITCM = 4,
    R_FLASH_AXIM = 5, R_FLASH_ITCM = 6, R_DMABUF = 7,
};

enum {              /* cfg: the cache configuration */
    C_OFF       = 0,    /* I-cache off, D-cache off                         */
    C_I         = 1,    /* I-cache on,  D-cache off                         */
    C_ID_WB     = 2,    /* both on, default attributes (write-back, allocate) */
    C_ID_WT     = 3,    /* both on, SRAM bench buffers write-through (MPU)  */
    C_ID_NC     = 4,    /* both on, SRAM bench buffers non-cacheable (MPU)  */
    C_OFF_NOART = 5,    /* caches off and the flash ART accelerator off     */
    C_ID_NOART  = 6,    /* caches on and the flash ART accelerator off      */
};

typedef struct {
    uint16_t test;      /* T_*  */
    uint8_t  region;    /* R_*  */
    uint8_t  cfg;       /* C_*  */
    uint32_t size;      /* bytes in the working set */
    uint32_t ops;       /* loads, stores or words moved */
    uint32_t cycles;    /* DWT cycles */
    uint32_t extra;     /* checksum, or mismatching words for DMA tests */
} res_row_t;            /* 20 bytes */

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t status;
    uint32_t n_rows;
    uint32_t idcode;
    uint32_t cpuid;
    uint32_t clidr;
    uint32_t ccsidr_d;
    uint32_t ccsidr_i;
    uint32_t mpu_type;
    uint32_t sysclk_hz;
    uint32_t pllcfgr;
    uint32_t cfgr;
    uint32_t acr;
    uint32_t pwr_csr1;
    uint32_t fault_pc;
    uint32_t fault_cfsr;
    uint32_t fault_hfsr;
    uint32_t fault_addr;
    uint32_t step;
    uint32_t reserved[12];              /* header = 32 words = 128 bytes */
    res_row_t rows[RES_MAX_ROWS];
} results_t;

extern results_t g_res;

void res_add(uint16_t test, uint8_t region, uint8_t cfg, uint32_t size,
             uint32_t ops, uint32_t cycles, uint32_t extra);

/* Called by Fault_Handler (startup.s) with the stacked exception frame:
 * records the faulting PC and fault status, then stops. */
void fault_report(const uint32_t *frame);
