/* SPDX-License-Identifier: BSD-2-Clause */
/* Vicky text consoles plus the DDR-backed 640x480 indexed GUI bitmap. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/kmem.h>
#include <uvm/uvm_extern.h>
#include <machine/intr.h>
#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <machine/board.h>
#include "opt_a2560me.h"
extern bool a2560me_video_mirror;
#define VICKY_COLS 160
#define VICKY_CELLS (VICKY_COLS * 64)
#define VICKY_SCREENS 4
struct vicky_screen {
	uint8_t chars[VICKY_CELLS];
	uint8_t attrs[VICKY_CELLS];
	uint32_t cursor_pos, cursor_ctl;
	bool graphics;
};
/* The console must exist before the memory allocator is available. */
static struct vicky_screen console_screen;
static struct vicky_screen *active;
static unsigned allocated;
static bool graphics_active;
static void apply_display_mode(void);

static void cursor(void *v, int on, int row, int col)
{
	struct vicky_screen *s = v;
	int priority = splhigh();
	s->cursor_pos = ((uint32_t)row << 16) | col;
	s->cursor_ctl = 0xf0b10000 | (on ? 1 : 0);
	if (s == active && !graphics_active) {
		REG32(0xfec80014) = s->cursor_pos;
		REG32(0xfec80010) = s->cursor_ctl;
	}
	splx(priority);
}
static int mapchar(void *v, int c, unsigned *out)
{
	return a2560me_console_mapchar(c, out);
}
static void putchar_cell(void *v, int row, int col, unsigned c, long attr)
{
	struct vicky_screen *s = v;
	unsigned i = row * VICKY_COLS + col;
	int priority = splhigh();
	if (s == active) {
		REG8(0xfeca0000 + i) = c;
		REG8(0xfeca8000 + i) = attr;
	} else {
		s->chars[i] = c;
		s->attrs[i] = attr;
	}
	splx(priority);
}
static void copycells(void *v, unsigned src, unsigned dst, unsigned n)
{
	struct vicky_screen *s = v;
	int priority = splhigh();
	if (s != active) {
		memmove(s->chars + dst, s->chars + src, n);
		memmove(s->attrs + dst, s->attrs + src, n);
	} else if (dst > src) {
		while (n) {
			--n;
			REG8(0xfeca0000 + dst + n) = REG8(0xfeca0000 + src + n);
			REG8(0xfeca8000 + dst + n) = REG8(0xfeca8000 + src + n);
		}
	} else {
		for (unsigned i = 0; i < n; i++) {
			REG8(0xfeca0000 + dst + i) = REG8(0xfeca0000 + src + i);
			REG8(0xfeca8000 + dst + i) = REG8(0xfeca8000 + src + i);
		}
	}
	splx(priority);
}
static void copycols(void *v, int r, int src, int dst, int n)
{
	copycells(v, r * 160 + src, r * 160 + dst, n);
}
static void erasecols(void *v, int r, int col, int n, long attr)
{
	while (n--)
		putchar_cell(v, r, col++, ' ', attr);
}
static void copyrows(void *v, int src, int dst, int n)
{
	copycells(v, src * 160, dst * 160, n * 160);
}
static void eraserows(void *v, int r, int n, long attr)
{
	while (n--)
		erasecols(v, r++, 0, 160, attr);
}
static int allocattr(void *v, int fg, int bg, int flags, long *attr)
{
	if (flags & (WSATTR_BLINK | WSATTR_UNDERLINE))
		return EINVAL;
	if (!(flags & WSATTR_WSCOLORS)) {
		fg = 7;
		bg = 0;
	}
	if (flags & WSATTR_HILIT)
		fg |= 8;
	if (flags & WSATTR_REVERSE) {
		int t = fg;
		fg = bg;
		bg = t;
	}
	*attr = ((fg & 15) << 4) | (bg & 15);
	return 0;
}
static const struct wsdisplay_emulops ops = {.cursor = cursor,
					     .mapchar = mapchar,
					     .putchar = putchar_cell,
					     .copycols = copycols,
					     .erasecols = erasecols,
					     .copyrows = copyrows,
					     .eraserows = eraserows,
					     .allocattr = allocattr};
