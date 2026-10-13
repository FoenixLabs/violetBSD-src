/* SPDX-License-Identifier: BSD-2-Clause */
#define _M68K_BUS_SPACE_PRIVATE
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/bus.h>
#include <sys/kmem.h>
#include <sys/callout.h>
#include <sys/kernel.h>
#include <machine/board.h>
#include <dev/ic/i8042reg.h>
#include <dev/ic/pckbcvar.h>
struct boardkbd {
	struct pckbc_softc sc;
	struct callout poll;
};
static void poll(void *arg)
{
	struct boardkbd *sc = arg;
	int s = spltty();

	pckbcintr(&sc->sc);

	/* Drain latched PS/2 requests while their board IRQs stay masked. */
	REG16(0xfec00100) = 0x0300;
	splx(s);

	callout_schedule(&sc->poll, 1);
}
static void establish(struct pckbc_softc *sc, pckbc_slot_t slot)
{
	/* Deliver input only from the periodic poller.
	 * The controller may request IRQs, but GAVIN must keep them masked.
	 */
	if (slot < PCKBC_NSLOTS) {
		int s = splhigh();
		REG16(0xfec00118) |= 1u << (8 + slot);
		splx(s);
	}
}
static int match(device_t p, cfdata_t cf, void *aux)
{
	return 1;
}
static void attach(device_t p, device_t self, void *aux)
{
	struct boardkbd *b = device_private(self);
	struct pckbc_softc *sc = &b->sc;
	sc->sc_dv = self;
	sc->intr_establish = establish;
	if (pckbc_is_console(&m68k_simple_bus_space, 0xfec02060)) {
		sc->id = &pckbc_consdata;
		pckbc_console_attached = 1;
	} else {
		sc->id = kmem_zalloc(sizeof(*sc->id), KM_SLEEP);
		sc->id->t_iot = &m68k_simple_bus_space;
		sc->id->t_ioh_d = 0xfec02060;
		sc->id->t_ioh_c = 0xfec02064;
		sc->id->t_addr = 0xfec02060;
		sc->id->t_cmdbyte = KC8_CPU;
	}
	sc->id->t_sc = sc;
	aprint_normal(
	    ": PS/2 controller (polling only; keyboard IRQs masked)\n");
	pckbc_attach(sc);
	callout_init(&b->poll, 0);
	callout_setfunc(&b->poll, poll, b);
	callout_schedule(&b->poll, 1);
}
CFATTACH_DECL_NEW(pckbc_mainbus, sizeof(struct boardkbd), match, attach, NULL,
		  NULL);
