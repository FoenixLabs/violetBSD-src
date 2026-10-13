/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/param.h>
#include <sys/device.h>
#include <sys/systm.h>
#include <machine/board.h>
static int mainbus_match(device_t p, cfdata_t cf, void *aux)
{
	return 1;
}
static int mainbus_search(device_t p, cfdata_t cf, const int *loc, void *aux)
{
	if (config_probe(p, cf, NULL))
		config_attach(p, cf, NULL, NULL, CFARGS_NONE);
	return 0;
}
static void mainbus_attach(device_t p, device_t self, void *aux)
{
	aprint_normal("\n");
	a2560me_led_init();
	config_search(self, NULL, CFARGS(.search = mainbus_search));
}
CFATTACH_DECL_NEW(mainbus, 0, mainbus_match, mainbus_attach, NULL, NULL);
