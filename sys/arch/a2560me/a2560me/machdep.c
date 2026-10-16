/* SPDX-License-Identifier: BSD-2-Clause */
#include "opt_a2560me.h"
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cpu.h>
#include <sys/reboot.h>
#include <sys/sysctl.h>
#include <uvm/uvm_extern.h>
#include <machine/board.h>
#include <machine/bootparams.h>
#include <machine/pmap.h>
#include <m68k/seglist.h>
#include <dev/cons.h>

struct pmap_bootmap machine_bootmap[] = {
 {.pmbm_vaddr = 0xfe000000,
					  .pmbm_size = 0x02000000,
					  .pmbm_flags = PMBM_F_KEEPOUT},
					 {.pmbm_vaddr = (vaddr_t)-1}};
void a2560me_early_init(void);
void machine_init(paddr_t);
unsigned a2560me_boot_option;

void a2560me_early_init(void)
{
	/* MMU/caches are still off; no bootstrap allocator has used this SRAM. */
	a2560me_boot_option = a2560me_boot_consume(
	    (volatile uint32_t *)A2560ME_BOOT_HANDOVER);
	fputype = FPU_NONE;
	physmem = A2560ME_SRAM_SIZE / PAGE_SIZE;
#if defined(A2560ME_DDR_FULL) || defined(A2560ME_DDR64)
	physmem += A2560ME_DDR_SIZE / PAGE_SIZE;
#endif

	a2560me_clock_early();

	a2560me_cninit();

#ifdef A2560ME_RAM
	a2560me_boot_puts("\nvioletBSD/a2560me: 68LC060, SRAM kernel; MMU bootstrap\n");
#else
	a2560me_boot_puts("\nvioletBSD/a2560me: 68LC060, flash XIP; MMU bootstrap\n");
#endif
}
void machine_init(paddr_t nextpa)
{

	if (nextpa >= A2560ME_SRAM_SIZE - MSGBUFSIZE)
		panic("bootstrap exceeds SRAM: %#lx", (unsigned long)nextpa);
#ifdef A2560ME_RAM
	phys_seg_list[0].ps_start = 0x100000;
	/* The loader gap is no longer in use once bootstrap switches stacks.
	 * Preserve the low 64 KiB (reset stack and vectors).
	 */
	phys_seg_list[1].ps_start = 0x10000;
	phys_seg_list[1].ps_end = 0x100000;
	phys_seg_list[1].ps_avail_start = 0x10000;
	phys_seg_list[1].ps_avail_end = 0x100000;
#else
	phys_seg_list[0].ps_start = 0x10000;
#endif
	phys_seg_list[0].ps_end = A2560ME_SRAM_SIZE;
	phys_seg_list[0].ps_avail_start = nextpa;
	phys_seg_list[0].ps_avail_end = A2560ME_SRAM_SIZE;
#if defined(A2560ME_DDR_FULL) || defined(A2560ME_DDR64)
	/* Leave segment 1 for the RAM loader gap. Bootstrap allocations and
	 * initial page tables remain in SRAM; UVM may allocate DDR thereafter.
	 * Never expose the DDR hidden below SRAM, or memory beyond the selected CPU-visible range.
	 */
	/* Keep scanout out of managed RAM, including the physical-page table.
	 * User mmap then inherits pmap's cache-inhibited device policy. */
	phys_seg_list[2].ps_start = A2560ME_DDR_BASE + A2560ME_GUI_FB_RESERVE;
	phys_seg_list[2].ps_end = A2560ME_DDR_BASE + A2560ME_DDR_SIZE;
	phys_seg_list[2].ps_avail_start = phys_seg_list[2].ps_start;
	phys_seg_list[2].ps_avail_end = phys_seg_list[2].ps_end;
#endif
	machine_init_common(nextpa);
#if defined(A2560ME_DDR_FULL) || defined(A2560ME_DDR64)
#ifdef A2560ME_DDR_FULL
	printf("Memory: 8 MiB SRAM + 1016 MiB DDR3 (CPU-visible map)\n");
#else
	printf("Memory: 8 MiB SRAM + 64 MiB DDR3 (FPGA Build 10 required)\n");
#endif
#ifdef A2560ME_DDR_NOCACHE
	printf("DDR test: CPU mappings cache-inhibited, serialized; SRAM unchanged\n");
#endif
#endif
	printf("MMU enabled; writable kernel/VM bootstrap ends at %#lx\n",
	       (unsigned long)nextpa);
	/* cpu_rootconf() applies the saved bootloader option to these defaults. */
#ifdef A2560ME_SINGLE_USER
	boothowto = RB_SINGLE;
#else
	boothowto = 0; /* Normal default; bootloader may request single-user. */
#endif
}
void consinit(void)
{
}
void cpu_startup(void)
{
	cpu_startup_common();
}
void machine_set_model(void)
{
	cpu_setmodel("Foenix A2560Me (68LC060, %u Hz CPU/bus)", a2560me_clock_hz);
}
void machine_halt(void)
{
	splhigh();
	printf("System halted. Reset the board to restart.\n");
	for (;;)
		__asm volatile("stop #0x2700");
}
void machine_reboot(int howto, char *bootstr)
{
	/* cpu_reboot_common already handles RB_NOSYNC, hooks, dumps and halt.
	 * The manufacturer loader owns the next boot selection/parameters. */
	(void)howto;
	(void)bootstr;
	splhigh();
	/* This is a mapped, uncached, 32-bit register write. Leave the MMU on
	 * until GAVIN resets the entire board, including the CPU/MMU/caches.
	 * Do not jump to reset vectors or disable translation beneath C code. */
	REG32(A2560ME_SYSTEM_CTRL) = A2560ME_SOFT_RESET;
	/* The documented cold-reset sequence takes about 1.2 seconds. The
	 * CPU normally resets before this returns; stop safely if it fails. */
	delay(2000000);
	printf("GAVIN reset failed; halting.\n");
	machine_halt();
}

