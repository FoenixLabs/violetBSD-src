/* SPDX-License-Identifier: BSD-2-Clause */
#define _M68K_BUS_SPACE_PRIVATE
#include <sys/param.h>
#include "opt_a2560me.h"
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/evcnt.h>
#include <sys/intr.h>
#include <sys/bus.h>
#include <net/if.h>
#include <net/if_media.h>
#include <net/if_ether.h>
#include <dev/mii/miivar.h>
#include <dev/ic/lan9118reg.h>
#include <dev/ic/lan9118var.h>
#include <machine/board.h>
/* LAN source 0x20 is group 2 bit 0, CPU vector 88 in the board dispatcher. */
struct smsh_board {
	struct lan9118_softc lan;
	struct evcnt interrupts;
	struct evcnt services;
	void *softint;
};
/* LAN default word order is low half at +0. BYTE_TEST detects lane swapping. */
static bool swapbytes, swaphalves;
static bool lan_longwords;

/* Older builds have a scratch register here. Confirm IDs and a harmless
 * longword BYTE_TEST read as well, never probe a FIFO or write to detect width.
 * Require the Build 34/35 read filter and post-Build 28 FIFO timing fix.
 * Accept the 66.67 MHz identity for Build 41 and later (manufacturer Build 59 notes).
 * Retain the older 50 MHz build policy; Build 34 is outside this comparison.
 */
static unsigned lan_native_build(void)
{
#ifndef A2560ME_LAN32
	/* Explicit fallback: do not even probe LAN longword accesses. */
	return 0;
#else
	unsigned id = REG32(0xfec0000c);
	if ((id != 0x0009b05c && id != 0x0009b06c) ||
	    REG32(0xfec00010) != 0x73380011 ||
	    REG32(0xfec00014) != 0x00170826)
		return 0;
	unsigned build = REG32(0xfec0003c);
	if (id == 0x0009b06c && build < 41)
		return 0;
	if (build < 35 || build > 1000 ||
	    REG32(A2560ME_LAN + LAN9118_BYTE_TEST) != 0x21436587)
		return 0;
	return build;
#endif
}
uint32_t a2560me_lan_read(bus_addr_t a)
{
	if (a == A2560ME_LAN + LAN9118_RXDFIFOP)
		a2560me_led_network();
	if (lan_longwords)
		return __builtin_bswap32(REG32(a));
	int s = splhigh();
	uint16_t lo = REG16(a), hi = REG16(a + 2);
	splx(s);
	if (swapbytes) {
		lo = __builtin_bswap16(lo);
		hi = __builtin_bswap16(hi);
	}
	return swaphalves ? ((uint32_t)lo << 16) | hi
			  : ((uint32_t)hi << 16) | lo;
}
void a2560me_lan_write(bus_addr_t a, uint32_t v)
{
	if (a == A2560ME_LAN + LAN9118_TXDFIFOP)
		a2560me_led_network();
	if (lan_longwords) {
		REG32(a) = __builtin_bswap32(v);
		return;
	}
	uint16_t lo = v, hi = v >> 16;
	if (swaphalves) {
		uint16_t tmp = lo;
		lo = hi;
		hi = tmp;
	}
	if (swapbytes) {
		lo = __builtin_bswap16(lo);
		hi = __builtin_bswap16(hi);
	}
	int s = splhigh();
	REG16(a) = lo;
	REG16(a + 2) = hi;
	splx(s);
}
/* Payloads are byte streams, unlike numeric registers and TX commands.
 * Supported builds transfer a FIFO DWORD in memory order with one CPU cycle.
 * Keep the old paired-cycle conversions for earlier or unrecognized builds.
 */
void a2560me_lan_read_multi(bus_addr_t a, uint32_t *p, bus_size_t n)
{
	if (lan_longwords) {
		if (n != 0)
			a2560me_led_network();
		while (n--)
			*p++ = REG32(a);
	} else {
		while (n--)
			*p++ = __builtin_bswap32(a2560me_lan_read(a));
	}
}
void a2560me_lan_write_multi(bus_addr_t a, const uint32_t *p, bus_size_t n)
{
	if (lan_longwords) {
		if (n != 0)
			a2560me_led_network();
		while (n--)
			REG32(a) = *p++;
	} else {
		while (n--)
			a2560me_lan_write(a, __builtin_bswap32(*p++));
	}
}
/* Test the level input while GAVIN masks it. The LAN output stays active-low
 * open-drain; use its software interrupt to resolve GAVIN's polarity bit
 * empirically rather than depending on firmware reset state or comments.
 */
