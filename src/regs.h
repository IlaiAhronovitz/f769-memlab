/* regs.h - the registers this project touches, and nothing else.
 *
 * Core registers: ARMv7-M Architecture Reference Manual and the Cortex-M7
 * Technical Reference Manual. Peripherals: RM0410 (STM32F76xxx/77xxx).
 */
#pragma once
#include <stdint.h>

/* A memory-mapped register: deviation DEV-MEM-01 (docs/deviations/memlab.md). */
#define REG32(a)        (*(volatile uint32_t *)(uintptr_t)(a))

/* The only inline assembly outside the timed loops: DEV-MEM-02. */
static inline void dsb(void) { __asm volatile("dsb 0xF" ::: "memory"); }
static inline void isb(void) { __asm volatile("isb 0xF" ::: "memory"); }
static inline void wfi(void) { __asm volatile("wfi"); }

/* ---- Cortex-M7 system control block -------------------------------------- */
#define SCB_CPUID       REG32(0xE000ED00U)
#define SCB_VTOR        REG32(0xE000ED08U)
#define SCB_CCR         REG32(0xE000ED14U)
#define   CCR_DC            ((uint32_t)1U << 16)   /* data cache enable        */
#define   CCR_IC            ((uint32_t)1U << 17)   /* instruction cache enable */
#define SCB_CFSR        REG32(0xE000ED28U)
#define SCB_HFSR        REG32(0xE000ED2CU)
#define SCB_BFAR        REG32(0xE000ED38U)
#define SCB_CLIDR       REG32(0xE000ED78U)
#define SCB_CCSIDR      REG32(0xE000ED80U)   /* geometry of the cache CSSELR picks */
#define SCB_CSSELR      REG32(0xE000ED84U)   /* 0 = L1 data, 1 = L1 instruction    */

/* Cache maintenance operations (write-only). */
#define SCB_ICIALLU     REG32(0xE000EF50U)   /* invalidate all I-cache            */
#define SCB_DCIMVAC     REG32(0xE000EF5CU)   /* D: invalidate line by address     */
#define SCB_DCISW       REG32(0xE000EF60U)   /* D: invalidate by set/way          */
#define SCB_DCCMVAC     REG32(0xE000EF68U)   /* D: clean line by address          */
#define SCB_DCCISW      REG32(0xE000EF74U)   /* D: clean + invalidate by set/way  */

/* ---- MPU (ARMv7-M PMSA) --------------------------------------------------- */
#define MPU_TYPE        REG32(0xE000ED90U)
#define MPU_CTRL        REG32(0xE000ED94U)
#define   MPU_CTRL_ENABLE     (1U << 0)
#define   MPU_CTRL_PRIVDEFENA (1U << 2)
#define MPU_RNR         REG32(0xE000ED98U)
#define MPU_RBAR        REG32(0xE000ED9CU)
#define MPU_RASR        REG32(0xE000EDA0U)

/* ---- Debug: DWT cycle counter, MCU debug ---------------------------------- */
#define SCB_DEMCR       REG32(0xE000EDFCU)
#define   DEMCR_TRCENA      ((uint32_t)1U << 24)
#define DWT_CTRL        REG32(0xE0001000U)
#define   DWT_CTRL_CYCCNTENA (1U << 0)
#define DWT_CYCCNT      REG32(0xE0001004U)
#define DWT_LAR         REG32(0xE0001FB0U)   /* CoreSight lock: write 0xC5ACCE55 */
#define DBGMCU_IDCODE   REG32(0xE0042000U)   /* DEV_ID 0x451 = F76x/F77x         */
#define DBGMCU_CR       REG32(0xE0042004U)

/* ---- RCC (RM0410 section 5.3) --------------------------------------------- */
#define RCC_BASE        0x40023800U
#define RCC_CR          REG32(RCC_BASE + 0x00U)
#define   RCC_CR_HSIRDY     (1U << 1)
#define   RCC_CR_PLLON      ((uint32_t)1U << 24)
#define   RCC_CR_PLLRDY     ((uint32_t)1U << 25)
#define RCC_PLLCFGR     REG32(RCC_BASE + 0x04U)
#define RCC_CFGR        REG32(RCC_BASE + 0x08U)
#define RCC_AHB1ENR     REG32(RCC_BASE + 0x30U)
#define   AHB1ENR_DMA2EN    ((uint32_t)1U << 22)
#define RCC_APB1ENR     REG32(RCC_BASE + 0x40U)
#define   APB1ENR_PWREN     ((uint32_t)1U << 28)

/* ---- PWR (RM0410 section 4.4) --------------------------------------------- */
#define PWR_BASE        0x40007000U
#define PWR_CR1         REG32(PWR_BASE + 0x00U)
#define   PWR_CR1_VOS_SCALE1 ((uint32_t)3U << 14)
#define   PWR_CR1_ODEN      ((uint32_t)1U << 16)
#define   PWR_CR1_ODSWEN    ((uint32_t)1U << 17)
#define PWR_CSR1        REG32(PWR_BASE + 0x04U)
#define   PWR_CSR1_ODRDY    ((uint32_t)1U << 16)
#define   PWR_CSR1_ODSWRDY  ((uint32_t)1U << 17)

/* ---- Flash interface (RM0410 section 3.7) --------------------------------- */
#define FLASH_ACR       REG32(0x40023C00U)
#define   ACR_LATENCY_MASK  0xFU
#define   ACR_PRFTEN        ((uint32_t)1U << 8)
#define   ACR_ARTEN         ((uint32_t)1U << 9)
#define   ACR_ARTRST        ((uint32_t)1U << 11)

/* ---- DMA2 (RM0410 section 8.5); memory-to-memory works on DMA2 only ------- */
#define DMA2_BASE       0x40026400U
#define DMA2_LISR       REG32(DMA2_BASE + 0x00U)
#define DMA2_LIFCR      REG32(DMA2_BASE + 0x08U)
#define DMA2_S0CR       REG32(DMA2_BASE + 0x10U)
#define DMA2_S0NDTR     REG32(DMA2_BASE + 0x14U)
#define DMA2_S0PAR      REG32(DMA2_BASE + 0x18U)
#define DMA2_S0M0AR     REG32(DMA2_BASE + 0x1CU)
#define DMA2_S0FCR      REG32(DMA2_BASE + 0x24U)
#define   DMA_CR_EN         (1U << 0)
#define   DMA_CR_DIR_M2M    (2U << 6)
#define   DMA_CR_PINC       ((uint32_t)1U << 9)
#define   DMA_CR_MINC       ((uint32_t)1U << 10)
#define   DMA_CR_PSIZE_W    ((uint32_t)2U << 11)
#define   DMA_CR_MSIZE_W    ((uint32_t)2U << 13)
#define   DMA_FCR_DMDIS     (1U << 2)    /* FIFO mode: required for M2M */
#define   DMA_FCR_FTH_FULL  (3U << 0)
#define   DMA_LISR_S0_ERR   ((1U << 0) | (1U << 2) | (1U << 3))  /* FE, DME, TE */
#define   DMA_LISR_TCIF0    (1U << 5)
#define   DMA_LIFCR_S0_ALL  0x3DU
