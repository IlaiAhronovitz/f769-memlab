/* cache.h - Cortex-M7 L1 cache and MPU control, written out instead of CMSIS
 * so every maintenance step is visible. Line size is 32 bytes on Cortex-M7. */
#pragma once
#include <stdint.h>

#define CACHE_LINE 32U

void icache_enable(void);
void icache_disable(void);
void dcache_enable(void);
void dcache_disable(void);                       /* cleans everything first */
void dcache_clean_invalidate_all(void);

/* By address. The range must be whole cache lines: invalidating a partial
 * line would throw away the neighbour's data. */
void dcache_clean_range(const void *addr, uint32_t len);
void dcache_invalidate_range(const void *addr, uint32_t len);

/* Memory attributes for an MPU region (TEX, C, B in RASR). */
/* Write-back read/write-allocate (TEX1 C1 B1) is what the default map gives
 * SRAM, so no region is ever set to it. */
#define MPU_ATTR_WT     ((uint32_t)1U << 17)               /* TEX0 C1 B0 */
#define MPU_ATTR_NC     ((uint32_t)1U << 19)               /* TEX1 C0 B0 */

/* base must be aligned to 1 << size_log2 (32 bytes .. 4 GiB). */
void mpu_region(uint32_t region, uint32_t base, uint32_t size_log2, uint32_t attr);
void mpu_enable(void);
void mpu_disable(void);
