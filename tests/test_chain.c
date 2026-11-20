/* module: src/bench_chain.c
 *
 * test_chain.c - host tests for chase_build (src/bench_chain.c).
 *
 * 1. The chain is one cycle through every line (Sattolo), for every working
 *    set main.c uses, so bench_chase really visits each line once per lap.
 * 2. The chain is the same, line for line, as the one the original
 *    bench.c code (commit ae5c633) built for the published run: the reference
 *    below is that code, kept as offsets so it runs on a 64-bit host.
 * chase_build does not check its size (DEV-MEM-09: a check would move the
 * timed flash code), so only sizes inside its precondition are tested.
 *
 * Build: cc -std=c11 -Isrc tests/test_chain.c src/bench_chain.c (tools/quality.sh
 * does it under ASan + UBSan and for coverage). Exit 0 = all passed.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "cache.h"
#include "bench_chain.h"

#define MAX_LINES  (CHASE_MAX_BYTES / CACHE_LINE)
#define LINE_SLOTS (CACHE_LINE / sizeof(void *))

static int g_failures;

static void check(int ok, const char *what, uint32_t bytes)
{
    if (!ok) {
        (void)printf("FAIL: %s (bytes=%u)\n", what, (unsigned)bytes);
        g_failures++;
    }
}

/* The original generator, verbatim in its arithmetic (bench.c, ae5c633). */
static uint32_t ref_rng(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

/* next[i] = the line the original chain goes to from line i. */
static void ref_chain(uint32_t bytes, uint32_t seed, uint16_t next[])
{
    uint32_t n = bytes / CACHE_LINE;
    uint32_t s = (seed != 0U) ? seed : 0x9E3779B9U;
    for (uint32_t i = 0U; i < n; i++) {
        next[i] = (uint16_t)i;
    }
    for (uint32_t i = n - 1U; i > 0U; i--) {
        uint32_t j = ref_rng(&s) % i;
        uint16_t t = next[i];
        next[i] = next[j];
        next[j] = t;
    }
}

/* Line index that slot of line i points to, or MAX_LINES if it points elsewhere. */
static uint32_t target_line(void *const *base, uint32_t i, uint32_t n)
{
    uintptr_t from = (uintptr_t)base;
    uintptr_t to = (uintptr_t)base[(size_t)i * LINE_SLOTS];
    if ((to < from) || (((to - from) % CACHE_LINE) != 0U) || ((to - from) / CACHE_LINE >= n)) {
        return MAX_LINES;
    }
    return (uint32_t)((to - from) / CACHE_LINE);
}

static void test_size(void *buf, uint32_t bytes, uint32_t seed)
{
    static uint16_t ref[MAX_LINES];
    static uint8_t seen[MAX_LINES];
    uint32_t n = bytes / CACHE_LINE;
    void *const *start = chase_build(buf, bytes, seed);

    check((const void *)start == buf, "returns the buffer start", bytes);
    if ((const void *)start != buf) {
        return;
    }
    ref_chain(bytes, seed, ref);
    int same = 1;
    for (uint32_t i = 0U; i < n; i++) {
        same &= (target_line(start, i, n) == ref[i]);
    }
    check(same, "same chain as the published build", bytes);

    for (uint32_t i = 0U; i < n; i++) {
        seen[i] = 0U;
    }
    uint32_t line = 0U;
    uint32_t steps = 0U;
    while ((steps < n) && (line < n) && (seen[line] == 0U)) {
        seen[line] = 1U;
        line = target_line(start, line, n);
        steps++;
    }
    check((steps == n) && (line == 0U), "one cycle through every line", bytes);
}

int main(void)
{
    /* Room for 128 KiB of 32-byte lines whatever the host pointer size. */
    void *buf = calloc(MAX_LINES * LINE_SLOTS, sizeof(void *));
    if (buf == NULL) {
        return 2;
    }
    for (uint32_t bytes = 1024U; bytes <= CHASE_MAX_BYTES; bytes *= 2U) {
        test_size(buf, bytes, 0x1234567U + bytes);       /* the seeds main.c uses */
    }
    test_size(buf, CACHE_LINE, 7U);                       /* one line: points at itself */
    test_size(buf, 3U * CACHE_LINE, 0U);                  /* seed 0: the default seed */
    free(buf);
    (void)printf("%s: test_chain\n", (g_failures == 0) ? "PASS" : "FAIL");
    return (g_failures == 0) ? 0 : 1;
}