static int irq_route(void)
{
	const uint16_t bit = 1;
	int s = splhigh();
	uint16_t oldpol = REG16(0xfec0010c), oldedge = REG16(0xfec00114);
	REG16(0xfec0011c) |= bit;
	REG16(0xfec00114) &= ~bit; /* level-sensitive */
	for (unsigned pol = 0; pol < 2; pol++) {
		REG16(0xfec0010c) = (oldpol & ~bit) | pol;
		a2560me_lan_write(A2560ME_LAN + LAN9118_IRQ_CFG, 0);
		a2560me_lan_write(A2560ME_LAN + LAN9118_INT_EN, 0);
		a2560me_lan_write(A2560ME_LAN + LAN9118_INT_STS, UINT32_MAX);
		REG16(0xfec00104) = bit;
		delay(100);
		bool idle = (REG16(0xfec00104) & bit) != 0;
		a2560me_lan_write(A2560ME_LAN + LAN9118_IRQ_CFG,
		    LAN9118_IRQ_CFG_IRQ_EN);
		a2560me_lan_write(A2560ME_LAN + LAN9118_INT_EN,
		    LAN9118_INT_SW_INT);
		delay(100);
		bool asserted = (REG16(0xfec00104) & bit) != 0;
		a2560me_lan_write(A2560ME_LAN + LAN9118_INT_EN, 0);
		a2560me_lan_write(A2560ME_LAN + LAN9118_INT_STS, UINT32_MAX);
		a2560me_lan_write(A2560ME_LAN + LAN9118_IRQ_CFG, 0);
		delay(100);
		REG16(0xfec00104) = bit;
		delay(100);
		bool released = (REG16(0xfec00104) & bit) == 0;
		if (!idle && asserted && released) {
			splx(s);
			return pol;
		}
	}
	REG16(0xfec0010c) = oldpol;
	REG16(0xfec00114) = oldedge;
	REG16(0xfec00104) = bit;
	splx(s);
	return -1;
}
/* Keep packet processing off the interrupted thread's 8 KiB kernel stack.
 * Hold the level source masked until the device is drained and acknowledged.
 */
