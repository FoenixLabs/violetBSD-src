/*	$NetBSD: lan9118.c,v 1.36 2020/01/29 14:53:40 thorpej Exp $	*/
/*
 * Copyright (c) 2008 KIYOHARA Takashi
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: lan9118.c,v 1.36 2020/01/29 14:53:40 thorpej Exp $");

/*
 * The LAN9118 Family
 * * The LAN9118 is targeted for 32-bit applications requiring high
 *   performance, and provides the highest level of performance possible for
 *   a non-PCI 10/100 Ethernet controller.
 *
 * * The LAN9117 is designed to provide the highest level of performance
 *   possible for 16-bit applications. It also has an external MII interface,
 *   which can be used to attach an external PHY.
 *
 * * The LAN9116 and LAN9115 are designed for performance-sensitive
 *   applications with less intensive performance requirements. The LAN9116
 *   is for 32-bit host processors, while the LAN9115 is for 16-bit
 *   applications, which may also require an external PHY. Both devices
 *   deliver superior levels of performance.
 *
 * The LAN9218 Family
 *   Also support HP Auto-MDIX.
 */

#include <sys/param.h>
#include <sys/callout.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/bus.h>
#include <sys/ioctl.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_ether.h>
#include <net/if_media.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <net/bpf.h>
#include <sys/rndsource.h>

#include <dev/ic/lan9118reg.h>
#include <dev/ic/lan9118var.h>



#ifdef SMSH_DEBUG
#define DPRINTF(x)	if (smsh_debug) printf x
#define DPRINTFN(n, x)	if (smsh_debug >= (n)) printf x
int smsh_debug = SMSH_DEBUG;
#else
#define DPRINTF(x)
#define DPRINTFN(n, x)
#endif


static void lan9118_start(struct ifnet *);
static int lan9118_ioctl(struct ifnet *, u_long, void *);
static int lan9118_init(struct ifnet *);
static void lan9118_stop(struct ifnet *, int);
static void lan9118_watchdog(struct ifnet *);

static int lan9118_ifm_change(struct ifnet *);
static void lan9118_ifm_status(struct ifnet *, struct ifmediareq *);

static int lan9118_miibus_readreg(device_t, int, int, uint16_t *);
static int lan9118_miibus_writereg(device_t, int, int, uint16_t);
static void lan9118_miibus_statchg(struct ifnet *);

static int lan9118_mii_readreg(struct lan9118_softc *, int, int, uint16_t *);
static int lan9118_mii_writereg(struct lan9118_softc *, int, int, uint16_t);
static uint32_t lan9118_mac_readreg(struct lan9118_softc *, int);
static void lan9118_mac_writereg(struct lan9118_softc *, int, uint32_t);

static void lan9118_set_filter(struct lan9118_softc *);
static void lan9118_rxintr(struct lan9118_softc *);
static void lan9118_txintr(struct lan9118_softc *);

static void lan9118_tick(void *);

/* This values refer from Linux's smc911x.c */
static uint32_t afc_cfg[] = {
	/* 0 */ 0x00000000,
	/* 1 */ 0x00000000,
	/* 2 */ 0x008c4600 | LAN9118_AFC_CFG_BACK_DUR(10) |
		LAN9118_AFC_CFG_FCMULT | LAN9118_AFC_CFG_FCBRD |
		LAN9118_AFC_CFG_FCADD | LAN9118_AFC_CFG_FCANY,
	/* 3 */ 0x00824100 | LAN9118_AFC_CFG_BACK_DUR(9) |
		LAN9118_AFC_CFG_FCMULT | LAN9118_AFC_CFG_FCBRD |
		LAN9118_AFC_CFG_FCADD | LAN9118_AFC_CFG_FCANY,
	/* 4 */ 0x00783c00 | LAN9118_AFC_CFG_BACK_DUR(9) |
		LAN9118_AFC_CFG_FCMULT | LAN9118_AFC_CFG_FCBRD |
		LAN9118_AFC_CFG_FCADD | LAN9118_AFC_CFG_FCANY,
	/* 5 */ 0x006e3700 | LAN9118_AFC_CFG_BACK_DUR(8) |
		LAN9118_AFC_CFG_FCMULT | LAN9118_AFC_CFG_FCBRD |
		LAN9118_AFC_CFG_FCADD | LAN9118_AFC_CFG_FCANY,
	/* 6 */ 0x00643200 | LAN9118_AFC_CFG_BACK_DUR(8) |
		LAN9118_AFC_CFG_FCMULT | LAN9118_AFC_CFG_FCBRD |
		LAN9118_AFC_CFG_FCADD | LAN9118_AFC_CFG_FCANY,
	/* 7 */ 0x005a2d00 | LAN9118_AFC_CFG_BACK_DUR(7) |
		LAN9118_AFC_CFG_FCMULT | LAN9118_AFC_CFG_FCBRD |
		LAN9118_AFC_CFG_FCADD | LAN9118_AFC_CFG_FCANY,
	/* 8 */ 0x00502800 | LAN9118_AFC_CFG_BACK_DUR(7) |
		LAN9118_AFC_CFG_FCMULT | LAN9118_AFC_CFG_FCBRD |
		LAN9118_AFC_CFG_FCADD | LAN9118_AFC_CFG_FCANY,
	/* 9 */ 0x00462300 | LAN9118_AFC_CFG_BACK_DUR(6) |
		LAN9118_AFC_CFG_FCMULT | LAN9118_AFC_CFG_FCBRD |
		LAN9118_AFC_CFG_FCADD | LAN9118_AFC_CFG_FCANY,
	/* a */ 0x003c1e00 | LAN9118_AFC_CFG_BACK_DUR(6) |
		LAN9118_AFC_CFG_FCMULT | LAN9118_AFC_CFG_FCBRD |
		LAN9118_AFC_CFG_FCADD | LAN9118_AFC_CFG_FCANY,
	/* b */ 0x00321900 | LAN9118_AFC_CFG_BACK_DUR(5) |
		LAN9118_AFC_CFG_FCMULT | LAN9118_AFC_CFG_FCBRD |
		LAN9118_AFC_CFG_FCADD | LAN9118_AFC_CFG_FCANY,
	/* c */ 0x00241200 | LAN9118_AFC_CFG_BACK_DUR(4) |
		LAN9118_AFC_CFG_FCMULT | LAN9118_AFC_CFG_FCBRD |
		LAN9118_AFC_CFG_FCADD | LAN9118_AFC_CFG_FCANY,
	/* d */ 0x00150700 | LAN9118_AFC_CFG_BACK_DUR(3) |
		LAN9118_AFC_CFG_FCMULT | LAN9118_AFC_CFG_FCBRD |
		LAN9118_AFC_CFG_FCADD | LAN9118_AFC_CFG_FCANY,
	/* e */ 0x00060300 | LAN9118_AFC_CFG_BACK_DUR(2) |
		LAN9118_AFC_CFG_FCMULT | LAN9118_AFC_CFG_FCBRD |
		LAN9118_AFC_CFG_FCADD | LAN9118_AFC_CFG_FCANY,
	/* f */ 0x00000000,
};

