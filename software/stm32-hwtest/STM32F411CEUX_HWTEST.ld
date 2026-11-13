/*
 * Linker script for stm32-hwtest (STM32F411CEU6, 512 KB flash, 128 KB SRAM).
 * Derived from stm32-bringup/STM32F411CEUX_FLASH.ld (STM32CubeIDE generated,
 * Copyright (c) 2022 STMicroelectronics, provided AS-IS).
 *
 * Differences from the bringup script:
 *  - RAM_NOINIT: last 256 bytes of SRAM (0x2001FF00) hold the `.noinit`
 *    control block (DFU magic, clean-reset counter, fault breadcrumbs). It is
 *    never initialised by the startup code, survives NVIC_SystemReset, and is
 *    far away from the low ~12 KB of SRAM the ST system bootloader uses
 *    (AN2606). The stack therefore tops out at 0x2001FF00.
 *  - `.noinit_arena` (NOLOAD) in normal RAM for the big test arena.
 *  - 4 KB minimum stack.
 *  - `.crc_trailer`: one 32-bit word placed LAST in flash, after the .data
 *    load image. tools/append_crc.py patches it with the STM32-CRC of every
 *    byte in [0x08000000, _flash_crc_end). The raw .bin ends exactly at
 *    _flash_crc_end + 4.
 */

ENTRY(Reset_Handler)

_Min_Heap_Size  = 0x400;   /* newlib only; USB uses a static allocator */
_Min_Stack_Size = 0x1000;  /* 4 KB main stack */

MEMORY
{
  ROM        (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
  RAM        (xrw) : ORIGIN = 0x20000000, LENGTH = 128K - 256
  RAM_NOINIT (rw)  : ORIGIN = 0x2001FF00, LENGTH = 256
}

/* Highest address of the user mode stack (just below the noinit block) */
_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >ROM

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)

    KEEP (*(.init))
    KEEP (*(.fini))

    . = ALIGN(4);
    _etext = .;
  } >ROM

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >ROM

  .ARM.extab (READONLY) :
  {
    . = ALIGN(4);
    *(.ARM.extab* .gnu.linkonce.armextab.*)
    . = ALIGN(4);
  } >ROM

  .ARM (READONLY) :
  {
    . = ALIGN(4);
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
    . = ALIGN(4);
  } >ROM

  .preinit_array (READONLY) :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
    . = ALIGN(4);
  } >ROM

  .init_array (READONLY) :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
    . = ALIGN(4);
  } >ROM

  .fini_array (READONLY) :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
    . = ALIGN(4);
  } >ROM

  /* Used by the startup to initialize data */
  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    *(.RamFunc)
    *(.RamFunc*)
    . = ALIGN(4);
    _edata = .;
  } >RAM AT> ROM

  /* CRC trailer: MUST stay the last loadable output in ROM. `AT> ROM` above
     advanced the ROM region pointer past the .data load image, so this lands
     right after it. LONG() is immune to --gc-sections. */
  .crc_trailer :
  {
    . = ALIGN(4);
    _flash_crc_end = .;
    LONG(0xFFFFFFFF)
    _flash_image_end = .;
  } >ROM

  . = ALIGN(4);
  .bss :
  {
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >RAM

  /* Large test arena: not zeroed, not loaded. */
  .noinit_arena (NOLOAD) :
  {
    . = ALIGN(8);
    _sarena = .;
    *(.noinit_arena)
    *(.noinit_arena*)
    . = ALIGN(8);
    _earena = .;
  } >RAM

  /* Check that there is enough RAM left for heap + stack */
  ._user_heap_stack (NOLOAD) :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM

  /* Reset-surviving control block at the very top of SRAM. */
  .noinit (NOLOAD) :
  {
    . = ALIGN(4);
    KEEP(*(.noinit))
    KEEP(*(.noinit*))
    . = ALIGN(4);
  } >RAM_NOINIT

  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}

ASSERT(_flash_image_end == _flash_crc_end + 4, "crc trailer must be 4 bytes")
ASSERT(_flash_crc_end % 4 == 0, "crc trailer must be word aligned")
ASSERT(_flash_crc_end >= LOADADDR(.data) + SIZEOF(.data), "crc trailer must follow the .data load image")
