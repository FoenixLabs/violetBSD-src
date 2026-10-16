/* SPDX-License-Identifier: BSD-2-Clause */
#define _M68K_BUS_SPACE_PRIVATE
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/bus.h>
#include <sys/kmem.h>
#include <sys/callout.h>
#include <sys/kernel.h>
#include <sys/evcnt.h>
#include <machine/board.h>
#include <dev/ic/i8042reg.h>
#include <dev/ic/pckbcvar.h>
/* Build 70 reference pp. 24-25, 38: LPC IRQ1/2 -> GAVIN 8/9. */
#define PS2_PENDING 0xfec00100
#define PS2_POLARITY 0xfec00108
#define PS2_EDGE 0xfec00110
#define PS2_MASK 0xfec00118
#define PS2_BITS 0x0300

struct boardkbd {
	struct pckbc_softc sc;
	struct callout poll;
	struct evcnt irq_count;
	struct evcnt recovery_count;
	unsigned established;
	bool irq_failed;
};

static int interrupt(void *arg)
{
	struct boardkbd *b = arg;
	int s = spltty();
	int served;

	/* The vectored dispatcher acknowledges the edge before calling us. */
	b->irq_count.ev_count++;
	served = pckbcintr(&b->sc);
	splx(s);
	return served;
}

static void poll(void *arg)
{
	struct boardkbd *b = arg;
	int s = spltty();

	/*
	 * Clear before draining: a byte arriving after the drain must retain
	 * its pending edge.  Both slots share the controller's output buffer.
	 */
	REG16(PS2_PENDING) = PS2_BITS;
	if (pckbcintr(&b->sc))
		b->recovery_count.ev_count++;
	splx(s);

	/* 100ms watchdog; retain one-tick service if registration failed. */
	callout_schedule(&b->poll, b->irq_failed ? 1 : MAX(1, hz / 10));
}

static void establish(struct pckbc_softc *sc, pckbc_slot_t slot)
{
	struct boardkbd *b = device_private(sc->sc_dv);
	unsigned bit;
	int s, error;

	if ((unsigned)slot >= PCKBC_NSLOTS)
		return;
	bit = 1u << (8 + slot);
	s = splhigh();
	if (b->established & bit) {
		splx(s);
		return;
	}
	REG16(PS2_MASK) |= bit;
	REG16(PS2_POLARITY) &= ~bit;
	REG16(PS2_EDGE) |= bit;
	/* Establish installs the vector, clears pending and unmasks it. */
	error = a2560me_intr_establish(8 + slot, interrupt, b);
	if (error == 0)
		b->established |= bit;
	else
		b->irq_failed = true;
	splx(s);
	if (error != 0)
		aprint_error_dev(sc->sc_dv,
		    "slot %d IRQ registration failed (%d); using polling\n",
		    slot, error);
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
	/* Keep unattached slots masked; establish enables each attached slot. */
	int s = splhigh();
	REG16(PS2_MASK) |= PS2_BITS;
	splx(s);
	evcnt_attach_dynamic(&b->irq_count, EVCNT_TYPE_INTR, NULL,
	    device_xname(self), "interrupts");
	evcnt_attach_dynamic(&b->recovery_count, EVCNT_TYPE_MISC, NULL,
	    device_xname(self), "poll recoveries");
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
	    ": PS/2 controller (IRQs 8/9, recovery polling)\n");
	pckbc_attach(sc);
	callout_init(&b->poll, 0);
	callout_setfunc(&b->poll, poll, b);
	callout_schedule(&b->poll, 1);
}
CFATTACH_DECL_NEW(pckbc_mainbus, sizeof(struct boardkbd), match, attach, NULL,
		  NULL);
