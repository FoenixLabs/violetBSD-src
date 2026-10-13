/* Full-range flash read diagnostic, not a NetBSD kernel.
 * Execute first in the firmware's SRAM copy, then branch to the flash alias.
 * No C runtime, stack accesses, timer, UART, font or MMU mappings required.
 */
.equ FLASH_BASE, 0xffc00000
/* Read CPU-visible FLASH, never the image staged in SRAM. */
.section .vectors,"a"
.long 0x10000
.long probe
.rept 254
.long fault
.endr
.text
.global probe
probe:
 movw #0x2700,%sr
 moveq #0,%d0
 movc %d0,%cacr
 movc %d0,%tc
 movc %d0,%itt0
 movc %d0,%itt1
 movc %d0,%dtt0
 movc %d0,%dtt1
 movc %d0,%vbr
 movl #0x4643484b,0xf000
 clrl 0xf004
 movl #1,0xf004
 lea FLASH_BASE+0x400,%a0
 movl %a0,0xf010
 movl #0x46fc2700,0xf014
 movl %a0@,0xf018
 movw %a0@,0xf01c
 movb %a0@,0xf01e
 movb %a0@(1),0xf01f
 movl #2,0xf004
 lea FLASH_BASE+0x10000,%a0
 movl %a0,0xf020
 movl #0xa2560001,0xf024
 movl %a0@,0xf028
 movw %a0@,0xf02c
 movb %a0@,0xf02e
 movb %a0@(1),0xf02f
 movl #3,0xf004
 lea FLASH_BASE+0x40000,%a0
 movl %a0,0xf030
 movl #0xa2560002,0xf034
 movl %a0@,0xf038
 movw %a0@,0xf03c
 movb %a0@,0xf03e
 movb %a0@(1),0xf03f
 movl #4,0xf004
 lea FLASH_BASE+0x80000,%a0
 movl %a0,0xf040
 movl #0xa2560003,0xf044
 movl %a0@,0xf048
 movw %a0@,0xf04c
 movb %a0@,0xf04e
 movb %a0@(1),0xf04f
 movl #5,0xf004
 lea FLASH_BASE+0xc0000,%a0
 movl %a0,0xf050
 movl #0xa2560004,0xf054
 movl %a0@,0xf058
 movw %a0@,0xf05c
 movb %a0@,0xf05e
 movb %a0@(1),0xf05f
 movl #6,0xf004
 lea FLASH_BASE+0x100000,%a0
 movl %a0,0xf060
 movl #0xa2560005,0xf064
 movl %a0@,0xf068
 movw %a0@,0xf06c
 movb %a0@,0xf06e
 movb %a0@(1),0xf06f
 movl #7,0xf004
 lea FLASH_BASE+0x160000,%a0
 movl %a0,0xf070
 movl #0xa2560006,0xf074
 movl %a0@,0xf078
 movw %a0@,0xf07c
 movb %a0@,0xf07e
 movb %a0@(1),0xf07f
 movl #8,0xf004
 lea FLASH_BASE+0x180000,%a0
 movl %a0,0xf080
 movl #0xa2560007,0xf084
 movl %a0@,0xf088
 movw %a0@,0xf08c
 movb %a0@,0xf08e
 movb %a0@(1),0xf08f
 movl #9,0xf004
 lea FLASH_BASE+0x200000,%a0
 movl %a0,0xf090
 movl #0xa2560008,0xf094
 movl %a0@,0xf098
 movw %a0@,0xf09c
 movb %a0@,0xf09e
 movb %a0@(1),0xf09f
 movl #10,0xf004
 lea FLASH_BASE+0x300000,%a0
 movl %a0,0xf0a0
 movl #0xa2560009,0xf0a4
 movl %a0@,0xf0a8
 movw %a0@,0xf0ac
 movb %a0@,0xf0ae
 movb %a0@(1),0xf0af
 movl #11,0xf004
 lea FLASH_BASE+0x3fff00,%a0
 movl %a0,0xf0b0
 movl #0xa256000a,0xf0b4
 movl %a0@,0xf0b8
 movw %a0@,0xf0bc
 movb %a0@,0xf0be
 movb %a0@(1),0xf0bf
 movl #0x100,0xf004
 /* Preserve video timing; enable text/sync and a wide border. */
 movl 0xfec80000,%d0
 andl #0xfffbff7f,%d0
 orl #1,%d0
 movl %d0,0xfec80000
 clrl 0xfec80010
 movl #0x00202001,0xfec80004
 movl #0x0000ffff,0xfec80008
 movl #5000000,%d0
1: subql #1,%d0
 bne 1b
 /* All following branches are PC-relative, hence remain in flash. */
 jmp flash_probe+FLASH_BASE
flash_probe:
 movl #0x0000ff00,0xfec80008
 movl #5000000,%d0
2: subql #1,%d0
 bne 2b
 movl #0x00ffffff,0xfec80008
 movl #5000000,%d0
3: subql #1,%d0
 bne 3b
 bra flash_probe
fault:
 movw #0x2700,%sr
 movl #0x00202001,0xfec80004
 movl #0x00ff0000,0xfec80008
4: bra 4b
