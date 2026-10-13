/* SPDX-License-Identifier: BSD-2-Clause */
/* A2560Me FPGA USB UART: byte-wide status/control and data registers.
 * Register layout documented by the local GEMTOS foenix/foenix.h.
 * This is not a 16550. Use bounded polling until IRQ delivery is verified.
 */
#include <sys/param.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/callout.h>
#include <sys/tty.h>
#include <sys/kauth.h>
#include <machine/board.h>

#define USB_BASE 0xfec00b00
#define USB_STRIDE 0x100
#define RX_EMPTY 2
#define TX_EMPTY 4
struct fnuart_softc {
	struct tty *tty;
	uintptr_t base;
	struct callout poll;
};
extern struct cfdriver fnuart_cd;
static bool console_polling;
static void start(struct tty *);
static void service(void *);

static bool putbyte(uintptr_t base, int c)
{
	for (unsigned i = 0; i < 100000; i++) {
		if (REG8(base) & TX_EMPTY) {
			REG8(base + 1) = c;
			return true;
		}
	}
	return false;
}
void a2560me_usb_putc(int c) { putbyte(A2560ME_USB_CONSOLE_BASE, c); }
void a2560me_usb_mirror(int c)
{
	putbyte(USB_BASE, c);
	putbyte(USB_BASE + USB_STRIDE, c);
}
void a2560me_usb_init(void)
{
	for (unsigned unit = 0; unit < 2; unit++)
		REG8(USB_BASE + unit * USB_STRIDE) = 0; /* 115200 8N1 */
}
int a2560me_usb_getc(void)
{
	uintptr_t base = A2560ME_USB_CONSOLE_BASE;
	while (REG8(base) & RX_EMPTY)
		;
	return REG8(base + 1);
}
void a2560me_usb_pollc(int on) { console_polling = on != 0; }
static int param(struct tty *tp, struct termios *t)
{
	if (t->c_ospeed != 115200 ||
	    (t->c_ispeed != 0 && t->c_ispeed != 115200) ||
	    (t->c_cflag & (CSIZE | PARENB | CSTOPB | CRTSCTS)) != CS8)
		return EINVAL;
	tp->t_ispeed = t->c_ispeed;
	tp->t_ospeed = t->c_ospeed;
	tp->t_cflag = t->c_cflag | CLOCAL;
	return 0;
}
static int match(device_t p, cfdata_t cf, void *aux) { return cf->cf_unit < 2; }
static void attach(device_t p, device_t self, void *aux)
{
	struct fnuart_softc *sc = device_private(self);
	sc->base = USB_BASE + device_unit(self) * USB_STRIDE;
	sc->tty = tty_alloc();
	sc->tty->t_oproc = start;
	sc->tty->t_param = param;
	tty_attach(sc->tty);
	callout_init(&sc->poll, 0);
	callout_setfunc(&sc->poll, service, sc);
	aprint_normal(": FPGA USB UART at %#lx, 115200 8N1, polling\n",
	    (unsigned long)sc->base);
}
CFATTACH_DECL_NEW(fnuart, sizeof(struct fnuart_softc), match, attach, NULL, NULL);

