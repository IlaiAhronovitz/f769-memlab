/* bench_chain.h - the pointer chain that bench_chase follows. Portable: built for
 * the target and, unchanged, for the host tests (tests/test_chain.c). */
#pragma once
#include <stdint.h>

/* Largest working set chase_build accepts: 128 KiB / 32 B = 4096 lines. */
#define CHASE_MAX_BYTES (128U * 1024U)

/* Build one random cycle through every 32-byte line of buf (Sattolo's
 * algorithm), so each load of bench_chase lands on a new line in an order
 * the hardware cannot predict. Returns the chain start.
 * Precondition (not checked, see DEV-MEM-09): buf aligned to 32 bytes, bytes
 * a multiple of 32 in 32 .. CHASE_MAX_BYTES. main.c passes 1 KiB .. 128 KiB. */
void *const *chase_build(void *buf, uint32_t bytes, uint32_t seed);
