/* -*- c -*- */

#include "kernel.ld.inc"

ENTRY(_start)

PHDRS {
  etext PT_LOAD;
  etextcommon PT_LOAD;
  phys PT_LOAD;
  kip PT_LOAD;
  koptions PT_LOAD;
  tramp PT_LOAD;
  ktext PT_LOAD;
  kdata PT_LOAD;
  kitext PT_LOAD;
  kidata PT_LOAD;
  l4_kip 0x10;
  l4_koptions 0x11;
}

SECTIONS {
  . = kernel_load_addr + 0x1000;
  /* 0x0 */
  .except : AT (0x0) {
     *(.except)
  } : etext = 0x0
  . = ALIGN(4K) + 8K;

  /* 0x3000 */
  PROVIDE (_kernel_image_start = .);
  .physmem : AT(ADDR(.physmem)) {
  } : phys = 0x0

  .kip : AT(ADDR(.kip)) {
    *(.kernel_info_page)
    _initkip_start = .;
    KEEP(*(.initkip.version))
    KEEP(*(.initkip.features))
    KEEP(*(.initkip.features.end))
    _initkip_end = .;
    . = ALIGN(4K);
  } :kip :l4_kip = 0

  .koptions : AT (ADDR(.koptions)) {
    *(.koptions)
  } :koptions :l4_koptions = 0

  .mp_tramp : AT (ADDR(.mp_tramp)) {
      KEEP(*(.mp_tramp))
  } :tramp

  .exceptcommon : AT(ADDR(.exceptcommon)) {
    *(.exceptcommon)
  } : etextcommon = 0x0

  .text : AT (ADDR(.text)) {
    crt0.o(.text)
    *(.init)
    *(.text SORT(.text.*) .gnu.linkonce.t.*)
    *(.fini)

    PROVIDE (_ecode = .);

    *(.rodata .rodata.* .gnu.linkonce.r.*)
    KEEP(*(.rodata.log.*))

    DEFINE_WORKLOAD_SECTIONS

    . = ALIGN(4);
    JDB_TABLE(log);
    . = ALIGN(4);
    JDB_TABLE(typeinfo);

    . = ALIGN(0x40);

    PROVIDE (_etext = .);
  } : ktext = 0x0000000

  . = ALIGN(0x10);
  .data : AT (ADDR(.data)) {
    *(.data .data.* .gnu.linkonce.d.*)
    *(.anno)

    . = ALIGN(8);
    DEFINE_INIT_ARRAYS

    PROVIDE (_edata = .);
  } : kdata

  . = ALIGN(8);
  .per_cpu_data : AT (ADDR(.per_cpu_data)) {
    PROVIDE (_per_cpu_data_start = .);
    *(.per_cpu.data)
    . = ALIGN(8);
    PROVIDE (_per_cpu_data_end = .);
  } : kdata

  . = ALIGN(0x10);
  .bss : AT (ADDR(.bss)) {
    PROVIDE (_bss_start = .);
    . = ALIGN(4);
    _per_cpu_ctor_data_start = .;
    KEEP (*(.bss.per_cpu_ctor_data))
    _per_cpu_ctor_data_end = .;

    *(.bss .bss.* .gnu.linkonce.b.* COMMON)
    PROVIDE (_bss_end = .);
  } : kdata

  . = ALIGN(4K);
  PROVIDE (_end = .);

  .initcall.text : AT (ADDR(.initcall.text)) {
  PROVIDE (_initcall_start = .);
    *(.initcall.text*)
  } : kitext = 0x000000000

  .initcall.data : AT (ADDR(.initcall.data)) {
    *(.initcall.data*)
    . = ALIGN(4K);
    PROVIDE (_initcall_end = .);
  } : kidata

  /DISCARD/ : {
    *(.note)
    *(.note.ABI-tag)
    *(.comment)
    *(.eh_frame)
    *(.dummy)
  }
}