dev_type_open(fnuartopen);
dev_type_close(fnuartclose);
dev_type_read(fnuartread);
dev_type_write(fnuartwrite);
dev_type_ioctl(fnuartioctl);
dev_type_tty(fnuarttty);
dev_type_poll(fnuartpoll);
const struct cdevsw fnuart_cdevsw = {
	.d_open = fnuartopen, .d_close = fnuartclose,
	.d_read = fnuartread, .d_write = fnuartwrite,
	.d_ioctl = fnuartioctl, .d_stop = nostop, .d_tty = fnuarttty,
	.d_poll = fnuartpoll, .d_mmap = nommap, .d_kqfilter = ttykqfilter,
	.d_discard = nodiscard, .d_flag = D_TTY
};
struct tty *fnuarttty(dev_t dev)
{
	struct fnuart_softc *sc = device_lookup_private(&fnuart_cd, minor(dev));
	return sc ? sc->tty : NULL;
}
int fnuartopen(dev_t dev, int flag, int mode, struct lwp *l)
{
	struct fnuart_softc *sc = device_lookup_private(&fnuart_cd, minor(dev));
	if (sc == NULL) return ENXIO;
	struct tty *tp = sc->tty;
	if (kauth_authorize_device_tty(l->l_cred, KAUTH_DEVICE_TTY_OPEN, tp))
		return EBUSY;
	tp->t_dev = dev;
	if (!(tp->t_state & TS_ISOPEN)) {
		ttychars(tp);
		tp->t_iflag = TTYDEF_IFLAG;
		tp->t_oflag = TTYDEF_OFLAG;
		tp->t_lflag = TTYDEF_LFLAG;
		tp->t_cflag = CS8 | CREAD | CLOCAL;
		tp->t_ispeed = tp->t_ospeed = 115200;
		ttsetwater(tp);
	}
	tp->t_state |= TS_CARR_ON;
	int error = (*tp->t_linesw->l_open)(dev, tp);
	if (!error) callout_schedule(&sc->poll, 1);
	return error;
}
int fnuartclose(dev_t dev, int flag, int mode, struct lwp *l)
{
	struct fnuart_softc *sc = device_lookup_private(&fnuart_cd, minor(dev));
	callout_stop(&sc->poll);
	(*sc->tty->t_linesw->l_close)(sc->tty, flag);
	ttyclose(sc->tty);
	return 0;
}
int fnuartread(dev_t dev, struct uio *uio, int flag)
{
	struct tty *tp = fnuarttty(dev);
	return (*tp->t_linesw->l_read)(tp, uio, flag);
}
int fnuartwrite(dev_t dev, struct uio *uio, int flag)
{
	struct tty *tp = fnuarttty(dev);
	return (*tp->t_linesw->l_write)(tp, uio, flag);
}
int fnuartpoll(dev_t dev, int events, struct lwp *l)
{
	struct tty *tp = fnuarttty(dev);
	return (*tp->t_linesw->l_poll)(tp, events, l);
}
int fnuartioctl(dev_t dev, u_long cmd, void *data, int flag, struct lwp *l)
{
	struct tty *tp = fnuarttty(dev);
	int error = (*tp->t_linesw->l_ioctl)(tp, cmd, data, flag, l);
	return error == EPASSTHROUGH ? ttioctl(tp, cmd, data, flag, l) : error;
}
static void start(struct tty *tp)
{
	struct fnuart_softc *sc = device_lookup_private(&fnuart_cd, minor(tp->t_dev));
	uintptr_t base = sc->base;
	int s = spltty();
	if (!(tp->t_state & (TS_BUSY | TS_TTSTOP | TS_TIMEOUT))) {
		tp->t_state |= TS_BUSY;
		/* Never dequeue a byte until hardware can accept it. */
		for (unsigned i = 0; i < 128 && tp->t_outq.c_cc; i++) {
			if (!(REG8(base) & TX_EMPTY)) break;
			REG8(base + 1) = getc(&tp->t_outq);
		}
		tp->t_state &= ~TS_BUSY;
		(void)ttypull(tp);
	}
	splx(s);
}
static void service(void *arg)
{
	struct fnuart_softc *sc = arg;
	struct tty *tp = sc->tty;
	uintptr_t base = sc->base;
	int s = spltty();
	if (!console_polling && (tp->t_state & TS_ISOPEN)) {
		for (unsigned i = 0; i < 256; i++) {
			if (REG8(base) & RX_EMPTY) break;
			int c = REG8(base + 1);
			if (tp->t_cflag & CREAD) (*tp->t_linesw->l_rint)(c, tp);
		}
		start(tp);
	}
	callout_schedule(&sc->poll, 1);
	splx(s);
}
