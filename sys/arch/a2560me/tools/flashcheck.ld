OUTPUT_ARCH(m68k)
ENTRY(probe)
SECTIONS {
 .vectors 0 : { KEEP(*(.vectors)) }
 .text 0x400 : { *(.text) }
 /DISCARD/ : { *(.comment) *(.note*) }
 ASSERT(. <= 0x10000, "probe must fit firmware SRAM copy")
}
