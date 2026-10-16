/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/conf.h>
#include <sys/reboot.h>
#include <machine/intr.h>
#include <machine/board.h>
#include <machine/bootparams.h>
#include "opt_a2560me.h"
#ifdef A2560ME_GUI_DEMO
void a2560me_gui_demo(void);
void machine_halt(void);
#endif
/* Read-only boot request exported to the early root-expansion rc scripts. */
int a2560me_boot_growroot;

void cpu_configure(void)
{
	intr_init();
	if (config_rootfound("mainbus", NULL) == NULL)
		panic("no mainbus");
	spl0();
}
void cpu_rootconf(void)
{
#ifdef A2560ME_GUI_DEMO
	a2560me_gui_demo();
	machine_halt();
#endif
	rootspec = "ld3a";
	rootfstype = "ffs";
	/* Retain flags explicitly selected by a maintenance kernel configuration. */
	if (a2560me_boot_option == A2560ME_BOOT_SINGLE)
		boothowto |= RB_SINGLE;
	a2560me_boot_growroot = a2560me_boot_option == A2560ME_BOOT_EXPAND;
	printf("Bootloader option %u: root %s, filesystem %s, %s-user, expansion %s\n",
	    a2560me_boot_option, rootspec, rootfstype,
	    (boothowto & RB_SINGLE) ? "single" : "multi",
	    a2560me_boot_growroot ? "requested" : "off");
	rootconf();

}
void device_register(device_t dev, void *aux)
{
}