static int lan9118_wait_reg(struct lan9118_softc *sc, unsigned reg,
			    uint32_t mask, bool set)
{
	for (unsigned n = 0; n < 10000; n++) {
		uint32_t v = bus_space_read_4(sc->sc_iot, sc->sc_ioh, reg);
		if (((v & mask) != 0) == set)
			return 0;
		delay(10);
	}
	return ETIMEDOUT;
}
static int lan9118_wait_mii(struct lan9118_softc *sc)
{
	for (unsigned n = 0; n < 100; n++) {
		if (!(lan9118_mac_readreg(sc, LAN9118_MII_ACC) &
		      LAN9118_MII_ACC_MIIBZY))
			return 0;
		delay(10);
	}
	return ETIMEDOUT;
}

/* Per-device diagnostic controls; FIFO/PAUSE changes require ifconfig down.
 * Register snapshots exclude read-clear and FIFO registers. */
static int
lan9118_sysctl(SYSCTLFN_ARGS)
{
	struct lan9118_softc *sc = rnode->sysctl_data;
	struct sysctlnode node = *rnode;
	int value = 0, error = 0, s = splnet();
	uint16_t phy = 0;

	switch (rnode->sysctl_num) {
	case 1: value = sc->sc_rxquiet; break;
	case 2: value = sc->sc_txfifosz; break;
	case 3: value = sc->sc_pause; break;
	case 4: value = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
	    LAN9118_HW_CFG); break;
	case 5: value = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
	    LAN9118_AFC_CFG); break;
	case 6: value = lan9118_mac_readreg(sc, LAN9118_FLOW); break;
	case 7: case 8:
		error = lan9118_mii_readreg(sc, 1,
		    rnode->sysctl_num == 7 ? MII_ANAR : MII_ANLPAR, &phy);
		value = phy;
		break;
	default: error = EINVAL;
	}
	splx(s);
	if (error)
		return error;
	node.sysctl_data = &value;
	error = sysctl_lookup(SYSCTLFN_CALL(&node));
	if (error || newp == NULL)
		return error;
	if (rnode->sysctl_num > 3)
		return EPERM;
	/* 3 KiB minimum keeps space for the driver's 2036-byte TX budget. */
	if (rnode->sysctl_num == 2 ? (value < 3 || value > 14) :
	    (value < 0 || value > 1))
		return EINVAL;
	s = splnet();
	if (rnode->sysctl_num != 1 &&
	    (sc->sc_ec.ec_if.if_flags & (IFF_UP | IFF_RUNNING)))
		error = EBUSY;
	else if (rnode->sysctl_num == 1)
		sc->sc_rxquiet = value;
	else if (rnode->sysctl_num == 2)
		sc->sc_txfifosz = value;
	else
		sc->sc_pause = value;
	splx(s);
	return error;
}

static void
lan9118_sysctl_attach(struct lan9118_softc *sc)
{
	const struct sysctlnode *root;
	static const char * const names[] = { "rxquiet", "txfifosz", "pause",
	    "hw_cfg", "afc_cfg", "mac_flow", "phy_anar", "phy_anlpar" };
	int error;

	error = sysctl_createv(&sc->sc_sysctllog, 0, NULL, &root, 0,
	    CTLTYPE_NODE, device_xname(sc->sc_dev),
	    SYSCTL_DESCR("LAN9221 diagnostic controls and register snapshots"),
	    NULL, 0, NULL, 0, CTL_HW, CTL_CREATE, CTL_EOL);
	for (unsigned i = 0; !error && i < __arraycount(names); i++)
		error = sysctl_createv(&sc->sc_sysctllog, 0, &root, NULL,
		    i < 3 ? CTLFLAG_READWRITE : CTLFLAG_READONLY,
		    CTLTYPE_INT, names[i], NULL, lan9118_sysctl, 0, (void *)sc, 0,
		    i + 1, CTL_EOL);
	if (error) {
		sysctl_teardown(&sc->sc_sysctllog);
		aprint_error_dev(sc->sc_dev, "diagnostic sysctl setup failed: %d\n",
		    error);
	}
}

/* RX_DROP is read-to-clear. Only this serialized owner samples it, once
 * per second and before reset; vmstat reads software totals, not the chip. */
static void
lan9118_rxdiag_sample(struct lan9118_softc *sc)
{
 if (!(sc->sc_flags & LAN9118_FLAGS_RXDIAG))
  return;
 sc->sc_rxdiag[LAN_RX_DROP].ev_count +=
     bus_space_read_4(sc->sc_iot, sc->sc_ioh, LAN9118_RX_DROP);
 sc->sc_rxdiag[LAN_RX_SAMPLES].ev_count++;
 /* Satisfy the chip's 180ns RX_DROP read-after-read restriction. */
 delay(1);
}

/* Polling uses the same serialized network context as interrupt service. */
static void lan9118_poll(void *arg)
{
	struct lan9118_softc *sc = arg;
	int s = splnet();
	if (sc->sc_ec.ec_if.if_flags & IFF_RUNNING) {
		/* Drain directly: an event acknowledged at the budget limit may
		 * not reassert. */
		uint32_t pending =
		    bus_space_read_4(sc->sc_iot, sc->sc_ioh, LAN9118_INT_STS);
		if (sc->sc_flags & LAN9118_FLAGS_RXDIAG)
			lan9118_rxdiag_interrupt(sc->sc_rxdiag, pending);
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, LAN9118_INT_STS,
				  pending);
		if (pending &
		    (LAN9118_INT_RXE | LAN9118_INT_RXDF_INT | LAN9118_INT_RSFF))
			if_statinc(&sc->sc_ec.ec_if, if_ierrors);
		lan9118_txintr(sc);
		lan9118_rxintr(sc);
		if_schedule_deferred_start(&sc->sc_ec.ec_if);
		callout_schedule(&sc->sc_poll, 1);
	}
	splx(s);
}

int
lan9118_attach(struct lan9118_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ec.ec_if;
	struct mii_data *mii = &sc->sc_mii;
	uint32_t val, irq_cfg;
	int timo, i;

	if (sc->sc_flags & LAN9118_FLAGS_SWAP)
		/* byte swap mode */
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, LAN9118_WORD_SWAP,
		    0xffffffff);
	val = bus_space_read_4(sc->sc_iot, sc->sc_ioh, LAN9118_BYTE_TEST);
	if (val != LAN9118_BYTE_TEST_VALUE) {
		aprint_error(": failed to detect chip\n");
		return EINVAL;
	}

	val = bus_space_read_4(sc->sc_iot, sc->sc_ioh, LAN9118_ID_REV);
	sc->sc_id = LAN9118_ID_REV_ID(val);
	sc->sc_rev = LAN9118_ID_REV_REV(val);

