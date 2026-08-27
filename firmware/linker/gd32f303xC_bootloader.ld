/* GD32F303CCT6 bootloader: 0x08000000..0x08003FFF (16 KiB). */
MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 16K
  RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 48K
}
ENTRY(Reset_Handler)
SECTIONS
{
  __stack_size = DEFINED(__stack_size) ? __stack_size : 2K;
  __heap_size = DEFINED(__heap_size) ? __heap_size : 1K;
  .vectors : { . = ALIGN(4); KEEP(*(.vectors)) . = ALIGN(4); } >FLASH
  .text : { . = ALIGN(4); *(.text) *(.text*) *(.glue_7) *(.glue_7t) *(.eh_frame) KEEP(*(.init)) KEEP(*(.fini)) . = ALIGN(4); _etext = .; } >FLASH
  .rodata : { . = ALIGN(4); *(.rodata) *(.rodata*) . = ALIGN(4); } >FLASH
  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
  .ARM.exidx : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } >FLASH
  .preinit_array : { PROVIDE_HIDDEN(__preinit_array_start = .); KEEP(*(.preinit_array*)) PROVIDE_HIDDEN(__preinit_array_end = .); } >FLASH
  .init_array : { PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array*)) PROVIDE_HIDDEN(__init_array_end = .); } >FLASH
  .fini_array : { PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(.fini_array*)) KEEP(*(SORT(.fini_array.*))) PROVIDE_HIDDEN(__fini_array_end = .); } >FLASH
  _sidata = LOADADDR(.data);
  .data : { . = ALIGN(4); _sdata = .; *(.data) *(.data*) . = ALIGN(4); _edata = .; } >RAM AT>FLASH
  .bss : { . = ALIGN(4); _sbss = .; __bss_start__ = _sbss; *(.bss) *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; __bss_end__ = .; } >RAM
  .heap_stack : { . = ALIGN(8); PROVIDE(end = _ebss); PROVIDE(_end = _ebss); . += __heap_size; PROVIDE(_heap_end = .); . += __stack_size; PROVIDE(_sp = .); . = ALIGN(8); } >RAM
}
ASSERT((_etext - ORIGIN(FLASH)) <= LENGTH(FLASH), "bootloader exceeds 16 KiB")
GROUP(libgcc.a libc.a libm.a libnosys.a)
