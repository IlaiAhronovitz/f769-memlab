/* startup.s - STM32F769 boot: a minimal vector table and the C runtime.
 * No interrupt is ever enabled, so only the core exceptions are listed; the
 * table stops after SysTick (VTOR needs no external vectors that never fire).
 * The CPU is built soft-float, so the FPU is never enabled.
 */
    .syntax unified
    .cpu cortex-m7
    .thumb

    .section .vectors, "a", %progbits
    .align 2
    .global g_vectors
g_vectors:
    .word _estack               /*  0: initial MSP              */
    .word Reset_Handler         /*  1: reset                    */
    .word Fault_Handler         /*  2: NMI                      */
    .word Fault_Handler         /*  3: HardFault                */
    .word Fault_Handler         /*  4: MemManage                */
    .word Fault_Handler         /*  5: BusFault                 */
    .word Fault_Handler         /*  6: UsageFault               */
    .word 0, 0, 0, 0            /*  7-10: reserved              */
    .word Fault_Handler         /* 11: SVCall                   */
    .word Fault_Handler         /* 12: DebugMon                 */
    .word 0                     /* 13: reserved                 */
    .word Fault_Handler         /* 14: PendSV                   */
    .word Fault_Handler         /* 15: SysTick                  */

    .text
    .thumb_func
    .global Reset_Handler
Reset_Handler:
    ldr   r0, =_estack          /* a debugger reset can leave a stale MSP */
    msr   msp, r0

    /* .data: flash image -> DTCM */
    ldr   r0, =_sdata
    ldr   r1, =_edata
    ldr   r2, =_sidata
1:  cmp   r0, r1
    ittt  lo
    ldrlo r3, [r2], #4
    strlo r3, [r0], #4
    blo   1b

    /* .itcm_text: flash image -> ITCM (the hot bench loops) */
    ldr   r0, =_sitcm
    ldr   r1, =_eitcm
    ldr   r2, =_siitcm
2:  cmp   r0, r1
    ittt  lo
    ldrlo r3, [r2], #4
    strlo r3, [r0], #4
    blo   2b

    /* .bss: zero */
    ldr   r0, =_sbss
    ldr   r1, =_ebss
    movs  r2, #0
3:  cmp   r0, r1
    itt   lo
    strlo r2, [r0], #4
    blo   3b

    dsb
    isb                         /* new ITCM code is visible before any call */
    bl    main
4:  b     4b

/* Any fault: record the stacked PC in the results block for the host. */
    .thumb_func
    .global Fault_Handler
Fault_Handler:
    tst   lr, #4
    ite   eq
    mrseq r0, msp
    mrsne r0, psp
    b     fault_report