#define LAN9xxx_ID(id)	\
 (IS_LAN9118(id) ? (id) : (IS_LAN9218(id) ? ((id) >> 4) + 0x100 : (id) & 0xfff))

	aprint_normal(": SMSC LAN9%03x Rev %d\n",
	    LAN9xxx_ID(sc->sc_id), sc->sc_rev);

	if (sc->sc_flags & LAN9118_FLAGS_SWAP)
		aprint_normal_dev(sc->sc_dev, "byte swap mode\n");

	timo = 3 * 1000 * 1000;	/* XXXX 3sec */
	do {
		val = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    LAN9118_MAC_CSR_CMD);
		if (!(val & LAN9118_MAC_CSR_CMD_BUSY))
			break;
		delay(100);
	} while (timo -= 100);
	if (timo <= 0)
		aprint_error_dev(sc->sc_dev, "%s: command busy\n", __func__);
	if (!(sc->sc_flags & LAN9118_FLAGS_NO_EEPROM)) {
		/* Read auto-loaded MAC address */
		val = lan9118_mac_readreg(sc, LAN9118_ADDRL);
		sc->sc_enaddr[3] = (val >> 24) & 0xff;
		sc->sc_enaddr[2] = (val >> 16) & 0xff;
		sc->sc_enaddr[1] = (val >> 8) & 0xff;
		sc->sc_enaddr[0] = val & 0xff;
		val = lan9118_mac_readreg(sc, LAN9118_ADDRH);
		sc->sc_enaddr[5] = (val >> 8) & 0xff;
		sc->sc_enaddr[4] = val & 0xff;
	}
	aprint_normal_dev(sc->sc_dev, "MAC address %s\n",
	    ether_sprintf(sc->sc_enaddr));

	/* Set IRQ config */
	irq_cfg = 0;
	if (sc->sc_flags & LAN9118_FLAGS_IRQ_ACTHI)
		irq_cfg |= LAN9118_IRQ_CFG_IRQ_POL;
	if (sc->sc_flags & LAN9118_FLAGS_IRQ_PP)
		irq_cfg |= LAN9118_IRQ_CFG_IRQ_TYPE;
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, LAN9118_IRQ_CFG, irq_cfg);

	KASSERT(LAN9118_TX_FIF_SZ >= 2 && LAN9118_TX_FIF_SZ < 15);
	sc->sc_txfifosz = LAN9118_TX_FIF_SZ;
	sc->sc_afc_cfg = afc_cfg[sc->sc_txfifosz];

	/* Initialize the ifnet structure. */
	strlcpy(ifp->if_xname, device_xname(sc->sc_dev), IFNAMSIZ);
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_start = lan9118_start;
	ifp->if_ioctl = lan9118_ioctl;
	ifp->if_init = lan9118_init;
	ifp->if_stop = lan9118_stop;
	ifp->if_watchdog = lan9118_watchdog;
	IFQ_SET_READY(&ifp->if_snd);

#if 0	/* Not support 802.1Q VLAN-sized frames yet. */
	sc->sc_ec.ec_capabilities |= ETHERCAP_VLAN_MTU;
#endif

	sc->sc_ec.ec_mii = mii;
	ifmedia_init(&mii->mii_media, 0,
	    lan9118_ifm_change, lan9118_ifm_status);
	mii->mii_ifp = ifp;
	mii->mii_readreg = lan9118_miibus_readreg;
	mii->mii_writereg = lan9118_miibus_writereg;
	mii->mii_statchg = lan9118_miibus_statchg;

	/*
	 * Number of instance of Internal PHY is always 0.  External PHY
	 * number that above.
	 */
	mii_attach(sc->sc_dev, mii, 0xffffffff, 1, MII_OFFSET_ANY, 0);

	if (sc->sc_id == LAN9118_ID_9115 || sc->sc_id == LAN9118_ID_9117 ||
	    sc->sc_id == LAN9218_ID_9215 || sc->sc_id == LAN9218_ID_9217) {
		if (bus_space_read_4(sc->sc_iot, sc->sc_ioh, LAN9118_HW_CFG) &
		    LAN9118_HW_CFG_EXT_PHY_DET) {
			/*
			 * We always have a internal PHY at phy1.
			 * In addition, external PHY is attached.
			 */
			DPRINTFN(1, ("%s: detect External PHY\n", __func__));

			/* Switch MII and SMI */
			bus_space_write_4(sc->sc_iot, sc->sc_ioh,
			    LAN9118_HW_CFG,
			    LAN9118_HW_CFG_MBO |
			    LAN9118_HW_CFG_PHY_CLK_SEL_CD);
			delay(1);	/* Wait 5 cycle */
			bus_space_write_4(sc->sc_iot, sc->sc_ioh,
			    LAN9118_HW_CFG,
			    LAN9118_HW_CFG_MBO |
			    LAN9118_HW_CFG_PHY_CLK_SEL_EMII |
			    LAN9118_HW_CFG_SMI_SEL |
			    LAN9118_HW_CFG_EXT_PHY_EN);
			delay(1);	/* Once wait more 5 cycle */

			/* Call mii_attach, avoid at phy1. */
			mii_attach(sc->sc_dev, mii, 0xffffffff,
			    0, MII_OFFSET_ANY, 0);
			for (i = 2; i < MII_NPHY; i++)
				mii_attach(sc->sc_dev, mii, 0xffffffff,
				    i, MII_OFFSET_ANY, 0);
		}
	}

	ifmedia_set(&mii->mii_media, IFM_ETHER | IFM_AUTO);

	/* Attach the interface. */
	if_attach(ifp);
	if_deferred_start_init(ifp, NULL);
	ether_ifattach(ifp, sc->sc_enaddr);

	if (sc->sc_flags & LAN9118_FLAGS_RXDIAG) {
		lan9118_sysctl_attach(sc);
		static const char * const names[LAN_RX_NCOUNTERS] = {
		 "rx chip drops", "rx drop samples", "rx chip resets",
		 "rx int RSFL", "rx int RXDF", "rx int RXE", "rx int RSFF",
		 "rx status words", "rx status ES", "rx status length",
		 "rx status runt", "rx status too long", "rx status watchdog",
		 "rx status MII", "rx status dribble", "rx status collision",
		 "rx status CRC", "rx status filter", "rx bad length",
		 "rx no mbuf", "rx no cluster", "rx queued", "rx ff timeout"
		};
		for (i = 0; i < LAN_RX_NCOUNTERS; i++)
			evcnt_attach_dynamic(&sc->sc_rxdiag[i], EVCNT_TYPE_MISC,
			    NULL, device_xname(sc->sc_dev), names[i]);
	}
	callout_init(&sc->sc_tick, 0);
	if (sc->sc_flags & LAN9118_FLAGS_POLL) {
		callout_init(&sc->sc_poll, 0);
		callout_setfunc(&sc->sc_poll, lan9118_poll, sc);
	}

	rnd_attach_source(&sc->rnd_source, device_xname(sc->sc_dev),
	    RND_TYPE_NET, RND_FLAG_DEFAULT);
	return 0;
}

