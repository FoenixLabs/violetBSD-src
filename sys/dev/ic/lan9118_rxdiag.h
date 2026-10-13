/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _LAN9118_RXDIAG_H_
#define _LAN9118_RXDIAG_H_

/* Included after lan9118reg.h and sys/evcnt.h. No additional FIFO reads. */
enum {
 LAN_RX_DROP, LAN_RX_SAMPLES, LAN_RX_RESETS,
 LAN_RX_RSFL, LAN_RX_RXDF, LAN_RX_RXE, LAN_RX_RSFF,
 LAN_RX_STATUS, LAN_RX_ES, LAN_RX_LENGTH, LAN_RX_RUNT, LAN_RX_LONG,
 LAN_RX_WATCHDOG, LAN_RX_MII, LAN_RX_DRIBBLE, LAN_RX_COLLISION, LAN_RX_CRC,
 LAN_RX_FILTER, LAN_RX_BADLEN, LAN_RX_NOMBUF, LAN_RX_NOCLUSTER,
 LAN_RX_DELIVERED, LAN_RX_FF_TIMEOUT, LAN_RX_NCOUNTERS
};
static inline void
lan9118_rxdiag_interrupt(struct evcnt *c, uint32_t status)
{
 if (status & LAN9118_INT_RSFL) c[LAN_RX_RSFL].ev_count++;
 if (status & LAN9118_INT_RXDF_INT) c[LAN_RX_RXDF].ev_count++;
 if (status & LAN9118_INT_RXE) c[LAN_RX_RXE].ev_count++;
 if (status & LAN9118_INT_RSFF) c[LAN_RX_RSFF].ev_count++;
}
static inline void
lan9118_rxdiag_status(struct evcnt *c, uint32_t status)
{
 c[LAN_RX_STATUS].ev_count++;
 if (status & LAN9118_RXS_ES) c[LAN_RX_ES].ev_count++;
 if (status & LAN9118_RXS_LENERR) c[LAN_RX_LENGTH].ev_count++;
 if (status & LAN9118_RXS_RUNTF) c[LAN_RX_RUNT].ev_count++;
 if (status & LAN9118_RXS_FTL) c[LAN_RX_LONG].ev_count++;
 if (status & LAN9118_RXS_RWTO) c[LAN_RX_WATCHDOG].ev_count++;
 if (status & LAN9118_RXS_MIIERR) c[LAN_RX_MII].ev_count++;
 if (status & LAN9118_RXS_DBIT) c[LAN_RX_DRIBBLE].ev_count++;
 if (status & LAN9118_RXS_COLS) c[LAN_RX_COLLISION].ev_count++;
 if (status & LAN9118_RXS_CRCERR) c[LAN_RX_CRC].ev_count++;
 if (status & LAN9118_RXS_FILTFAIL) c[LAN_RX_FILTER].ev_count++;
}
#endif
