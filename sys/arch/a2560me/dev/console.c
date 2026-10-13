/* SPDX-License-Identifier: BSD-2-Clause */
#define _M68K_BUS_SPACE_PRIVATE
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/termios.h>
#include <dev/ic/comvar.h>
#include <dev/ic/pckbcvar.h>
#include "opt_a2560me.h"
void a2560me_wscons_init(void);
#include <sys/systm.h>
#include <sys/conf.h>
#include <dev/cons.h>
#include <dev/wscons/wsconsio.h>
#include <machine/board.h>
#include <dev/wsfont/wsfont.h>
#include <dev/wsfont/bold8x16.h>
/* Map emulator Unicode to the IBM-encoded font loaded into Vicky below. */
int
a2560me_console_mapchar(int c, unsigned *out)
{
	int glyph = c < 0 ? -1 : wsfont_map_unichar(&bold8x16, c);

	if (glyph < bold8x16.firstchar ||
	    glyph >= bold8x16.firstchar + bold8x16.numchars) {
		*out = '?';
		return 0;
	}
	*out = glyph;
	return 5;
}

static unsigned rows;
static unsigned row, col;
static bool uart_ready;
#ifndef A2560ME_WSCONS
static struct consdev *serialcons;
#endif
bool a2560me_video_mirror = true;
static void sio(unsigned r, unsigned v)
{
	REG8(0xfec0202e) = r;
	REG8(0xfec0202f) = v;
}
static void uartput(int c)
{
#ifdef A2560ME_USB_CONSOLE
	a2560me_usb_putc(c);
	return;
#else
	a2560me_usb_mirror(c);
#endif
	/* Bounded so a missing UART cannot freeze boot or panic output. */
	for (unsigned i = 0; i < 100000; i++)
		if (REG8(A2560ME_UART1 + 5) & 0x20) {
			REG8(A2560ME_UART1) = c;
			break;
		}
}
static void textput(int c)
{
	if (c == '\r')
		col = 0;
	else if (c == '\n') {
		col = 0;
		row++;
	} else if (c == '\b') {
		if (col)
			col--;
	} else if (c == '\t') {
		do {
			textput(' ');
		} while (col & 7);
	} else if (c >= 32) {
		REG8(0xfeca0000 + row * 160 + col) = c;
		REG8(0xfeca8000 + row * 160 + col) = 0xf0;
		if (++col == 160) {
			col = 0;
			row++;
		}
	}
	if (row >= rows) {
		for (unsigned i = 0; i < (rows - 1) * 160; i++) {
			REG8(0xfeca0000 + i) = REG8(0xfeca0000 + i + 160);
			REG8(0xfeca8000 + i) = REG8(0xfeca8000 + i + 160);
		}
		row = rows - 1;
		for (unsigned i = row * 160; i < rows * 160; i++) {
			REG8(0xfeca0000 + i) = ' ';
			REG8(0xfeca8000 + i) = 0xf0;
		}
	}
	REG32(0xfec80014) = (row << 16) | col;
}
void
a2560me_boot_puts(const char *s)
{
	while (*s) {
		int c = *s++;
		/* Display first: a stuck LPC bus must not hide the last stage. */
		textput(c);
		if (uart_ready) {
			if (c == '\n') uartput('\r');
			uartput(c);
		}
	}
}
#ifndef A2560ME_WSCONS
static void cnput(dev_t d, int c)
{
	if (c == '\n')
		uartput('\r');
	uartput(c);
	if (a2560me_video_mirror)
		textput(c);
}
static int cnget(dev_t d)
{
#ifdef A2560ME_USB_CONSOLE
	return a2560me_usb_getc();
#endif
	if (serialcons)
		return serialcons->cn_getc(d);
	while (!(REG8(A2560ME_UART1 + 5) & 1))
		;
	return REG8(A2560ME_UART1);
}
static void cnpoll(dev_t d, int on)
{
	a2560me_usb_pollc(on);
	if (serialcons)
		serialcons->cn_pollc(d, on);
}
static struct consdev boardcons = {.cn_getc = cnget,
				   .cn_putc = cnput,
				   .cn_pollc = cnpoll,
				   .cn_dev = NODEV,
				   .cn_pri = CN_NORMAL};
