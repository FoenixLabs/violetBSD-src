/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/cpu.h>
#include <sys/timetc.h>
#include <machine/board.h>
#include <machine/clockrate.h>
unsigned a2560me_clock_hz = A2560ME_CLOCK_HZ;
static unsigned clock_id;
static bool timer_running;
static u_int counter(struct timecounter *tc)
{
	return REG32(0xfec00210);
}
static struct timecounter timer_tc = {.tc_get_timecount = counter,
				      .tc_counter_mask = 0xffffffff,
				      .tc_frequency = A2560ME_CLOCK_HZ,
				      .tc_name = "gavin-timer1",
				      .tc_quality = 100};
void a2560me_clock_early(void)
{
	clock_id = REG32(0xfec0000c);
	unsigned rate = a2560me_clock_rate(clock_id);
	if (rate)
		a2560me_clock_hz = rate;
	/* Timer 1 free-runs upward without interrupts, for delays and
	 * timecounter. */
	REG32(0xfec00200) = 0;
	REG32(0xfec00210) = 0;
	REG32(0xfec00214) = 0xffffffff;
	REG32(0xfec00200) = 0x00000a00;
	REG32(0xfec00200) = 0x00000900;
	uint32_t before = counter(NULL);
	for (unsigned i = 0; i < 1000; i++)
		__asm volatile("nop");
	timer_running = counter(NULL) != before;
}
void delay(unsigned int us)
{
	if (!timer_running) {
		/* Diagnostic fallback only; never start the scheduler on a dead
		 * timer. */
		while (us--)
			for (unsigned i = 0; i < 16; i++)
				__asm volatile("nop");
		return;
	}
	/* Bound arithmetic and keep each interval below a counter wrap. */
	while (us) {
		unsigned n = uimin(us, 1000000), start = counter(NULL);
		unsigned ticks = a2560me_delay_ticks(n, a2560me_clock_hz);
		while ((uint32_t)(counter(NULL) - start) < ticks)
			__asm volatile("nop");
		us -= n;
	}
}
void cpu_initclocks(void)
{
	if (!timer_running)
		panic("GAVIN timer1 is not advancing; check timer map/clock");
	stathz = 0;
	if (!a2560me_clock_rate(clock_id))
		printf("WARNING: unknown GAVIN clock ID %#x; assuming 50 MHz\n", clock_id);
	printf("GAVIN clock: ID %#x, CPU/bus/timers %u Hz, FPGA build %u\n",
	    clock_id, a2560me_clock_hz, REG32(0xfec0003c));
	timer_tc.tc_frequency = a2560me_clock_hz;
	tc_init(&timer_tc);
	REG32(0xfec00208) = 0;
	REG32(0xfec0020c) = a2560me_clock_hz / hz - 1;
	REG32(0xfec00200) = 0x0000090a;
	REG16(0xfec00102) = 0x0200;
	REG16(0xfec0011a) &= ~0x0200;
	REG32(0xfec00200) = 0x00000989;
}
void a2560me_clock_intr(struct clockframe *f)
{
	/* Timer 0 lacks explicit auto-reload bits: clear at each compare. */
	REG32(0xfec00200) = 0x0000098b;
	REG32(0xfec00200) = 0x00000989;
	hardclock(f);
}
void setstatclockrate(int hznew)
{
}
