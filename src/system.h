/* system.h - clocks and the cycle counter. */
#pragma once
#include <stdint.h>

#define SYSCLK_HZ  216000000U

/* SYSCLK = 216 MHz from the PLL fed by HSI (16 MHz), with over-drive and
 * 7 flash wait states. HSI is used so the result does not depend on which
 * crystal this engineering-sample board carries. Returns 0, or a negative
 * step number if a ready flag never came. */
int  clock_216_init(void);
void dwt_init(void);
void art_enable(int on);
