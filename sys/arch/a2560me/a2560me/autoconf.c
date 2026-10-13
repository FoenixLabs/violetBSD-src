/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/conf.h>
#include <sys/reboot.h>
#include <machine/intr.h>
#include <machine/board.h>
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
	uint32_t control;
	unsigned int usr_dips;

	/* Native big-endian longword; user DIP inputs are active low. */
	control = REG32(A2560ME_SYSTEM_CTRL);
	usr_dips = (~(control >> 24)) & 0x03;
	rootspec = "ld3a";
	/* DIP 1 adds single-user mode; retain all existing boot flags. */
	if (usr_dips & 0x01)
		boothowto |= RB_SINGLE;
	a2560me_boot_growroot = (usr_dips & 0x02) != 0;
	rootfstype = "ffs";
	printf("User DIP %u: root %s, filesystem %s, %s-user, expansion %s\n",
	    usr_dips, rootspec, rootfstype,
	    (boothowto & RB_SINGLE) ? "single" : "multi",
	    a2560me_boot_growroot ? "requested" : "off");
	rootconf();

}
void device_register(device_t dev, void *aux)
{
}
