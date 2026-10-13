/* SPDX-License-Identifier: BSD-2-Clause */
/* Private implementation included by vicky.c. Hardware reference rev0.2 pp62-65. */
#define GUI_PHYS 0xfc000000U
#define GUI_MASTER 0xfec80000U
#define GUI_BORDER 0xfec80004U
#define GUI_PALETTE 0x400
static vaddr_t bitmap_regs;
#ifdef A2560ME_GUI_DEMO
static vaddr_t demo_pixels;
#endif
static bool bitmap_ready;
static uint32_t text_master, text_border;
static uint8_t bitmap_cmap[3][256];

#if defined(A2560ME_DDR_FULL) || defined(A2560ME_DDR64)
static uint32_t
xterm_color(unsigned i)
{
	static const uint32_t base[16] = {
		0x000000, 0xcd0000, 0x00cd00, 0xcdcd00,
		0x0000ee, 0xcd00cd, 0x00cdcd, 0xe5e5e5,
		0x7f7f7f, 0xff0000, 0x00ff00, 0xffff00,
		0x5c5cff, 0xff00ff, 0x00ffff, 0xffffff
	};
	static const unsigned level[6] = {0, 95, 135, 175, 215, 255};
	if (i < 16)
		return base[i];
	if (i < 232) {
		i -= 16;
		return (level[i / 36] << 16) | (level[i / 6 % 6] << 8) |
		    level[i % 6];
	}
	return (8 + 10 * (i - 232)) * 0x010101;
}

#endif

static void
palette_write(unsigned i)
{
	REG32(bitmap_regs + GUI_PALETTE + 4 * i) =
	    ((uint32_t)bitmap_cmap[0][i] << 16) |
	    ((uint32_t)bitmap_cmap[1][i] << 8) | bitmap_cmap[2][i];
}

static void
apply_display_mode(void)
{
	bool want = active != NULL && active->graphics && bitmap_ready;
	if (want == graphics_active) {
		if (want) REG32(0xfec80010) = 0xf0b10000;
		return;
	}
	if (want) {
		text_master = REG32(GUI_MASTER) & 0x1fffffff;
		text_border = REG32(GUI_BORDER);
		a2560me_video_mirror = false;
		REG32(0xfec80010) = 0xf0b10000;
		REG32(GUI_BORDER) = 0;
		REG32(bitmap_regs + 4) = A2560ME_GUI_FB_BASE;
		REG32(bitmap_regs + 0x0c) = 640;
		REG32(bitmap_regs) = 3; /* ENABLE | 640-pixel 8bpp mode */
		/* 1280x960 timing; GUI only; software-controlled gamma bypass.
		 * Do not set PIXDIV_GUI: the bitmap engine doubles internally. */
		REG32(GUI_MASTER) = 4 | (1U << 17);
	} else {
		REG32(bitmap_regs) = 0;
		REG32(GUI_BORDER) = text_border;
		REG32(GUI_MASTER) = text_master;
		if (active != NULL) {
			REG32(0xfec80014) = active->cursor_pos;
			REG32(0xfec80010) = active->cursor_ctl;
		}
	}
	graphics_active = want;
}

static int
bitmap_cmap_io(struct wsdisplay_cmap *cm, bool put)
{
	uint8_t rgb[3][256];
	unsigned i = cm->index, n = cm->count;
	int e, priority;
	if (!bitmap_ready)
		return ENODEV;
	if (i >= 256 || n > 256 - i)
		return EINVAL;
	if (n == 0)
		return 0;
	if (put) {
		/* Complete all copyins before changing either the shadow or hardware. */
		if ((e = copyin(cm->red, rgb[0], n)) ||
		    (e = copyin(cm->green, rgb[1], n)) ||
		    (e = copyin(cm->blue, rgb[2], n)))
			return e;
		priority = splhigh();
		for (unsigned c = 0; c < 3; c++)
			memcpy(&bitmap_cmap[c][i], rgb[c], n);
		for (unsigned j = i; j < i + n; j++)
			palette_write(j);
		splx(priority);
		return 0;
	}
	priority = splhigh();
	for (unsigned c = 0; c < 3; c++)
		memcpy(rgb[c], &bitmap_cmap[c][i], n);
	splx(priority);
	if ((e = copyout(rgb[0], cm->red, n)) ||
	    (e = copyout(rgb[1], cm->green, n)))
		return e;
	return copyout(rgb[2], cm->blue, n);
}

