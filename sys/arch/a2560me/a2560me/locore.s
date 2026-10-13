/* Reset image for the 68LC060. Firmware copies 64 KiB to SRAM zero. */
#include "assym.h"
#include <machine/asm.h>
#include <machine/trap.h>
#include "opt_m68k_arch.h"
#include "opt_a2560me.h"
.section .reset,"a"
.long 0x00010000
.long start
.rept 254
.long early_fault
.endr
.text
GLOBAL(kernel_text)
ASENTRY_NOPROFILE(start)
 movw #PSL_HIGHIPL,%sr
 moveq #0,%d0
 movc %d0,%cacr
 movc %d0,%tc
 movc %d0,%itt0
 movc %d0,%itt1
 movc %d0,%dtt0
 movc %d0,%dtt1
#ifdef A2560ME_RAM
 /* FoenixMgr installs only SSP/PC at zero; use our complete RAM table. */
 movl #0x00100000,%d0
#endif
 movc %d0,%vbr
 lea 0x10000,%sp
 /* Mask all GAVIN sources before touching peripherals. */
 movw #0xffff,0xfec00118
 movw #0xffff,0xfec0011a
 movw #0xffff,0xfec0011c
 lea __data_load,%a0
 lea __data_start,%a1
 lea edata,%a2
1: cmpal %a2,%a1
 jcc 2f
 movl %a0@+,%a1@+
 jra 1b
2:
 lea __bss_start,%a0
 lea end,%a1
3: cmpal %a1,%a0
 jcc 4f
 clrl %a0@+
 jra 3b
4:
 lea tmpstk,%sp
 jbsr a2560me_early_init
 clrl %sp@-
 pea end
 jbsr pmap_bootstrap1
 addql #8,%sp
 movl %d0,%d7
 /* All linked addresses already equal their physical addresses. */
#define IMMEDIATE #
#define RELOC(var, ar) movl IMMEDIATE var,ar
#include <m68k/m68k/mmu_enable.s>
Lmmuenabled:
 lea tmpstk,%sp
 jbsr vec_init
 jbsr pmap_bootstrap2
 movl %a0,%sp
 subal %a6,%a6
 movl %d7,%sp@-
 jbsr machine_init
 addql #4,%sp
 jra main
ASENTRY_NOPROFILE(early_fault)
 movw #PSL_HIGHIPL,%sr
1: stop #PSL_HIGHIPL
 jra 1b
.data
 .balign 4096
 .space 4096
ASGLOBAL(tmpstk)
GLOBAL(mmutype)
 .long MMU_68060
GLOBAL(cputype)
 .long CPU_68060
BSS(esym,4)