int
lan9118_intr(void *arg)
{
	struct lan9118_softc *sc = (struct lan9118_softc *)arg;
	struct ifnet *ifp = &sc->sc_ec.ec_if;
	uint32_t int_sts, int_en, datum = 0;
	int handled = 0;

	for (;;) {
		int_sts =
		    bus_space_read_4(sc->sc_iot, sc->sc_ioh, LAN9118_INT_STS);
		if (sc->sc_flags & LAN9118_FLAGS_RXDIAG)
			lan9118_rxdiag_interrupt(sc->sc_rxdiag, int_sts);
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, LAN9118_INT_STS,
		    int_sts);
		int_en =
		    bus_space_read_4(sc->sc_iot, sc->sc_ioh, LAN9118_INT_EN) |
		    LAN9118_INT_SW_INT;

		DPRINTFN(3, ("%s: int_sts=0x%x, int_en=0x%x\n",
		    __func__, int_sts, int_en));

		if (!(int_sts & int_en))
			break;
		datum = int_sts;
		handled = 1;

#if 0	/* not yet... */
		if (int_sts & LAN9118_INT_PHY_INT) { /* PHY */
			/* Shall we need? */
		}
		if (int_sts & LAN9118_INT_PME_INT) { /*Power Management Event*/
			/* not yet... */
		}
#endif
		if (int_sts & LAN9118_INT_RXE) {
			if_statinc(ifp, if_ierrors);
			aprint_error_ifnet(ifp, "Receive Error\n");
		}
		if (int_sts & (LAN9118_INT_TSFL|LAN9118_INT_SW_INT)) /* TX Status FIFO Level */
			lan9118_txintr(sc);
		if (int_sts & LAN9118_INT_RXDF_INT) {
			if_statinc(ifp, if_ierrors);
			if (!sc->sc_rxquiet)
				aprint_error_ifnet(ifp, "RX Dropped Frame Interrupt\n");
		}
		if (int_sts & LAN9118_INT_RSFF) {
			if_statinc(ifp, if_ierrors);
			aprint_error_ifnet(ifp, "RX Status FIFO Full\n");
		}
		if (int_sts & LAN9118_INT_RSFL) /* RX Status FIFO Level */
			lan9118_rxintr(sc);
	}

	if (handled)
		if_schedule_deferred_start(ifp);

	rnd_add_uint32(&sc->rnd_source, datum);

	return handled;
}


static void
lan9118_start(struct ifnet *ifp)
{
	struct lan9118_softc *sc = ifp->if_softc;
	struct mbuf *m0, *m;
	unsigned tdfree, totlen, dso;
	uint32_t txa, txb, int_en;
	uint8_t *p;
	int n;

	DPRINTFN(3, ("%s\n", __func__));

	if ((ifp->if_flags & (IFF_RUNNING | IFF_OACTIVE)) != IFF_RUNNING)
		return;

	totlen = 0;
	for (unsigned budget = 0;
	     !(sc->sc_flags & LAN9118_FLAGS_POLL) || budget < 8; budget++) {
		IFQ_POLL(&ifp->if_snd, m0);
		if (m0 == NULL)
			break;

		tdfree = LAN9118_TX_FIFO_INF_TDFREE(bus_space_read_4(sc->sc_iot,
		    sc->sc_ioh, LAN9118_TX_FIFO_INF));
		if (tdfree < 2036) {
			/*
			 * 2036 is the possible maximum FIFO consumption
			 * for the most fragmented frame.
			 */
			ifp->if_flags |= IFF_OACTIVE;
			break;
		}

		IFQ_DEQUEUE(&ifp->if_snd, m0);

		/*
		 * WE ARE NOW COMMITTED TO TRANSMITTING THE PACKET.
		 */

		/*
		 * Check mbuf chain -- "middle" buffers must be >= 4 bytes
		 * and maximum # of buffers is 86.
		 */
		m = m0;
		n = 0;
		while (m) {
			if (m->m_len < 4 || ++n > 86) {
				/* Copy mbuf chain. */
				MGETHDR(m, M_DONTWAIT, MT_DATA);
				if (m == NULL)
					goto discard;	/* discard packet */
				MCLGET(m, M_DONTWAIT);
				if ((m->m_flags & M_EXT) == 0) {
					m_freem(m);
					goto discard;	/* discard packet */
				}
				m_copydata(m0, 0, m0->m_pkthdr.len,
				    mtod(m, void *));
				m->m_pkthdr.len = m->m_len = m0->m_pkthdr.len;
				m_freem(m0);
				m0 = m;
				break;
			}
			m = m->m_next;
		}

		m = m0;
		totlen = m->m_pkthdr.len;
		p = mtod(m, uint8_t *);
		dso = (unsigned)p & 0x3;
		txa =
		    LAN9118_TXC_A_BEA_4B	|
		    LAN9118_TXC_A_DSO(dso)	|
		    LAN9118_TXC_A_FS		|
		    LAN9118_TXC_A_BS(m->m_len);
		txb = LAN9118_TXC_B_PL(totlen);
		while (m->m_next != NULL) {
			bus_space_write_4(sc->sc_iot, sc->sc_ioh,
			    LAN9118_TXDFIFOP, txa);
			bus_space_write_4(sc->sc_iot, sc->sc_ioh,
			    LAN9118_TXDFIFOP, txb);
			bus_space_write_multi_4(sc->sc_iot, sc->sc_ioh,
			    LAN9118_TXDFIFOP, (uint32_t *)(p - dso),
			    (m->m_len + dso + 3) >> 2);

			m = m->m_next;
			p = mtod(m, uint8_t *);
			dso = (unsigned)p & 0x3;
			txa =
			    LAN9118_TXC_A_BEA_4B	|
			    LAN9118_TXC_A_DSO(dso)	|
			    LAN9118_TXC_A_BS(m->m_len);
		}
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, LAN9118_TXDFIFOP,
		    txa | LAN9118_TXC_A_LS);
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, LAN9118_TXDFIFOP,
		    txb);
		bus_space_write_multi_4(sc->sc_iot, sc->sc_ioh,
		    LAN9118_TXDFIFOP, (uint32_t *)(p - dso),
		    (m->m_len + dso + 3) >> 2);

discard:
		/*
		 * Pass the packet to any BPF listeners.
		 */
		bpf_mtap(ifp, m0, BPF_D_OUT);

		m_freem(m0);
}

	if (totlen > 0) {
		ifp->if_timer = 5;

		/*
		 * Trigger a software interrupt to catch any missed completion
		 * interrupts.
		 */
		int_en = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    LAN9118_INT_EN);
		int_en |= LAN9118_INT_SW_INT;
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, LAN9118_INT_EN,
		    int_en);
	}
}

static int
lan9118_ioctl(struct ifnet *ifp, u_long command, void *data)
{
	struct lan9118_softc *sc = ifp->if_softc;
	int s, error = 0;

	s = splnet();

	switch (command) {
	case SIOCSIFFLAGS:
		DPRINTFN(2, ("%s: IFFLAGS\n", __func__));
		if ((error = ifioctl_common(ifp, command, data)) != 0)
			break;
		switch (ifp->if_flags & (IFF_UP | IFF_RUNNING)) {
		case IFF_RUNNING:
			lan9118_stop(ifp, 0);
			break;
		case IFF_UP:
			lan9118_init(ifp);
			break;
		case IFF_UP | IFF_RUNNING:
			lan9118_set_filter(sc);
			break;
		default:
			break;
		}
		error = 0;
		break;

	default:
		DPRINTFN(2, ("%s: ETHER\n", __func__));
		error = ether_ioctl(ifp, command, data);
		if (error == ENETRESET) {
			if (ifp->if_flags & IFF_RUNNING) {
				lan9118_set_filter(sc);
				DPRINTFN(2, ("%s set_filter called\n",
				    __func__));
			}
			error = 0;
		}
		break;
	}

	splx(s);

	return error;
}

