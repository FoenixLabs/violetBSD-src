/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cpu.h>
#include <machine/vectors.h>
#include <machine/board.h>
extern char intrstub_vectored[];
static int (*handlers[48])(void *);
static void *handler_args[48];
int a2560me_intr_establish(unsigned, int (*)(void *), void *);
int a2560me_intr_establish(unsigned source, int (*fn)(void *), void *arg)
{
	if (source < 8 || source >= 48 || source == 0x19 || handlers[source])
		return EINVAL;
	int s = splhigh();
	handlers[source] = fn;
	handler_args[source] = arg;
	vec_set_entry(source + 56, intrstub_vectored);
	REG16(0xfec00100 + (source / 16) * 2) = 1u << (source % 16);
	REG16(0xfec00118 + (source / 16) * 2) &= ~(1u << (source % 16));
	splx(s);
	return 0;
}
void m68k_intr_vectored(struct clockframe);
void m68k_intr_autovec(struct clockframe);
void intr_init(void)
{
	/* MCP timer 0: logical source 0x19, CPU vector 81. */
	vec_set_entry(81, intrstub_vectored);
}
void m68k_intr_vectored(struct clockframe f)
{
	unsigned v = (f.cf_vo & 0xfff) / 4;
	if (v == 81) {
		REG16(0xfec00102) = 0x0200;
		a2560me_clock_intr(&f);
	} else if (v >= 64 && v < 104) {
		unsigned source = v - 56;
		REG16(0xfec00100 + (source / 16) * 2) = 1u << (source % 16);
		if (handlers[source])
			handlers[source](handler_args[source]);
	}
}
void m68k_intr_autovec(struct clockframe f)
{
	/* Video sources remain masked until a driver owns them. */
	REG16(0xfec00100) = 0x000f;
}
bool cpu_intr_p(void)
{
	return intr_depth != 0;
}
volatile unsigned int intr_depth;