static struct wsscreen_descr screen = {
    .name = "std",
    .ncols = 160,
    .nrows = 60,
    .textops = &ops,
    .fontwidth = 8,
    .fontheight = 16,
    .capabilities = WSSCREEN_WSCOLORS | WSSCREEN_REVERSE | WSSCREEN_HILIT};
static const struct wsscreen_descr *screens[] = {&screen};
static const struct wsscreen_list list = {1, screens};
#ifdef A2560ME_HWCURSOR_TEST
#include "vicky_cursor_test.h"
#endif
#include "vicky_bitmap.h"
static int alloc_screen(void *v, const struct wsscreen_descr *d, void **cookie,
			int *x, int *y, long *attr)
{
#ifdef A2560ME_NO_VCONS
	/* Keep the boot display even with an older /etc/wscons.conf on disk. */
	return ENODEV;
#else
	struct vicky_screen *s;
	if (allocated == VICKY_SCREENS)
		return ENOMEM;
	s = kmem_zalloc(sizeof(*s), KM_SLEEP);
	memset(s->chars, ' ', sizeof(s->chars));
	memset(s->attrs, 0x70, sizeof(s->attrs));
	s->cursor_ctl = 0xf0b10000;
	allocated++;
	*cookie = s;
	*x = *y = 0;
	*attr = 0x70;
	return 0;
#endif
}
static void free_screen(void *v, void *cookie)
{
	struct vicky_screen *s = cookie;
	int priority;
	KASSERT(s != &console_screen);
	priority = splhigh();
	if (active == s) {
		REG32(0xfec80010) = 0xf0b10000;
		active = NULL;
		apply_display_mode();
	}
	allocated--;
	splx(priority);
	kmem_free(s, sizeof(*s));
}
static int show_screen(void *v, void *cookie, int wait,
		       void (*cb)(void *, int, int), void *arg)
{
	struct vicky_screen *s = cookie;
	int priority = splhigh();
	if (s != active) {
		unsigned cells = screen.nrows * VICKY_COLS;
		/* Save hardware, including text written by the early boot console. */
		if (active != NULL) {
			/* Graphics hides the hardware cursor; retain its text state. */
			if (!graphics_active) {
				active->cursor_pos = REG32(0xfec80014);
				active->cursor_ctl = REG32(0xfec80010);
			}
			for (unsigned i = 0; i < cells; i++) {
				active->chars[i] = REG8(0xfeca0000 + i);
				active->attrs[i] = REG8(0xfeca8000 + i);
			}
		}
		/* Serial mirroring must not overwrite a virtual terminal. */
		a2560me_video_mirror = false;
		REG32(0xfec80010) = 0xf0b10000;
		for (unsigned i = 0; i < cells; i++) {
			REG8(0xfeca0000 + i) = s->chars[i];
			REG8(0xfeca8000 + i) = s->attrs[i];
		}
		active = s;
		REG32(0xfec80014) = s->cursor_pos;
		REG32(0xfec80010) = s->cursor_ctl;
	}
	apply_display_mode();
	splx(priority);
	/* Synchronous completion: wsdisplay does not need its callback invoked. */
	return 0;
}
static const struct wsdisplay_accessops accessops = {
    .ioctl = ioctl_display,
    .mmap = mmap_display,
    .alloc_screen = alloc_screen,
    .free_screen = free_screen,
    .show_screen = show_screen};
void a2560me_wscons_init(void);
void a2560me_wscons_init(void)
{
	screen.nrows = (REG32(0xfec80000) & 0x100) ? 64 : 60;
#ifdef A2560ME_WSCONS
	allocated = 1;
	active = &console_screen;
	wsdisplay_cnattach(&screen, &console_screen, 0, 0, 0x70);
#endif
}
static int match(device_t p, cfdata_t cf, void *aux)
{
	return 1;
}
static void attach(device_t p, device_t self, void *aux)
{
	struct wsemuldisplaydev_attach_args a = {
	    .scrdata = &list, .accessops = &accessops, .accesscookie = &screen};
#ifdef A2560ME_WSCONS
	a.console = 1;
#endif
	bitmap_init();
	aprint_normal(": legacy text, 160x%d, 8x16 font%s\n", screen.nrows,
	    bitmap_ready ? "; GUI 640x480x8 in DDR" : "");
	config_found(self, &a, wsemuldisplaydevprint, CFARGS_NONE);
}
CFATTACH_DECL_NEW(vicky, 0, match, attach, NULL, NULL);
