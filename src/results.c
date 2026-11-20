/* results.c - the results block and the fault hook. */
#include "regs.h"
#include "results.h"

results_t g_res __attribute__((section(".results")));

void res_add(uint16_t test, uint8_t region, uint8_t cfg, uint32_t size,
             uint32_t ops, uint32_t cycles, uint32_t extra)
{
    if (g_res.n_rows >= RES_MAX_ROWS) return;
    res_row_t *r = &g_res.rows[g_res.n_rows];
    r->test = test;
    r->region = region;
    r->cfg = cfg;
    r->size = size;
    r->ops = ops;
    r->cycles = cycles;
    r->extra = extra;
    dsb();
    g_res.n_rows++;                    /* the row is complete before it counts */
}

/* From Fault_Handler in startup.s, with the stacked exception frame. */
void fault_report(const uint32_t *frame)
{
    g_res.fault_pc   = frame[6];
    g_res.fault_cfsr = SCB_CFSR;
    g_res.fault_hfsr = SCB_HFSR;
    g_res.fault_addr = SCB_BFAR;
    dsb();
    g_res.status = ST_FAULT;
    for (;;) {}
}