static void board_softintr(void *arg)
{
	struct smsh_board *b = arg;
	int s = splnet();
	b->services.ev_count++;
	(void)lan9118_intr(&b->lan);
	REG16(0xfec00104) = 1;
	REG16(0xfec0011c) &= ~1u;
	splx(s);
}
static int board_intr(void *arg)
{
	struct smsh_board *b = arg;
	int s = splhigh();
	REG16(0xfec0011c) |= 1;
	b->interrupts.ev_count++;
	softint_schedule(b->softint);
	splx(s);
	return 1;
}
static int mac_read(unsigned reg, uint32_t *value)
{
	for (unsigned n = 0; n < 10000; n++) {
		if (!(a2560me_lan_read(A2560ME_LAN + LAN9118_MAC_CSR_CMD) &
		      LAN9118_MAC_CSR_CMD_BUSY))
			goto issue;
		delay(10);
	}
	return ETIMEDOUT;
issue:
	a2560me_lan_write(A2560ME_LAN + LAN9118_MAC_CSR_CMD,
			  LAN9118_MAC_CSR_CMD_BUSY | LAN9118_MAC_CSR_CMD_R |
			      reg);
	for (unsigned n = 0; n < 10000; n++) {
		if (!(a2560me_lan_read(A2560ME_LAN + LAN9118_MAC_CSR_CMD) &
		      LAN9118_MAC_CSR_CMD_BUSY)) {
			*value = a2560me_lan_read(A2560ME_LAN +
						  LAN9118_MAC_CSR_DATA);
			return 0;
		}
		delay(10);
	}
	return ETIMEDOUT;
}
static int match(device_t p, cfdata_t cf, void *aux)
{
	return 1;

}
static void attach(device_t p, device_t self, void *aux)
{
	struct smsh_board *b = device_private(self);
	struct lan9118_softc *sc = &b->lan;
	uint16_t probe;
	if (bus_space_peek_2(&m68k_simple_bus_space,
			     A2560ME_LAN + LAN9118_BYTE_TEST, 0, &probe) ||
	    bus_space_peek_2(&m68k_simple_bus_space,
			     A2560ME_LAN + LAN9118_BYTE_TEST + 2, 0, &probe)) {
		aprint_error(": register window does not respond\n");
		return;
	}
	bool found = false;
	uint32_t byte_test[4];
	for (unsigned mode = 0; mode < 4; mode++) {
		swapbytes = mode & 1;
		swaphalves = mode & 2;
		byte_test[mode] = a2560me_lan_read(A2560ME_LAN +
		    LAN9118_BYTE_TEST);
		if (byte_test[mode] == LAN9118_BYTE_TEST_VALUE) {
			found = true;
			break;
		}
	}
	if (!found) {
		/* Failed attach must report the bus values, not presume wiring.
		 * Mode 0 is low-half first, then byte/half/both swaps. No reset
		 * or register writes have been issued at this point.
		 */
		aprint_error(": BYTE_TEST failed at %#x: expected %#08x, "
		    "modes 0/1/2/3 = %#08x/%#08x/%#08x/%#08x\n",
		    A2560ME_LAN + LAN9118_BYTE_TEST, LAN9118_BYTE_TEST_VALUE,
		    byte_test[0], byte_test[1], byte_test[2], byte_test[3]);
		return;
	}
	uint32_t id = a2560me_lan_read(A2560ME_LAN + LAN9118_ID_REV);
	if ((id >> 16) != 0x9221) {
		aprint_error(": unexpected ID %#x\n", id);
		return;
	}
	unsigned build = lan_native_build();
	lan_longwords = build != 0;
	if (lan_longwords)
		aprint_normal(": FPGA Build %u, native 32-bit LAN accesses\n", build);
	sc->sc_dev = self;
	sc->sc_iot = &m68k_simple_bus_space;
	sc->sc_ioh = A2560ME_LAN;
	sc->sc_flags = 0;
	a2560me_lan_write(A2560ME_LAN + LAN9118_IRQ_CFG, 0);
	uint32_t low, high;
	if (mac_read(LAN9118_ADDRL, &low) || mac_read(LAN9118_ADDRH, &high)) {
		aprint_error(": MAC CSR timeout\n");
		return;
	}
	if ((low & 1) || (low == 0 && (high & 0xffff) == 0)) {
		static const uint8_t fallback[6] = {0x02, 0xa2, 0x56, 0, 0, 1};
		memcpy(sc->sc_enaddr, fallback, sizeof(fallback));
		sc->sc_flags |= LAN9118_FLAGS_NO_EEPROM;
		aprint_normal(": no valid EEPROM MAC; using development "
			      "address 02:a2:56:00:00:01\n");
	}
	int polarity = irq_route();
	evcnt_attach_dynamic(&b->interrupts, EVCNT_TYPE_INTR, NULL,
	    device_xname(self), "interrupts");
	if (polarity < 0) {
		sc->sc_flags |= LAN9118_FLAGS_POLL;
		aprint_error_dev(self, "IRQ routing self-test failed; using polling\n");
	} else if ((b->softint = softint_establish(SOFTINT_NET,
	    board_softintr, b)) == NULL) {
		sc->sc_flags |= LAN9118_FLAGS_POLL;
		aprint_error_dev(self, "softint allocation failed; using polling\n");
	} else if (a2560me_intr_establish(A2560ME_LAN_IRQ, board_intr, b)) {
		softint_disestablish(b->softint);
		b->softint = NULL;
		sc->sc_flags |= LAN9118_FLAGS_POLL;
		REG16(0xfec0011c) |= 1;
		aprint_error_dev(self, "IRQ registration failed; using polling\n");
	}
	evcnt_attach_dynamic(&b->services, EVCNT_TYPE_MISC, NULL,
	    device_xname(self), "deferred services");
	sc->sc_flags |= LAN9118_FLAGS_RXDIAG;
	if (lan9118_attach(sc))
		return;
	aprint_normal_dev(
	    self, "%s, %s, byte-swap %d half-swap %d (16-bit probe)\n",
	    (sc->sc_flags & LAN9118_FLAGS_POLL) ? "polling" : "IRQ source 0x20 vector 88 (deferred service)",
	    lan_longwords ? "32-bit cycles, direct FIFO copy" : "paired 16-bit cycles",
	    swapbytes, swaphalves);
	if (!(sc->sc_flags & LAN9118_FLAGS_POLL))
		aprint_normal_dev(self,
		    "IRQ routing self-test passed; level input, GAVIN polarity=%d\n",
		    polarity);
}
CFATTACH_DECL_NEW(smsh_mainbus, sizeof(struct smsh_board), match, attach,
		  NULL, NULL);
