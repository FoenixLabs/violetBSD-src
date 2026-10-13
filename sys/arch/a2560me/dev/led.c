/* SPDX-License-Identifier: BSD-2-Clause */
/* A2560Me mappings/colors from MCP indicators.c and A2560M/gabe_a2560m.h. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/callout.h>
#include <machine/board.h>

#define LED_POWER 0x01
#define LED_SD0   0x02
#define LED_SD1   0x04
#define LED_NET   0x08
#define RGB(n) (A2560ME_SYSTEM_CTRL + 0x10 + (n) * 4)
static unsigned busy[4];
static callout_t net_off;
static bool ready, net_on;
static unsigned net_tick;

/* All callers hold splhigh: preserve DIP/buzzer/reset/unrelated control bits. */
static void led_bit(unsigned mask, bool on)
{
	uint32_t value = REG32(A2560ME_SYSTEM_CTRL);
	REG32(A2560ME_SYSTEM_CTRL) = on ? value | mask : value & ~mask;
}
static void network_off(void *arg)
{
	int s = splhigh();
	unsigned hold = MAX(1, hz / 10);
	unsigned elapsed = (unsigned)getticks() - net_tick;
	if (elapsed < hold) {
		callout_schedule(&net_off, hold - elapsed);
		splx(s);
		return;
	}
	led_bit(LED_NET, false);
	net_on = false;
	splx(s);
}
void a2560me_led_init(void)
{
	callout_init(&net_off, 0);
	callout_setfunc(&net_off, network_off, NULL);
	int s = splhigh();
	REG32(RGB(0)) = 0x00003000; /* power: green */
	REG32(RGB(1)) = 0x00000030; /* front SD: blue */
	REG32(RGB(2)) = 0x00ff4000; /* back SD: orange */
	REG32(RGB(3)) = 0x00008080; /* network: cyan */
	led_bit(LED_SD0 | LED_SD1 | LED_NET, false);
	led_bit(LED_POWER, true);
	ready = true;
	splx(s);
}
void a2560me_led_disk(unsigned unit, bool on)
{
	/* MCP provides no local-SD (ld2) or floppy indicator operation. */
	if (!ready || unit > 3 || unit == 2)
		return;
	int s = splhigh();
	if (on)
		busy[unit]++;
	else {
		KASSERT(busy[unit] != 0);
		busy[unit]--;
	}
	if (unit == 0)
		led_bit(LED_SD0, busy[0] != 0);
	else {
		/* ATA and back SD share RGB2. ATA takes color priority. */
		REG32(RGB(2)) = busy[3] ? 0x00030030 : 0x00ff4000;
		led_bit(LED_SD1, busy[1] != 0 || busy[3] != 0);
	}
	splx(s);
}
void a2560me_led_network(void)
{
	if (!ready)
		return;
	int s = splhigh();
	/* Called for FIFO words; coalesce updates to at most once per tick. */
	if (!net_on || net_tick != (unsigned)getticks()) {
		net_tick = (unsigned)getticks();
		if (!net_on)
			led_bit(LED_NET, true);
		net_on = true;
		callout_schedule(&net_off, MAX(1, hz / 10));
	}
	splx(s);
}