static int
lan9118_init(struct ifnet *ifp)
{
	struct lan9118_softc *sc = ifp->if_softc;
	struct ifmedia *ifm = &sc->sc_mii.mii_media;
	uint32_t reg, hw_cfg, mac_cr, irq_cfg;
	int timo, s;

	DPRINTFN(2, ("%s\n", __func__));

	s = splnet();

	/* wait for PMT_CTRL[READY] */
	timo = mstohz(5000);	/* XXXX 5sec */
	while (!(bus_space_read_4(sc->sc_iot, sc->sc_ioh, LAN9118_PMT_CTRL) &
	    LAN9118_PMT_CTRL_READY)) {
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, LAN9118_BYTE_TEST,
		    0xbad0c0de);
		tsleep(&sc, PRIBIO, "lan9118_pmt_ready", 1);
		if (--timo <= 0) {
			splx(s);
			return EBUSY;
		}
	}

	unsigned resettries = 10000;
	lan9118_rxdiag_sample(sc);
	if (sc->sc_flags & LAN9118_FLAGS_RXDIAG)
		sc->sc_rxdiag[LAN_RX_RESETS].ev_count++;
	/* Soft Reset */
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, LAN9118_HW_CFG,
	    LAN9118_HW_CFG_SRST);
	do {
		reg = bus_space_read_4(sc->sc_iot, sc->sc_ioh, LAN9118_HW_CFG);
		if (reg & LAN9118_HW_CFG_SRST_TO) {
			aprint_error_dev(sc->sc_dev,
			    "soft reset timeouted out\n");
			splx(s);
			return ETIMEDOUT;
		}
		delay(10);
	} while ((reg & LAN9118_HW_CFG_SRST) && --resettries);
	if (!resettries) {
		splx(s);
		return ETIMEDOUT;
	}

	/* Set MAC and PHY CSRs */

	if (sc->sc_flags & LAN9118_FLAGS_SWAP)
		/* need byte swap */
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, LAN9118_WORD_SWAP,
		    0xffffffff);

	if (lan9118_wait_reg(sc, LAN9118_E2P_CMD, LAN9118_E2P_CMD_EPCB,
			     false)) {
		splx(s);
		return ETIMEDOUT;
	}
	if ((sc->sc_flags & LAN9118_FLAGS_NO_EEPROM) ||
	    !(bus_space_read_4(sc->sc_iot, sc->sc_ioh, LAN9118_E2P_CMD) &
	      LAN9118_E2P_CMD_MACAL)) {
		lan9118_mac_writereg(sc, LAN9118_ADDRL,
		    sc->sc_enaddr[0] |
		    sc->sc_enaddr[1] << 8 |
		    sc->sc_enaddr[2] << 16 |
		    sc->sc_enaddr[3] << 24);
		lan9118_mac_writereg(sc, LAN9118_ADDRH,
		    sc->sc_enaddr[4] | sc->sc_enaddr[5] << 8);
	}

	if (!(sc->sc_flags & LAN9118_FLAGS_RXDIAG) &&
	    (ifm->ifm_media & IFM_FLOW)) {
		lan9118_mac_writereg(sc, LAN9118_FLOW,
		    LAN9118_FLOW_FCPT(1) | LAN9118_FLOW_FCEN);
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, LAN9118_AFC_CFG,
		    sc->sc_afc_cfg);
	}

	sc->sc_afc_cfg = afc_cfg[sc->sc_txfifosz];
	if (sc->sc_flags & LAN9118_FLAGS_RXDIAG) {
		struct mii_softc *phy;
		mii_lock(&sc->sc_mii);
		LIST_FOREACH(phy, &sc->sc_mii.mii_phys, mii_list) {
			if (sc->sc_pause)
				phy->mii_flags |= MIIF_DOPAUSE;
			else
				phy->mii_flags &= ~MIIF_DOPAUSE;
		}
		mii_unlock(&sc->sc_mii);
	}
	int media_error = lan9118_ifm_change(ifp);
	if (media_error) {
		splx(s);
		return media_error;
	}
	hw_cfg = bus_space_read_4(sc->sc_iot, sc->sc_ioh, LAN9118_HW_CFG);
	hw_cfg &= ~LAN9118_HW_CFG_TX_FIF_MASK;
	hw_cfg |= LAN9118_HW_CFG_TX_FIF_SZ(sc->sc_txfifosz);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, LAN9118_HW_CFG, hw_cfg);

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, LAN9118_GPIO_CFG,
	    LAN9118_GPIO_CFG_LEDX_EN(2)	 |
	    LAN9118_GPIO_CFG_LEDX_EN(1)	 |
	    LAN9118_GPIO_CFG_LEDX_EN(0)	 |
	    LAN9118_GPIO_CFG_GPIOBUFN(2) |
	    LAN9118_GPIO_CFG_GPIOBUFN(1) |
	    LAN9118_GPIO_CFG_GPIOBUFN(0));

	irq_cfg = bus_space_read_4(sc->sc_iot, sc->sc_ioh, LAN9118_IRQ_CFG);
	if (sc->sc_flags & LAN9118_FLAGS_POLL)
		irq_cfg &= ~LAN9118_IRQ_CFG_IRQ_EN;
	else
		irq_cfg |= LAN9118_IRQ_CFG_IRQ_EN;
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, LAN9118_IRQ_CFG, irq_cfg);

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, LAN9118_INT_STS,
	    bus_space_read_4(sc->sc_iot, sc->sc_ioh, LAN9118_INT_STS));
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, LAN9118_FIFO_INT,
	    LAN9118_FIFO_INT_TXSL(0) | LAN9118_FIFO_INT_RXSL(0));
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, LAN9118_INT_EN,
#if 0	/* not yet... */
	    LAN9118_INT_PHY_INT | /* PHY */
	    LAN9118_INT_PME_INT | /* Power Management Event */
#endif
	    LAN9118_INT_RXE	| /* Receive Error */
	    LAN9118_INT_TSFL	| /* TX Status FIFO Level */
	    LAN9118_INT_RXDF_INT| /* RX Dropped Frame Interrupt */
	    LAN9118_INT_RSFF	| /* RX Status FIFO Full */
	    LAN9118_INT_RSFL);	  /* RX Status FIFO Level */

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, LAN9118_RX_CFG,
	    LAN9118_RX_CFG_RXDOFF(2));
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, LAN9118_TX_CFG,
	    LAN9118_TX_CFG_TX_ON);
	mac_cr = lan9118_mac_readreg(sc, LAN9118_MAC_CR);
	lan9118_mac_writereg(sc, LAN9118_MAC_CR,
	    mac_cr | LAN9118_MAC_CR_TXEN | LAN9118_MAC_CR_RXEN);

	lan9118_set_filter(sc);

	ifp->if_flags |= IFF_RUNNING;
	ifp->if_flags &= ~IFF_OACTIVE;

	callout_reset(&sc->sc_tick, hz, lan9118_tick, sc);
	if (sc->sc_flags & LAN9118_FLAGS_POLL)
		callout_schedule(&sc->sc_poll, 1);

	splx(s);

	return 0;
}

