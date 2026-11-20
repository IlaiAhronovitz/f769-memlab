/* system.c - 216 MHz clock tree and the DWT cycle counter.
 *
 * Over-drive entry follows RM0410 section 4.1.4: PLL on, ODEN, wait ODRDY,
 * ODSWEN, wait ODSWRDY, then flash latency and bus prescalers, then switch.
 * 216 MHz at 2.7-3.6 V needs 7 flash wait states (RM0410 Table 7).
 */
#include "regs.h"
#include "system.h"

#define WAIT_POLLS 2000000U

/* Poll until (*reg & mask) == want. Returns 0, or -1 after WAIT_POLLS reads.
 * The early return is deviation DEV-MEM-06 (kept: the clock bring-up must
 * compile exactly as it did for the published run). */
static int wait_set(const volatile uint32_t *reg, uint32_t mask, uint32_t want)
{
    for (uint32_t n = 0; n < WAIT_POLLS; n++) {
        if ((*reg & mask) == want) {
            return 0;
        }
    }
    return -1;
}

/* Each early return names the step that failed: DEV-MEM-06. */
int clock_216_init(void)
{
    RCC_APB1ENR |= APB1ENR_PWREN;
    (void)RCC_APB1ENR;                          /* clock on before use */
    PWR_CR1 |= PWR_CR1_VOS_SCALE1;

    if (wait_set(&RCC_CR, RCC_CR_HSIRDY, RCC_CR_HSIRDY) != 0) {
        return -1;
    }
    if (((RCC_CFGR >> 2) & 3U) != 0U) {         /* must start on HSI */
        return -2;
    }

    RCC_CR &= ~RCC_CR_PLLON;
    if (wait_set(&RCC_CR, RCC_CR_PLLRDY, 0) != 0) {
        return -3;
    }

    /* HSI 16 MHz /M=8 -> 2 MHz, xN=216 -> VCO 432 MHz, /P=2 -> 216 MHz,
     * /Q=9 -> 48 MHz. PLLR (bits 30:28, DSI) keeps its reset value. */
    RCC_PLLCFGR = (RCC_PLLCFGR & 0xF0000000U)   /* PLLSRC (bit 22) = 0: HSI */
                | ((uint32_t)9U << 24)          /* PLLQ = 9                 */
                | ((uint32_t)216U << 6)         /* PLLN = 216               */
                | 8U;                           /* PLLM = 8; PLLP (17:16) = 0: /2 */
    RCC_CR |= RCC_CR_PLLON;

    PWR_CR1 |= PWR_CR1_ODEN;
    if (wait_set(&PWR_CSR1, PWR_CSR1_ODRDY, PWR_CSR1_ODRDY) != 0) {
        return -4;
    }
    PWR_CR1 |= PWR_CR1_ODSWEN;
    if (wait_set(&PWR_CSR1, PWR_CSR1_ODSWRDY, PWR_CSR1_ODSWRDY) != 0) {
        return -5;
    }

    FLASH_ACR = (FLASH_ACR & ~ACR_LATENCY_MASK) | 7U | ACR_PRFTEN | ACR_ARTEN;
    if ((FLASH_ACR & ACR_LATENCY_MASK) != 7U) {
        return -6;
    }

    /* AHB /1 = 216 MHz, APB1 /4 = 54 MHz (max 54), APB2 /2 = 108 MHz (max 108) */
    RCC_CFGR = (RCC_CFGR & ~((0xFU << 4) | ((uint32_t)7U << 10) | ((uint32_t)7U << 13)))
             | ((uint32_t)5U << 10) | ((uint32_t)4U << 13);

    if (wait_set(&RCC_CR, RCC_CR_PLLRDY, RCC_CR_PLLRDY) != 0) {
        return -7;
    }
    RCC_CFGR = (RCC_CFGR & ~3U) | 2U;           /* SW = PLL */
    if (wait_set(&RCC_CFGR, 3U << 2, 2U << 2) != 0) {
        return -8;
    }
    return 0;
}

void dwt_init(void)
{
    SCB_DEMCR |= DEMCR_TRCENA;
    DWT_LAR    = 0xC5ACCE55U;                   /* M7 keeps DWT behind a lock */
    DWT_CYCCNT = 0;
    DWT_CTRL  |= DWT_CTRL_CYCCNTENA;
}

/* The ART accelerator caches flash reads on the ITCM bus only. RM0410: it may
 * be reset only while disabled, so a fresh enable always starts empty. */
void art_enable(int on)
{
    if (on != 0) {
        FLASH_ACR &= ~ACR_ARTEN;
        FLASH_ACR |= ACR_ARTRST;
        FLASH_ACR &= ~ACR_ARTRST;
        FLASH_ACR |= ACR_ARTEN | ACR_PRFTEN;
    } else {
        FLASH_ACR &= ~(ACR_ARTEN | ACR_PRFTEN);
    }
    dsb();
    isb();
}
