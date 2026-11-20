/* STM32F769NI memory map (RM0410 section 2.2.2):
 *   ITCM RAM   0x00000000  16 KiB   instruction TCM, also readable as data
 *   flash/ITCM 0x00200000   2 MiB   same flash seen through the ITCM bus + ART
 *   flash/AXIM 0x08000000   2 MiB   same flash seen through the AXI bus
 *   DTCM RAM   0x20000000 128 KiB   never cached
 *   SRAM1      0x20020000 368 KiB   cacheable (AXI)
 *   SRAM2      0x2007C000  16 KiB   cacheable (AXI)
 *
 * Every bench buffer sits at an address aligned to its own size, so one MPU
 * region can cover it exactly (the MPU needs base % size == 0).
 * The first 256 bytes of ITCM stay empty: code at address 0 would compare
 * equal to a NULL function pointer.
 */
MEMORY
{
  FLASH   (rx)  : ORIGIN = 0x08000000, LENGTH = 2048K
  ITCM    (rwx) : ORIGIN = 0x00000100, LENGTH = 8K - 0x100
  ITCMBUF (rw)  : ORIGIN = 0x00002000, LENGTH = 8K
  RESULTS (rw)  : ORIGIN = 0x20000000, LENGTH = 8K
  DTCM    (rw)  : ORIGIN = 0x20002000, LENGTH = 24K
  DTCMBUF (rw)  : ORIGIN = 0x20008000, LENGTH = 64K
  STACK   (rw)  : ORIGIN = 0x2001C000, LENGTH = 16K
  SRAM1B  (rw)  : ORIGIN = 0x20040000, LENGTH = 128K
  DMABUF  (rw)  : ORIGIN = 0x20060000, LENGTH = 64K
  SRAM2B  (rw)  : ORIGIN = 0x2007C000, LENGTH = 16K
}

ENTRY(Reset_Handler)

_estack = ORIGIN(STACK) + LENGTH(STACK);

SECTIONS
{
  .vectors : {
    KEEP(*(.vectors))
  } > FLASH

  .text : {
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
  } > FLASH

  .ARM.exidx : { *(.ARM.exidx*) } > FLASH

  /* Hot bench loops run from ITCM: the instruction side stays the same
   * whatever the I-cache does, so only the data side is being measured. */
  _siitcm = LOADADDR(.itcm_text);
  .itcm_text : {
    . = ALIGN(8);
    _sitcm = .;
    *(.itcm_text*)
    . = ALIGN(4);
    _eitcm = .;
  } > ITCM AT> FLASH

  _sidata = LOADADDR(.data);
  .data : {
    . = ALIGN(4);
    _sdata = .;
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } > DTCM AT> FLASH

  .bss (NOLOAD) : {
    . = ALIGN(4);
    _sbss = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } > DTCM

  .results   (NOLOAD) : { KEEP(*(.results))   } > RESULTS
  .dtcm_buf  (NOLOAD) : { *(.dtcm_buf)        } > DTCMBUF
  .sram1_buf (NOLOAD) : { *(.sram1_buf)       } > SRAM1B
  .dma_buf   (NOLOAD) : { *(.dma_buf)         } > DMABUF
  .sram2_buf (NOLOAD) : { *(.sram2_buf)       } > SRAM2B
  .itcm_buf  (NOLOAD) : { *(.itcm_buf)        } > ITCMBUF
}