static void
lan9118_stop(struct ifnet *ifp, int disable)
{
	struct lan9118_softc *sc = ifp->if_softc;
	uint32_t cr;

	DPRINTFN(2, ("%s\n", __func__));

	if (sc->sc_flags & LAN9118_FLAGS_POLL)
		callout_stop(&sc->sc_poll);
	ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);
	/* Disable IRQ */
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, LAN9118_INT_EN, 0);

	/* Stopping transmitter */
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, LAN9118_TX_CFG,
	    LAN9118_TX_CFG_STOP_TX);
	(void)lan9118_wait_reg(sc, LAN9118_TX_CFG,
			       LAN9118_TX_CFG_TX_ON | LAN9118_TX_CFG_STOP_TX,
			       false);

	/* Purge TX Status/Data FIFOs */
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, LAN9118_TX_CFG,
	    LAN9118_TX_CFG_TXS_DUMP | LAN9118_TX_CFG_TXD_DUMP);

	/* Stopping receiver, also clear TXEN */
	cr = lan9118_mac_readreg(sc, LAN9118_MAC_CR);
	cr &= ~(LAN9118_MAC_CR_TXEN | LAN9118_MAC_CR_RXEN);
	lan9118_mac_writereg(sc, LAN9118_MAC_CR, cr);
	(void)lan9118_wait_reg(sc, LAN9118_INT_STS, LAN9118_INT_RXSTOP_INT,
			       true);

	/* Clear RX Status/Data FIFOs */
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, LAN9118_RX_CFG,
	    LAN9118_RX_CFG_RX_DUMP);

	callout_stop(&sc->sc_tick);
}

static void
lan9118_watchdog(struct ifnet *ifp)
{
	struct lan9118_softc *sc = ifp->if_softc;

	/*
	 * Reclaim first as there is a possibility of losing Tx completion
	 * interrupts.
	 */
	lan9118_txintr(sc);

	aprint_error_ifnet(ifp, "watchdog timeout\n");
	if_statinc(ifp, if_oerrors);

	lan9118_init(ifp);
}


static int
lan9118_ifm_change(struct ifnet *ifp)
{
	struct lan9118_softc *sc = ifp->if_softc;
	struct mii_data *mii = &sc->sc_mii;
	struct ifmedia *ifm = &mii->mii_media;
	struct ifmedia_entry *ife = ifm->ifm_cur;
	uint32_t pmt_ctrl;

	DPRINTFN(3, ("%s: ifm inst %d\n", __func__, IFM_INST(ife->ifm_media)));

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, LAN9118_HW_CFG,
	    LAN9118_HW_CFG_MBO | LAN9118_HW_CFG_PHY_CLK_SEL_CD);
	delay(1);	/* Wait 5 cycle */

	if (IFM_INST(ife->ifm_media) != 0) {
		/* Use External PHY */

		bus_space_write_4(sc->sc_iot, sc->sc_ioh, LAN9118_HW_CFG,
		    LAN9118_HW_CFG_MBO			|
		    LAN9118_HW_CFG_PHY_CLK_SEL_EMII	|
		    LAN9118_HW_CFG_SMI_SEL		|
		    LAN9118_HW_CFG_EXT_PHY_EN);
		delay(1);
		return mii_mediachg(&sc->sc_mii);
	}

	/* Setup Internal PHY */

	mii->mii_media_status = IFM_AVALID;
	mii->mii_media_active = IFM_ETHER;

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, LAN9118_HW_CFG,
	    LAN9118_HW_CFG_MBO |
	    LAN9118_HW_CFG_PHY_CLK_SEL_IPHY);
	delay(1);

	/* Reset PHY */
	pmt_ctrl = bus_space_read_4(sc->sc_iot, sc->sc_ioh, LAN9118_PMT_CTRL);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, LAN9118_PMT_CTRL,
	    pmt_ctrl | LAN9118_PMT_CTRL_PHY_RST);
	if (lan9118_wait_reg(sc, LAN9118_PMT_CTRL, LAN9118_PMT_CTRL_PHY_RST,
			     false))
		return ETIMEDOUT;

	mii_mediachg(&sc->sc_mii);
	return 0;
}

static void
lan9118_ifm_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct lan9118_softc *sc = ifp->if_softc;
	struct mii_data *mii = &sc->sc_mii;

	DPRINTFN(3, ("%s\n", __func__));

	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
}


static int
lan9118_miibus_readreg(device_t dev, int phy, int reg, uint16_t *val)
{

	return lan9118_mii_readreg(device_private(dev), phy, reg, val);
}
static int
lan9118_miibus_writereg(device_t dev, int phy, int reg, uint16_t val)
{

	return lan9118_mii_writereg(device_private(dev), phy, reg, val);
}

static void
lan9118_miibus_statchg(struct ifnet *ifp)
{
	struct lan9118_softc *sc = ifp->if_softc;
	u_int cr;

	cr = lan9118_mac_readreg(sc, LAN9118_MAC_CR);
	if (IFM_OPTIONS(sc->sc_mii.mii_media_active) & IFM_FDX) {
		cr &= ~LAN9118_MAC_CR_RCVOWN;
		cr |= LAN9118_MAC_CR_FDPX;
	} else {
		cr |= LAN9118_MAC_CR_RCVOWN;
		cr &= ~LAN9118_MAC_CR_FDPX;
	}
	lan9118_mac_writereg(sc, LAN9118_MAC_CR, cr);
	if (sc->sc_flags & LAN9118_FLAGS_RXDIAG) {
		const int active = sc->sc_mii.mii_media_active;
		uint32_t flow = 0, afc = 0;
		if (sc->sc_pause && (active & IFM_FDX) &&
		    (sc->sc_mii.mii_media_status & IFM_ACTIVE)) {
			/* FCEN receives PAUSE; FCANY enables automatic transmission. */
			flow = LAN9118_FLOW_FCPT(0xffffU);
			if (active & IFM_ETH_RXPAUSE)
				flow |= LAN9118_FLOW_FCEN;
			if (active & IFM_ETH_TXPAUSE)
				afc = (sc->sc_afc_cfg & ~0xfU) | LAN9118_AFC_CFG_FCANY;
		}
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, LAN9118_AFC_CFG, 0);
		unsigned tries;
		for (tries = 0; tries < 10000; tries++) {
			if (!(lan9118_mac_readreg(sc, LAN9118_FLOW) & LAN9118_FLOW_FCBUSY))
				break;
			delay(1);
		}
		if (tries == 10000) {
			aprint_error_dev(sc->sc_dev, "flow control busy timeout\n");
			return;
		}
		lan9118_mac_writereg(sc, LAN9118_FLOW, flow);
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, LAN9118_AFC_CFG, afc);
	}
}