#endif
void a2560me_cninit(void)
{

	uint32_t hi = REG32(0xfec80000) & 0x40000000;
	rows = hi ? 64 : 60;
	REG32(0xfec80000) = 1 | (hi ? 0x100 : 0);
	REG32(0xfec80004) = 0;
	REG32(0xfec80008) = 0;

	for (unsigned i = 0; i < 4096; i++)
		REG8(0xfec88000 + i) = 0;

	const uint8_t *font = bold8x16.data;
	for (unsigned i = 0; i < bold8x16.numchars * 16; i++)
		REG8(0xfec88000 + bold8x16.firstchar * 16 + i) = font[i];

	for (unsigned i = 0; i < 16; i++) {
		uint32_t v = ((i & 1) ? 0xaa0000 : 0) | ((i & 2) ? 0xaa00 : 0) |
			     ((i & 4) ? 0xaa : 0);
		if (i & 8)
			v += 0x555555;
		REG32(0xfecac400 + i * 4) = 0xff000000 | v;
		REG32(0xfecac440 + i * 4) = 0xff000000 | v;
	}

	for (unsigned i = 0; i < rows * 160; i++) {
		REG8(0xfeca0000 + i) = ' ';
		REG8(0xfeca8000 + i) = 0xf0;
	}

	REG32(0xfec80010) = 0xf0b10001;

	/* MCP A2560Me UnReset_LPC: high, low, then high with settling time. */

	REG32(0xfec00000) |= 0x100;
	delay(1000);
	REG32(0xfec00000) &= ~0x100u;
	delay(100000);
	REG32(0xfec00000) |= 0x100;
	delay(500000);

	REG8(0xfec0202e) = 0x55;
	sio(7, 4);
	sio(0x60, 3);
	sio(0x61, 0xf8);
	sio(0x70, 4);
	sio(0x30, 1);
	sio(7, 7);
	sio(0x60, 0);
	sio(0x61, 0x60);
	sio(0x62, 0);
	sio(0x63, 0x64);
	sio(0x70, 1);
	sio(0x72, 2);
	sio(0x30, 1);
	/* PME runtime window contains the GPIO pin-multiplexing registers. */
	sio(7, 10); sio(0x60, 1); sio(0x61, 0); sio(0x30, 1);
	sio(0x22, 0xff); /* MCP global device power enable. */
	REG8(0xfec0202e) = 0xaa;
	for (unsigned i = 0; i < 8; i++)
		REG8(0xfec02123 + i) = i < 4 ? 1 : 5;

	/* Restore cooling after LPC reset, as in A2560Me MCP superio_init(). */
	REG8(A2560ME_GP33) = 0x04; /* Select the FAN1 pin function. */
	REG8(A2560ME_FAN1) = 0x90; /* MCP fan enable/PWM setting. */
	REG8(A2560ME_FAN_CTRL) = 0x01;

	REG8(A2560ME_UART1 + 1) = 0;
	REG8(A2560ME_UART1 + 3) = 0x80;
	REG8(A2560ME_UART1) = 1;
	REG8(A2560ME_UART1 + 1) = 0;
	REG8(A2560ME_UART1 + 3) = 3;
	REG8(A2560ME_UART1 + 2) = 7;
	REG8(A2560ME_UART1 + 4) = 3;

	a2560me_usb_init();
	uart_ready = true;
#ifndef A2560ME_WSCONS

#ifdef A2560ME_USB_CONSOLE
	boardcons.cn_dev = makedev(13, A2560ME_USB_CONSOLE_UNIT);
#else
	if (comcnattach(&m68k_simple_bus_space, A2560ME_UART1, 115200, 1843200,
			COM_TYPE_NORMAL, CS8 | CREAD | CLOCAL) == 0) {
		serialcons = cn_tab;
		boardcons.cn_dev = cn_tab->cn_dev;
	}
#endif
	cn_tab = &boardcons;
#endif

	a2560me_wscons_init();
#ifdef A2560ME_WSCONS
	(void)pckbc_cnattach(&m68k_simple_bus_space, 0xfec02060, 4,
			     PCKBC_KBD_SLOT, 0);
#endif
}
