/* SPDX-License-Identifier: BSD-2-Clause */
/* A2560Me's default StaticColor visual uses the Vicky xterm-256 palette. */
#include <stdint.h>
#include "xf86.h"
#include "colormapst.h"
#include "micmap.h"

Bool WsfbCreateDefColormap(ScreenPtr);

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

static Bool
create_colormap(ColormapPtr map)
{
	unsigned i;

	if (!miInitializeColormap(map))
		return FALSE;
	if (map->pVisual->class != StaticColor ||
	    map->pVisual->ColormapEntries != 256)
		return TRUE;
	for (i = 0; i < 256; i++) {
		uint32_t rgb = xterm_color(i);
		map->red[i].co.local.red = ((rgb >> 16) & 255) * 257;
		map->red[i].co.local.green = ((rgb >> 8) & 255) * 257;
		map->red[i].co.local.blue = (rgb & 255) * 257;
	}
	return TRUE;
}

Bool
WsfbCreateDefColormap(ScreenPtr screen)
{
	/* fbScreenInit uses miInitializeColormap; retain it for other visuals. */
	if (screen->rootDepth == 8)
		screen->CreateColormap = create_colormap;
	return miCreateDefColormap(screen);
}
