/* SPDX-License-Identifier: BSD-2-Clause */
#define _M68K_BUS_SPACE_PRIVATE
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/bus.h>
#include <sys/termios.h>
#include <dev/ic/comvar.h>
#include <machine/board.h>
static int match(device_t p, cfdata_t cf, void *aux)
{
	return 1;
}
static void attach(device_t p, device_t self, void *aux)
{
	struct com_softc *sc = device_private(self);
	sc->sc_dev = self;
	sc->sc_frequency = 1843200;
	sc->sc_type = COM_TYPE_NORMAL;
	sc->sc_poll_ticks = 1; /* fallback also services a missed interrupt */
	com_init_regs(&sc->sc_regs, &m68k_simple_bus_space, A2560ME_UART1,
		      A2560ME_UART1);
	com_attach_subr(sc);
	if (a2560me_intr_establish(0x0c, comintr, sc))
		aprint_error_dev(self, "IRQ unavailable; polling only\n");
}
CFATTACH_DECL_NEW(com_mainbus, sizeof(struct com_softc), match, attach, NULL,
		  NULL);
