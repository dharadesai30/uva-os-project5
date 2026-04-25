/* copied from user.ld but we start from 0x1000 */

OUTPUT_ARCH( "aarch64" )
ENTRY( _main )


SECTIONS
{
  /* # fxl: text should start from non zero 
  # in case prog take null function pointers and believe it is invalid. cf: cpu_run()
  # this happens with gcc-9.4 (Ubuntu 20.04) which links cpu_address_immediate()
  # to address 0. */

 . = 0x1000;
 
  .text : {
    *(.text .text.*)
  }

  .rodata : {
    . = ALIGN(16);
    *(.srodata .srodata.*) /* do not need to distinguish this from .rodata */
    . = ALIGN(16);
    *(.rodata .rodata.*)
    . = ALIGN(0x1000);
  }

  .data : {
    . = ALIGN(16);
    *(.sdata .sdata.*) /* do not need to distinguish this from .data */
    . = ALIGN(16);
    *(.data .data.*)
  }

  .bss : {
    . = ALIGN(16);
    *(.sbss .sbss.*) /* do not need to distinguish this from .bss */
    . = ALIGN(16);
    *(.bss .bss.*)
  }

  PROVIDE(end = .);
}
