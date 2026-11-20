/* dmatest.h - DMA versus D-cache coherence on SRAM1. */
#pragma once

/* Runs the stale-data case, the maintenance fix and the MPU fix, and records
 * mismatch counts and the cost of each fix. Leaves both caches on, MPU off. */
void dmatest_run(void);