static int
lan9118_mii_readreg(struct lan9118_softc *sc, int phy, int reg, uint16_t *val)
{
	uint32_t acc;

	if (lan9118_wait_mii(sc))
		return ETIMEDOUT;
	acc = LAN9118_MII_ACC_PHYA(phy) | LAN9118_MII_ACC_MIIRINDA(reg);
	lan9118_mac_writereg(sc, LAN9118_MII_ACC, acc);
	if (lan9118_wait_mii(sc))
		return ETIMEDOUT;
	*val = lan9118_mac_readreg(sc, LAN9118_MII_DATA) & 0xffff;
	return 0;
}

static int
lan9118_mii_writereg(struct lan9118_softc *sc, int phy, int reg, uint16_t val)
{
	uint32_t acc;

	if (lan9118_wait_mii(sc))
		return ETIMEDOUT;
	acc = LAN9118_MII_ACC_PHYA(phy) | LAN9118_MII_ACC_MIIRINDA(reg) |
	    LAN9118_MII_ACC_MIIWNR;
	lan9118_mac_writereg(sc, LAN9118_MII_DATA, val);
	lan9118_mac_writereg(sc, LAN9118_MII_ACC, acc);
	if (lan9118_wait_mii(sc))
		return ETIMEDOUT;

	return 0;
}

static uint32_t
lan9118_mac_readreg(struct lan9118_softc *sc, int reg)
{
	uint32_t cmd;
	int timo = 3 * 1000 * 1000;	/* XXXX: 3sec */

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, LAN9118_MAC_CSR_CMD,
	    LAN9118_MAC_CSR_CMD_BUSY | LAN9118_MAC_CSR_CMD_R | reg);
	do {
		cmd = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    LAN9118_MAC_CSR_CMD);
		if (!(cmd & LAN9118_MAC_CSR_CMD_BUSY))
			break;
		delay(100);
	} while (timo -= 100);
	if (timo <= 0)
		aprint_error_dev(sc->sc_dev, "%s: command busy\n", __func__);
	return bus_space_read_4(sc->sc_iot, sc->sc_ioh, LAN9118_MAC_CSR_DATA);
}

static void
lan9118_mac_writereg(struct lan9118_softc *sc, int reg, uint32_t val)
{
	uint32_t cmd;
	int timo = 3 * 1000 * 1000;	/* XXXX: 3sec */

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, LAN9118_MAC_CSR_DATA, val);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, LAN9118_MAC_CSR_CMD,
	    LAN9118_MAC_CSR_CMD_BUSY | LAN9118_MAC_CSR_CMD_W | reg);
	do {
		cmd = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    LAN9118_MAC_CSR_CMD);
		if (!(cmd & LAN9118_MAC_CSR_CMD_BUSY))
			break;
		delay(100);
	} while (timo -= 100);
	if (timo <= 0)
		aprint_error_dev(sc->sc_dev, "%s: command busy\n", __func__);
}


static void
lan9118_set_filter(struct lan9118_softc *sc)
{
	struct ethercom *ec = &sc->sc_ec;
	struct ether_multistep step;
	struct ether_multi *enm;
	struct ifnet *ifp = &ec->ec_if;
	uint32_t mac_cr, h, hashes[2] = { 0, 0 };

	mac_cr = lan9118_mac_readreg(sc, LAN9118_MAC_CR);
	if (ifp->if_flags & IFF_PROMISC) {
		lan9118_mac_writereg(sc, LAN9118_MAC_CR,
		    mac_cr | LAN9118_MAC_CR_PRMS);
		return;
	}

	mac_cr &= ~(LAN9118_MAC_CR_PRMS | LAN9118_MAC_CR_MCPAS |
	    LAN9118_MAC_CR_BCAST | LAN9118_MAC_CR_HPFILT);
	if (!(ifp->if_flags & IFF_BROADCAST))
		mac_cr |= LAN9118_MAC_CR_BCAST;

	if (ifp->if_flags & IFF_ALLMULTI)
		mac_cr |= LAN9118_MAC_CR_MCPAS;
	else {
		ETHER_LOCK(ec);
		ETHER_FIRST_MULTI(step, ec, enm);
		while (enm != NULL) {
			if (memcmp(enm->enm_addrlo, enm->enm_addrhi,
			    ETHER_ADDR_LEN) != 0) {
				/*
				 * We must listen to a range of multicast
				 * addresses.  For now, just accept all
				 * multicasts, rather than trying to set
				 * only those filter bits needed to match
				 * the range.  (At this time, the only use
				 * of address ranges is for IP multicast
				 * routing, for which the range is big enough
				 * to require all bits set.)
				 */
				ifp->if_flags |= IFF_ALLMULTI;
				mac_cr |= LAN9118_MAC_CR_MCPAS;
				break;
			}
			h = ether_crc32_le(enm->enm_addrlo,
			    ETHER_ADDR_LEN) >> 26;
			hashes[h >> 5] |= 1 << (h & 0x1f);

			mac_cr |= LAN9118_MAC_CR_HPFILT;
			ETHER_NEXT_MULTI(step, enm);
		}
		ETHER_UNLOCK(ec);
		if (mac_cr & LAN9118_MAC_CR_HPFILT) {
			lan9118_mac_writereg(sc, LAN9118_HASHH, hashes[1]);
			lan9118_mac_writereg(sc, LAN9118_HASHL, hashes[0]);
		}
	}
	lan9118_mac_writereg(sc, LAN9118_MAC_CR, mac_cr);
	return;
}