int nmihand(void *arg)
{
	return 1;
}
SYSCTL_SETUP(sysctl_machdep_setup, "a2560me machdep")
{
	sysctl_createv(clog, 0, NULL, NULL, CTLFLAG_PERMANENT, CTLTYPE_NODE,
		       "machdep", NULL, NULL, 0, NULL, 0, CTL_MACHDEP, CTL_EOL);
	sysctl_createv(clog, 0, NULL, NULL, CTLFLAG_PERMANENT, CTLTYPE_STRUCT,
		       "console_device", NULL, sysctl_consdev, 0, NULL,
		       sizeof(dev_t), CTL_MACHDEP, CPU_CONSDEV, CTL_EOL);
	sysctl_createv(clog, 0, NULL, NULL, CTLFLAG_PERMANENT, CTLTYPE_INT,
	    "boot_growroot", SYSCTL_DESCR("Bootloader root expansion request"),
	    NULL, 0, &a2560me_boot_growroot, 0, CTL_MACHDEP, CTL_CREATE,
	    CTL_EOL);
}
const uint16_t ipl2psl_table[NIPL] = {
    [IPL_NONE] = PSL_S,
    [IPL_SOFTCLOCK] = PSL_S | PSL_IPL1,
    [IPL_VM] = PSL_S | PSL_IPL7,
    [IPL_SCHED] = PSL_S | PSL_IPL7,
    [IPL_HIGH] = PSL_S | PSL_IPL7,
};

void pmap_machine_check_bootstrap_allocations(paddr_t nextpa, paddr_t reloff)
{
	if (reloff != 0 || nextpa >= A2560ME_SRAM_SIZE - 0x10000) {
		printf("Bootstrap allocation exceeds SRAM: %#lx\n",
		       (unsigned long)nextpa);
		machine_halt();
	}
}
