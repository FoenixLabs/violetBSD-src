/* CPU addresses are distinct from raw programmer offsets. */
OUTPUT_ARCH(m68k)
ENTRY(start)
SECTIONS
{
 .reset 0xffe00000 : { __kernel_vectors = .; KEEP(*(.reset)) }
 .text 0xffe10000 : { *(.text .text.*) }
 .rodata ALIGN(4096) : { *(.rodata .rodata.*) }
 /* Keep each set named so GNU ld supplies __start/__stop symbols. */
 link_set_sysctl_funcs ALIGN(4) : { KEEP(*(link_set_sysctl_funcs)) }
 link_set_modules ALIGN(4) : { KEEP(*(link_set_modules)) }
 link_set_evcnts ALIGN(4) : { KEEP(*(link_set_evcnts)) }
 link_set_domains ALIGN(4) : { KEEP(*(link_set_domains)) }
 link_set_sysdflt_device_calls ALIGN(4) : { KEEP(*(link_set_sysdflt_device_calls)) }
 link_set_dkwedge_methods ALIGN(4) : { KEEP(*(link_set_dkwedge_methods)) }
 link_set_prop_linkpools ALIGN(4) : { KEEP(*(link_set_prop_linkpools)) }
 etext = .; _etext = .;
 __data_load = ALIGN(., 4096);
 .data 0x00010000 : AT(__data_load) {
  __data_start = .;
  *(.data .data.*) *(.sdata .sdata.*)
  . = ALIGN(4);
  edata = .; _edata = .;
 }
 .bss ALIGN(4096) (NOLOAD) : {
  __bss_start = .;
  *(.bss .bss.*) *(COMMON)
  . = ALIGN(4096);
  end = .; _end = .;
 }
 __flash_end = __data_load + SIZEOF(.data);
 /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
 ASSERT(SIZEOF(.reset) <= 0x10000, "reset page exceeds firmware copy")
 ASSERT(__flash_end <= 0xffffffff, "kernel exceeds flash")
 ASSERT(end < 0x00600000, "kernel leaves less than 2 MiB SRAM")
}