static void
lan9118_rxintr(struct lan9118_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ec.ec_if;
	struct mbuf *m;
	uint32_t rx_fifo_inf, rx_status;
	int pktlen;
	const int pad = ETHER_HDR_LEN % sizeof(uint32_t);

	DPRINTFN(3, ("%s\n", __func__));

	for (unsigned budget = 0;
	     !(sc->sc_flags & LAN9118_FLAGS_POLL) || budget < 8; budget++) {
		rx_fifo_inf = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    LAN9118_RX_FIFO_INF);
		if (LAN9118_RX_FIFO_INF_RXSUSED(rx_fifo_inf) == 0)
			break;

		rx_status = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    LAN9118_RXSFIFOP);
		if (sc->sc_flags & LAN9118_FLAGS_RXDIAG)
			lan9118_rxdiag_status(sc->sc_rxdiag, rx_status);
		pktlen = LAN9118_RXS_PKTLEN(rx_status);
		if ((sc->sc_flags & LAN9118_FLAGS_RXDIAG) &&
		    (pktlen < ETHER_CRC_LEN || pktlen > MCLBYTES - pad))
			sc->sc_rxdiag[LAN_RX_BADLEN].ev_count++;
		DPRINTFN(3, ("%s: rx_status=0x%x(pktlen %d)\n",
		    __func__, rx_status, pktlen));
		if (pktlen < ETHER_CRC_LEN || pktlen > MCLBYTES - pad ||
		    rx_status & (LAN9118_RXS_ES | LAN9118_RXS_LENERR |
				 LAN9118_RXS_RWTO | LAN9118_RXS_MIIERR |
				 LAN9118_RXS_DBIT)) {
			if (rx_status & LAN9118_RXS_LENERR)
				aprint_error_dev(sc->sc_dev, "Length Error\n");
			if (rx_status & LAN9118_RXS_RUNTF)
				aprint_error_dev(sc->sc_dev, "Runt Frame\n");
			if (rx_status & LAN9118_RXS_FTL)
				aprint_error_dev(sc->sc_dev,
				    "Frame Too Long\n");
			if (rx_status & LAN9118_RXS_RWTO)
				aprint_error_dev(sc->sc_dev,
				    "Receive Watchdog time-out\n");
			if (rx_status & LAN9118_RXS_MIIERR)
				aprint_error_dev(sc->sc_dev, "MII Error\n");
			if (rx_status & LAN9118_RXS_DBIT)
				aprint_error_dev(sc->sc_dev, "Drabbling Bit\n");
			if (rx_status & LAN9118_RXS_COLS)
				aprint_error_dev(sc->sc_dev,
				    "Collision Seen\n");
			if (rx_status & LAN9118_RXS_CRCERR)
				aprint_error_dev(sc->sc_dev, "CRC Error\n");

dropit:
			if_statinc(ifp, if_ierrors);
			/*
			 * Receive Data FIFO Fast Forward
			 * When performing a fast-forward, there must be at
			 * least 4 DWORDs of data in the RX data FIFO for the
			 * packet being discarded.
			 */
			if (pktlen >= 4 * sizeof(uint32_t)) {
				uint32_t rx_dp_ctl;
				unsigned fftries = 10000;

				bus_space_write_4(sc->sc_iot, sc->sc_ioh,
				    LAN9118_RX_DP_CTL,
				    LAN9118_RX_DP_CTL_RX_FFWD);
				/* DP_FFWD bit is self clearing */
				do {
					rx_dp_ctl = bus_space_read_4(sc->sc_iot,
					    sc->sc_ioh, LAN9118_RX_DP_CTL);
				} while (
				    (rx_dp_ctl & LAN9118_RX_DP_CTL_RX_FFWD) &&
				    --fftries);
				if (!fftries) {
					if (sc->sc_flags & LAN9118_FLAGS_RXDIAG)
						sc->sc_rxdiag[LAN_RX_FF_TIMEOUT].ev_count++;
					return;
				}
			} else {
				/* For less than 4 DWORDs do not use RX_FFWD. */
				/* RXDOFF contributes leading bytes even for a
				 * discarded short frame; RXEA rounds to a word.
				 */
				uint32_t garbage[5];

				bus_space_read_multi_4(sc->sc_iot, sc->sc_ioh,
				    LAN9118_RXDFIFOP, garbage,
				    roundup(pad + pktlen, 4) >> 2);
			}
			continue;
}

		MGETHDR(m, M_DONTWAIT, MT_DATA);
		if (m == NULL) {
			if (sc->sc_flags & LAN9118_FLAGS_RXDIAG)
				sc->sc_rxdiag[LAN_RX_NOMBUF].ev_count++;
			goto dropit;
		}
		if (pktlen > (MHLEN - pad)) {
			MCLGET(m, M_DONTWAIT);
			if ((m->m_flags & M_EXT) == 0) {
				if (sc->sc_flags & LAN9118_FLAGS_RXDIAG)
					sc->sc_rxdiag[LAN_RX_NOCLUSTER].ev_count++;
				m_freem(m);
				goto dropit;
			}
		}

		/* STRICT_ALIGNMENT */
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, LAN9118_RX_CFG,
		    LAN9118_RX_CFG_RXEA_4B | LAN9118_RX_CFG_RXDOFF(pad));
		bus_space_read_multi_4(sc->sc_iot, sc->sc_ioh, LAN9118_RXDFIFOP,
		    mtod(m, uint32_t *),
		    roundup(pad + pktlen, sizeof(uint32_t)) >> 2);
		m->m_data += pad;

		m_set_rcvif(m, ifp);
		m->m_pkthdr.len = m->m_len = (pktlen - ETHER_CRC_LEN);

		/* Pass it on. */
		if (sc->sc_flags & LAN9118_FLAGS_RXDIAG)
			sc->sc_rxdiag[LAN_RX_DELIVERED].ev_count++;
		if_percpuq_enqueue(ifp->if_percpuq, m);
	}
}

static void
lan9118_txintr(struct lan9118_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ec.ec_if;
	uint32_t tx_fifo_inf, tx_status;
	int fdx = IFM_OPTIONS(sc->sc_mii.mii_media_active) & IFM_FDX;
	int tdfree;

	DPRINTFN(3, ("%s\n", __func__));

	for (unsigned budget = 0;
	     !(sc->sc_flags & LAN9118_FLAGS_POLL) || budget < 32; budget++) {
		tx_fifo_inf = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    LAN9118_TX_FIFO_INF);
		if (LAN9118_TX_FIFO_INF_TXSUSED(tx_fifo_inf) == 0)
			break;

		tx_status = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    LAN9118_TXSFIFOP);
		DPRINTFN(3, ("%s: tx_status=0x%x\n", __func__, tx_status));
		if (tx_status & LAN9118_TXS_ES) {
			if (tx_status & LAN9118_TXS_LOC)
				aprint_error_dev(sc->sc_dev,
				    "Loss Of Carrier\n");
			if ((tx_status & LAN9118_TXS_NC) && !fdx)
				aprint_error_dev(sc->sc_dev, "No Carrier\n");
			if (tx_status & LAN9118_TXS_LCOL)
				aprint_error_dev(sc->sc_dev,
				    "Late Collision\n");
			if (tx_status & LAN9118_TXS_ECOL) {
				/* Rearch 16 collision */
				if_statadd(ifp, if_collisions, 16);
				aprint_error_dev(sc->sc_dev,
				    "Excessive Collision\n");
			}
			if (LAN9118_TXS_COLCNT(tx_status) != 0)
				aprint_error_dev(sc->sc_dev,
				    "Collision Count: %d\n",
				    LAN9118_TXS_COLCNT(tx_status));
			if (tx_status & LAN9118_TXS_ED)
				aprint_error_dev(sc->sc_dev,
				    "Excessive Deferral\n");
			if (tx_status & LAN9118_TXS_DEFERRED)
				aprint_error_dev(sc->sc_dev, "Deferred\n");
			if_statinc(ifp, if_oerrors);
		} else
			if_statinc(ifp, if_opackets);
	}

	tdfree = LAN9118_TX_FIFO_INF_TDFREE(tx_fifo_inf);
	if (tdfree == (unsigned)(sc->sc_txfifosz * 1024 - 512))
		/* FIFO empty */
		ifp->if_timer = 0;
	if (tdfree >= 2036)
		/*
		 * 2036 is the possible maximum FIFO consumption
		 * for the most fragmented frame.
		 */
		ifp->if_flags &= ~IFF_OACTIVE;
}

void
lan9118_tick(void *v)
{
	struct lan9118_softc *sc = v;
	int s;

	s = splnet();
	lan9118_rxdiag_sample(sc);
	mii_tick(&sc->sc_mii);
	callout_schedule(&sc->sc_tick, hz);
	splx(s);
}
