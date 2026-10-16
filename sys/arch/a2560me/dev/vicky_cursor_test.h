/* SPDX-License-Identifier: BSD-2-Clause */
/* Opt-in pointer tests. Build70 reference rev1.2 p52. */
#include <sys/callout.h>
#include <sys/kernel.h>
#include <sys/kauth.h>
#include <sys/file.h>

static struct callout cursor_test_timer;
static bool cursor_test_initialized, cursor_test_running;
static uint16_t cursor_test_control;

static void
cursor_test_finish(void *arg)
{
	int s = splhigh();
	REG16(0xfec80c00) = cursor_test_control;
	cursor_test_running = false;
	splx(s);
}

static int
cursor_test_start(struct lwp *l, uint32_t *position)
{
	/* A white arrow with an opaque black outline, transparent elsewhere. */
	static const uint16_t outline[16] = {
		0xc000,0xe000,0xf000,0xf800,0xfc00,0xfe00,0xff00,0xff80,
		0xffc0,0xffc0,0xfe00,0xef00,0xcf00,0x0780,0x0780,0x0300
	};
	static const uint16_t fill[16] = {
		0,0x4000,0x6000,0x7000,0x7800,0x7c00,0x7e00,0x7f00,
		0x7f80,0x7c00,0x6c00,0x4600,0x0600,0x0300,0x0300,0
	};
	int error, s;
	uint16_t control;

	error = kauth_authorize_generic(l->l_cred, KAUTH_GENERIC_ISSUSER, NULL);
	if (error) return error;
	if (!cursor_test_initialized) {
		callout_init(&cursor_test_timer, 0);
		callout_setfunc(&cursor_test_timer, cursor_test_finish, NULL);
		cursor_test_initialized = true;
	}
	s = splhigh();
	control = REG16(0xfec80c00);
	if (cursor_test_running || (control & 1)) {
		splx(s);
		return EBUSY;
	}
	cursor_test_control = control;
	/* Images are write-only. Touch only the inactive image; never read it. */
	unsigned image = ((cursor_test_control >> 1) & 1) ^ 1;
	for (unsigned y = 0; y < 16; y++) {
		for (unsigned x = 0; x < 16; x++) {
			uint32_t pixel = 0;
			if (outline[y] & (0x8000 >> x))
				pixel = (fill[y] & (0x8000 >> x)) ?
				    0xffffffff : 0xff000000;
			REG32(0xfec80400 + image * 0x400 + (y * 16 + x) * 4) = pixel;
		}
	}
	*position = REG32(0xfec80c04);
	REG16(0xfec80c00) = (cursor_test_control & ~3u) | (image << 1) | 1;
	cursor_test_running = true;
	callout_schedule(&cursor_test_timer, 15 * hz);
	splx(s);
	return 0;
}

/* Read-only snapshot: no enable, packet, or position writes. */
static int
cursor_test_position(struct lwp *l, uint32_t *position)
{
	int error = kauth_authorize_generic(l->l_cred,
	    KAUTH_GENERIC_ISSUSER, NULL);
	if (error) return error;
	*position = REG32(0xfec80c04);
	return 0;
}
