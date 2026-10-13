/* SPDX-License-Identifier: BSD-2-Clause */
/* A2560Me BQ4802 time-of-day clock; hardware reference rev 0.2, pp. 32-33. */
#include <sys/param.h>
#include <sys/device.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <dev/clock_subr.h>
#include <dev/ic/bq4802reg.h>
#include <machine/board.h>
#include <machine/intr.h>

struct fnrtc_softc {
	struct todr_chip_handle sc_todr;
};

uint8_t
bq4802_read(void *cookie, uint8_t reg)
{
	return REG8(A2560ME_RTC_BASE + reg);
}

void
bq4802_write(void *cookie, uint8_t reg, uint8_t value)
{
	/* Wider writes put zero on the RTC's data bus. */
	REG8(A2560ME_RTC_BASE + reg) = value;
}

static int
fnrtc_valid(const struct clock_ymdhms *dt)
{
	static const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
	unsigned limit;

	if (dt->dt_year < 1970 || dt->dt_year > 9999 ||
	    dt->dt_mon < 1 || dt->dt_mon > 12 || dt->dt_hour > 23 ||
	    dt->dt_min > 59 || dt->dt_sec > 59)
		return EINVAL;
	limit = days[dt->dt_mon - 1];
	if (dt->dt_mon == 2 && dt->dt_year % 4 == 0 &&
	    (dt->dt_year % 100 != 0 || dt->dt_year % 400 == 0))
		limit++;
	return dt->dt_day < 1 || dt->dt_day > limit ? EINVAL : 0;
}

static int
fnrtc_gettime(todr_chip_handle_t handle, struct clock_ymdhms *dt)
{
	bq4802_regs regs;
	uint8_t ctrl, hour;
	int i, s;

	s = splclock();
	ctrl = bq4802_read(handle, BQ4802_CTRL);
	/* A pre-existing latch could contain stale time. Do not trust it. */
	if (ctrl & BQ4802_CTRL_UTI) {
		splx(s);
		printf("fnrtc: calendar update inhibited (ctrl=%02x)\n", ctrl);
		return EBUSY;
	}
	bq4802_write(handle, BQ4802_CTRL, ctrl | BQ4802_CTRL_UTI);
	for (i = 0; i < BQ4802_NREGS; i++)
		if (bq4802_tods[i])
			regs[i] = bq4802_read(handle, i);
	bq4802_write(handle, BQ4802_CTRL, ctrl);
	splx(s);

	hour = regs[BQ4802_HOUR];
	if (!(ctrl & BQ4802_CTRL_24))
		regs[BQ4802_HOUR] &= ~BQ4802_HOURS_PM;
	for (i = 0; i < BQ4802_NREGS; i++) {
		if (!bq4802_tods[i] || i == BQ4802_WDAY)
			continue;
		if ((regs[i] & 0x0f) > 9 || (regs[i] >> 4) > 9)
			goto invalid;
	}
	dt->dt_hour = bcdtobin(regs[BQ4802_HOUR]);
	if (ctrl & BQ4802_CTRL_24) {
		/* BQ4802 uses 24 for midnight; tolerate MCP's 00 as well. */
		if (dt->dt_hour == 24)
			dt->dt_hour = 0;
	} else {
		if (dt->dt_hour < 1 || dt->dt_hour > 12)
			goto invalid;
		dt->dt_hour %= 12;
		if (hour & BQ4802_HOURS_PM)
			dt->dt_hour += 12;
	}
	dt->dt_year = 100 * bcdtobin(regs[BQ4802_CENT]) +
	    bcdtobin(regs[BQ4802_YEAR]);
	dt->dt_mon = bcdtobin(regs[BQ4802_MONTH]);
	dt->dt_day = bcdtobin(regs[BQ4802_DAY]);
	dt->dt_min = bcdtobin(regs[BQ4802_MIN]);
	dt->dt_sec = bcdtobin(regs[BQ4802_SEC]);
	/* MCP does not maintain weekday; TOD conversion does not use it. */
	dt->dt_wday = regs[BQ4802_WDAY] >= 1 && regs[BQ4802_WDAY] <= 7 ?
	    regs[BQ4802_WDAY] - 1 : 0;
	if (fnrtc_valid(dt) == 0)
		return 0;
invalid:
	printf("fnrtc: invalid calendar: ctrl=%02x century=%02x year=%02x "
	    "month=%02x day=%02x hour=%02x min=%02x sec=%02x\n",
	    ctrl, regs[BQ4802_CENT], regs[BQ4802_YEAR],
	    regs[BQ4802_MONTH], regs[BQ4802_DAY], hour,
	    regs[BQ4802_MIN], regs[BQ4802_SEC]);
	return EINVAL;
}

static int
fnrtc_settime(todr_chip_handle_t handle, struct clock_ymdhms *dt)
{
	bq4802_regs regs;
	uint8_t ctrl;
	int i, s;

	if (fnrtc_valid(dt) != 0 || dt->dt_wday > 6)
		return EINVAL;
	regs[BQ4802_SEC] = bintobcd(dt->dt_sec);
	regs[BQ4802_MIN] = bintobcd(dt->dt_min);
	regs[BQ4802_HOUR] = bintobcd(dt->dt_hour == 0 ? 24 : dt->dt_hour);
	regs[BQ4802_DAY] = bintobcd(dt->dt_day);
	regs[BQ4802_WDAY] = dt->dt_wday + 1;
	regs[BQ4802_MONTH] = bintobcd(dt->dt_mon);
	regs[BQ4802_YEAR] = bintobcd(dt->dt_year % 100);
	regs[BQ4802_CENT] = bintobcd(dt->dt_year / 100);
	s = splclock();
	ctrl = bq4802_read(handle, BQ4802_CTRL);
	bq4802_write(handle, BQ4802_CTRL, ctrl | BQ4802_CTRL_UTI);
	for (i = 0; i < BQ4802_NREGS; i++)
		if (bq4802_tods[i])
			bq4802_write(handle, i, regs[i]);
	/* NetBSD handles timezone/DST. Keep the oscillator running on battery. */
	ctrl = (ctrl & ~(BQ4802_CTRL_UTI | BQ4802_CTRL_DSE)) |
	    BQ4802_CTRL_24 | BQ4802_CTRL_STP;
	bq4802_write(handle, BQ4802_CTRL, ctrl);
	splx(s);
	return 0;
}

static int
fnrtc_match(device_t parent, cfdata_t cf, void *aux)
{
	return 1;
}

static void
fnrtc_attach(device_t parent, device_t self, void *aux)
{
	struct fnrtc_softc *sc = device_private(self);

	aprint_normal(": BQ4802 real-time clock\n");
	sc->sc_todr.cookie = self;
	sc->sc_todr.todr_gettime_ymdhms = fnrtc_gettime;
	sc->sc_todr.todr_settime_ymdhms = fnrtc_settime;
	todr_attach(&sc->sc_todr);
}

CFATTACH_DECL_NEW(fnrtc, sizeof(struct fnrtc_softc),
    fnrtc_match, fnrtc_attach, NULL, NULL);
