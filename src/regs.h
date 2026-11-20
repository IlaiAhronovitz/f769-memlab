/* regs.h - the registers this project touches, and nothing else.
 *
 * Core registers: ARMv7-M Architecture Reference Manual and the Cortex-M7
 * Technical Reference Manual. Peripherals: RM0410 (STM32F76xxx/77xxx).
 */
#pragma once
#include <stdint.h>

#define REG32(a)        (*(volatile uint32_t *)(uintptr_t)(a))

static inline void dsb(void) { __asm volatile("dsb 0xF" ::: "memory"); }
static inline void isb(void) { __asm volatile("isb 0xF" ::: "memory"); }

/* ---- Cortex-M7 system control block -------------------------------------- */
#define SCB_CPUID       REG32(0xE000ED00u)
#define SCB_VTOR        REG32(0xE000ED08u)
#define SCB_CCR         REG32(0xE000ED14u)
#define   CCR_DC            (1u << 16)   /* data cache enable        */
#define   CCR_IC            (1u << 17)   /* instruction cache enable */
#define SCB_CFSR        REG32(0xE000ED28u)
#define SCB_HFSR        REG32(0xE000ED2Cu)
#define SCB_MMFAR       REG32(0xE000ED34u)
#define SCB_BFAR        REG32(0xE000ED38u)
#define SCB_CLIDR       REG32(0xE000ED78u)
#define SCB_CCSIDR      REG32(0xE000ED80u)   /* geometry of the cache CSSELR picks */
#define SCB_CSSELR      REG32(0xE000ED84u)   /* 0 = L1 data, 1 = L1 instruction    */

/* Cache maintenance operations (write-only). */
#define SCB_ICIALLU     REG32(0xE000EF50u)   /* invalidate all I-cache            */
#define SCB_DCIMVAC     REG32(0xE000EF5Cu)   /* D: invalidate line by address     */
#define SCB_DCISW       REG32(0xE000EF60u)   /* D: invalidate by set/way          */
#define SCB_DCCMVAC     REG32(0xE000EF68u)   /* D: clean line by address          */
#define SCB_DCCISW      REG32(0xE000EF74u)   /* D: clean + invalidate by set/way  */
#define SCB_DCCIMVAC    REG32(0xE000EF70u)   /* D: clean + invalidate by address  */

/* ---- MPU (ARMv7-M PMSA) --------------------------------------------------- */
#define MPU_TYPE        REG32(0xE000ED90u)
#define MPU_CTRL        REG32(0xE000ED94u)
#define   MPU_CTRL_ENABLE     (1u << 0)
#define   MPU_CTRL_PRIVDEFENA (1u << 2)
#define MPU_RNR         REG32(0xE000ED98u)
#define MPU_RBAR        REG32(0xE000ED9Cu)
#define MPU_RASR        REG32(0xE000EDA0u)

/* ---- Debug: DWT cycle counter, MCU debug ---------------------------------- */
#define SCB_DEMCR       REG32(0xE000EDFCu)
#define   DEMCR_TRCENA      (1u << 24)
#define DWT_CTRL        REG32(0xE0001000u)
#define   DWT_CTRL_CYCCNTENA (1u << 0)
#define DWT_CYCCNT      REG32(0xE0001004u)
#define DWT_LAR         REG32(0xE0001FB0u)   /* CoreSight lock: write 0xC5ACCE55 */
#define DBGMCU_IDCODE   REG32(0xE0042000u)   /* DEV_ID 0x451 = F76x/F77x         */
#define DBGMCU_CR       REG32(0xE0042004u)

/* ---- RCC (RM0410 section 5.3) --------------------------------------------- */
#define RCC_BASE        0x40023800u
#define RCC_CR          REG32(RCC_BASE + 0x00u)
#define   RCC_CR_HSION      (1u << 0)
#define   RCC_CR_HSIRDY     (1u << 1)
#define   RCC_CR_PLLON      (1u << 24)
#define   RCC_CR_PLLRDY     (1u << 25)
#define RCC_PLLCFGR     REG32(RCC_BASE + 0x04u)
#define RCC_CFGR        REG32(RCC_BASE + 0x08u)
#define RCC_AHB1ENR     REG32(RCC_BASE + 0x30u)
#define   AHB1ENR_DMA2EN    (1u << 22)
#define RCC_APB1ENR     REG32(RCC_BASE + 0x40u)
#define   APB1ENR_PWREN     (1u << 28)

/* ---- PWR (RM0410 section 4.4) --------------------------------------------- */
#define PWR_BASE        0x40007000u
#define PWR_CR1         REG32(PWR_BASE + 0x00u)
#define   PWR_CR1_VOS_SCALE1 (3u << 14)
#define   PWR_CR1_ODEN      (1u << 16)
#define   PWR_CR1_ODSWEN    (1u << 17)
#define PWR_CSR1        REG32(PWR_BASE + 0x04u)
#define   PWR_CSR1_ODRDY    (1u << 16)
#define   PWR_CSR1_ODSWRDY  (1u << 17)

/* ---- Flash interface (RM0410 section 3.7) --------------------------------- */
#define FLASH_ACR       REG32(0x40023C00u)
#define   ACR_LATENCY_MASK  0xFu
#define   ACR_PRFTEN        (1u << 8)
#define   ACR_ARTEN         (1u << 9)
#define   ACR_ARTRST        (1u << 11)

/* ---- DMA2 (RM0410 section 8.5); memory-to-memory works on DMA2 only ------- */
#define DMA2_BASE       0x40026400u
#define DMA2_LISR       REG32(DMA2_BASE + 0x00u)
#define DMA2_LIFCR      REG32(DMA2_BASE + 0x08u)
#define DMA2_S0CR       REG32(DMA2_BASE + 0x10u)
#define DMA2_S0NDTR     REG32(DMA2_BASE + 0x14u)
#define DMA2_S0PAR      REG32(DMA2_BASE + 0x18u)
#define DMA2_S0M0AR     REG32(DMA2_BASE + 0x1Cu)
#define DMA2_S0FCR      REG32(DMA2_BASE + 0x24u)
#define   DMA_CR_EN         (1u << 0)
#define   DMA_CR_DIR_M2M    (2u << 6)
#define   DMA_CR_PINC       (1u << 9)
#define   DMA_CR_MINC       (1u << 10)
#define   DMA_CR_PSIZE_W    (2u << 11)
#define   DMA_CR_MSIZE_W    (2u << 13)
#define   DMA_FCR_DMDIS     (1u << 2)    /* FIFO mode: required for M2M */
#define   DMA_FCR_FTH_FULL  (3u << 0)
#define   DMA_LISR_S0_ERR   ((1u << 0) | (1u << 2) | (1u << 3))  /* FE, DME, TE */
#define   DMA_LISR_TCIF0    (1u << 5)
#define   DMA_LIFCR_S0_ALL  0x3Du