static int
ioctl_display(void *v, void *vs, u_long cmd, void *data, int flag,
    struct lwp *l)
{
	struct vicky_screen *s = vs;
	int priority;
	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = bitmap_ready ? WSDISPLAY_TYPE_GENFB : WSDISPLAY_TYPE_UNKNOWN;
		return 0;
	case WSDISPLAYIO_GINFO: {
		struct wsdisplay_fbinfo *f = data;
		if (!bitmap_ready) return ENODEV;
		memset(f, 0, sizeof(*f));
		f->width = 640; f->height = 480; f->depth = 8; f->cmsize = 256;
		return 0;
	}
	case WSDISPLAYIO_GET_FBINFO: {
		struct wsdisplayio_fbinfo *f = data;
		if (!bitmap_ready) return ENODEV;
		memset(f, 0, sizeof(*f));
		f->fbi_fbsize = A2560ME_GUI_FB_SIZE;
		f->fbi_width = 640; f->fbi_height = 480; f->fbi_stride = 640;
		f->fbi_bitsperpixel = 8; f->fbi_pixeltype = WSFB_CI;
		f->fbi_subtype.fbi_cmapinfo.cmap_entries = 256;
		f->fbi_flags = WSFB_VRAM_IS_RAM;
		return 0;
	}
	case WSDISPLAYIO_LINEBYTES:
		if (!bitmap_ready) return ENODEV;
		*(u_int *)data = 640;
		return 0;
	case WSDISPLAYIO_GETCMAP:
	case WSDISPLAYIO_PUTCMAP:
		return bitmap_cmap_io(data, cmd == WSDISPLAYIO_PUTCMAP);
	case WSDISPLAYIO_SMODE: {
		unsigned mode = *(u_int *)data;
		if (s == NULL) return EINVAL;
		if (mode != WSDISPLAYIO_MODE_EMUL && mode != WSDISPLAYIO_MODE_MAPPED &&
		    mode != WSDISPLAYIO_MODE_DUMBFB) return EINVAL;
		if (mode != WSDISPLAYIO_MODE_EMUL &&
		    (!bitmap_ready || (REG32(A2560ME_SYSTEM_CTRL) & 0x40)))
			return ENODEV;
		priority = splhigh();
		s->graphics = mode != WSDISPLAYIO_MODE_EMUL;
		if (s == active) apply_display_mode();
		splx(priority);
		return 0;
	}
	}
	return EPASSTHROUGH;
}

static paddr_t
mmap_display(void *v, void *vs, off_t off, int prot)
{
	struct vicky_screen *s = vs;
	if (!bitmap_ready || s == NULL || !s->graphics || off < 0 ||
	    (off & PAGE_MASK) || off >= round_page(A2560ME_GUI_FB_SIZE) ||
	    (prot & VM_PROT_EXECUTE))
		return (paddr_t)-1;
	/* Reserved/unmanaged physical RAM: user pmap_enter forces cache inhibit. */
	return atop(A2560ME_GUI_FB_BASE + off);
}

static void
bitmap_init(void)
{
#if defined(A2560ME_DDR_FULL) || defined(A2560ME_DDR64)
	vaddr_t bitmap_kva;
	/* Use only the GUI register layout documented for Build28 and later. */
	if (REG32(0xfec0003c) < 28 || REG32(0xfec0003c) == 0xffffffff ||
	    (REG32(A2560ME_SYSTEM_CTRL) & 0x40))
		return;
	bitmap_regs = uvm_km_alloc(kernel_map, PAGE_SIZE, 0,
	    UVM_KMF_VAONLY | UVM_KMF_NOWAIT);
	if (bitmap_regs == 0) return;
	bitmap_kva = uvm_km_alloc(kernel_map, A2560ME_GUI_FB_RESERVE, 0,
	    UVM_KMF_VAONLY | UVM_KMF_NOWAIT);
	if (bitmap_kva == 0) {
		uvm_km_free(kernel_map, bitmap_regs, PAGE_SIZE, UVM_KMF_VAONLY);
		bitmap_regs = 0;
		return;
	}
	pmap_kenter_pa(bitmap_regs, GUI_PHYS, VM_PROT_READ | VM_PROT_WRITE, PMAP_NOCACHE);
	for (unsigned off = 0; off < A2560ME_GUI_FB_RESERVE; off += PAGE_SIZE)
		pmap_kenter_pa(bitmap_kva + off, A2560ME_GUI_FB_BASE + off,
		    VM_PROT_READ | VM_PROT_WRITE, PMAP_NOCACHE);
	pmap_update(pmap_kernel());
	REG32(bitmap_regs) = 0;
	memset((void *)bitmap_kva, 0, A2560ME_GUI_FB_RESERVE);
	for (unsigned i = 0; i < 256; i++) {
		uint32_t c = xterm_color(i);
		bitmap_cmap[0][i] = c >> 16;
		bitmap_cmap[1][i] = c >> 8;
		bitmap_cmap[2][i] = c;
		palette_write(i);
	}
	bitmap_ready = true;
#ifdef A2560ME_GUI_DEMO
	demo_pixels = bitmap_kva;
#endif
#endif
}

#ifdef A2560ME_GUI_DEMO
#include "vicky_demo.h"
#endif
