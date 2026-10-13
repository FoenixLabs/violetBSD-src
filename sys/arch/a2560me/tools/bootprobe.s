/* Standalone diagnostic, not a NetBSD kernel.
 * Execute first in the firmware's SRAM copy, then branch to the flash alias.
 * No C runtime, stack accesses, timer, UART, font or MMU mappings required.
 */
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
 jmp flash_probe+0xffc00000
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
