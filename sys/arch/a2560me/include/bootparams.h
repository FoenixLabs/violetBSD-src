/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _A2560ME_BOOTPARAMS_H_
#define _A2560ME_BOOTPARAMS_H_

#include <sys/types.h>

/* Manufacturer LVGL boot handover, 2026-10-14, section 4. Native big-endian. */
#define A2560ME_BOOT_HANDOVER 0x007fbf00U
#define A2560ME_BOOT_MAGIC 0x4c564254U
#define A2560ME_BOOT_NORMAL 1U
#define A2560ME_BOOT_EXPAND 2U
#define A2560ME_BOOT_SINGLE 3U

/* Read before SRAM is handed to UVM. Strings are not needed or dereferenced.
 * Clear even an invalid record so retained SRAM cannot replay the request. */
static inline unsigned
a2560me_boot_consume(volatile uint32_t *record)
{
	uint32_t magic = record[0];
	uint32_t entry = record[1];
	uint32_t option = record[2];
	uint32_t flash = record[3];
	uint32_t check = record[24];

	record[0] = 0;
	if (magic == A2560ME_BOOT_MAGIC &&
	    check == (magic ^ entry ^ option ^ flash) &&
	    option >= A2560ME_BOOT_NORMAL && option <= A2560ME_BOOT_SINGLE)
		return option;
	return A2560ME_BOOT_NORMAL;
}
#endif
