/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _A2560ME_CLOCKRATE_H_
#define _A2560ME_CLOCKRATE_H_
/* GAVIN CPU speed IDs from manufacturer Build 34/35 notes. Timers use BCLK.
 * The 66.67 MHz clock is represented to the nearest Hz (200 MHz / 3).
 */
static inline unsigned
a2560me_clock_rate(unsigned id)
{
	if (id == 0x0009b05c)
		return 50000000;
	if (id == 0x0009b06c)
		return 66666667;
	return 0;
}
/* Caller chunks delays to <= one second. Round up, never truncate a delay. */
static inline unsigned
a2560me_delay_ticks(unsigned us, unsigned rate)
{
	return rate == 66666667 ? (us * 200U + 2U) / 3U : us * 50U;
}
#endif
