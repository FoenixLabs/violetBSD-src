/* SPDX-License-Identifier: BSD-2-Clause */
/* Opt-in, diskless kernel demonstration; normal kernels never include this. */
#include <dev/wsfont/bold8x16.h>

static void
demo_text(unsigned x, unsigned y, const char *s, uint8_t color)
{
	const uint8_t *font = bold8x16.data;
	for (; *s != '\0'; s++, x += 8) {
		unsigned ch = (unsigned char)*s;
		if (ch < bold8x16.firstchar ||
		    ch >= bold8x16.firstchar + bold8x16.numchars)
			continue;
		for (unsigned row = 0; row < 16; row++) {
			uint8_t bits = font[(ch - bold8x16.firstchar) * 16 + row];
			for (unsigned col = 0; col < 8; col++)
				if (bits & (0x80 >> col))
					REG8(demo_pixels + (y + row) * 640 + x + col) = color;
		}
	}
}

void a2560me_gui_demo(void);
void
a2560me_gui_demo(void)
{
	static const char hex[] = "0123456789ABCDEF";
	unsigned mismatches = 0;
	if (!bitmap_ready) {
		printf("GUI-DEMO: unavailable (build=%u, system=%#x); no mount\n",
		    REG32(0xfec0003c), REG32(A2560ME_SYSTEM_CTRL));
		return;
	}
	memset((void *)demo_pixels, 234, A2560ME_GUI_FB_SIZE);
	demo_text(16, 8, "violetBSD / A2560Me - GUI BITMAP", 15);
	demo_text(16, 30, "640x480 / 8-bit indexed / DDR / xterm-256", 51);
	for (unsigned i = 0; i < 256; i++) {
		unsigned x = 16 + (i % 16) * 38;
		unsigned y = 60 + (i / 16) * 24;
		char label[3] = {hex[i >> 4], hex[i & 15], '\0'};
		for (unsigned dy = 1; dy < 23; dy++)
			for (unsigned dx = 1; dx < 37; dx++)
				REG8(demo_pixels + (y + dy) * 640 + x + dx) = i;
		/* Contrast based on the actual palette's approximate luminance. */
		unsigned light = 299 * bitmap_cmap[0][i] +
		    587 * bitmap_cmap[1][i] + 114 * bitmap_cmap[2][i];
		demo_text(x + 11, y + 4, label, light >= 128000 ? 0 : 15);
		if (REG8(demo_pixels + (y + 1) * 640 + x + 1) != i)
			mismatches++;
	}
	demo_text(16, 456, "00-FF palette indices / diskless demo / reset to restart", 250);
	active = &console_screen;
	active->graphics = true;
	apply_display_mode();
	delay(100000);
	printf("GUI-DEMO: 640x480x8 xterm-256, DDR=%#x stride=640\n",
	    A2560ME_GUI_FB_BASE);
	printf("GUI-DEMO: tile samples mismatched=%u; ctrl=%#x addr=%#x status=%#x\n",
	    mismatches, REG32(bitmap_regs), REG32(bitmap_regs + 4),
	    REG32(bitmap_regs + 0x14));
	printf("GUI-DEMO: palette displayed; stopping before root selection/mount\n");
}
