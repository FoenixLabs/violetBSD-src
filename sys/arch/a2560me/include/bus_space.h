/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _A2560ME_BUS_SPACE_H_
#define _A2560ME_BUS_SPACE_H_
/* Preserve the simple bus ABI, specializing the LAN's numeric and packet accesses. */
#define bus_space_read_4 a2560me_native_read_4
#include <m68k/bus_space_simple.h>
#undef bus_space_read_4
#undef bus_space_write_4
#undef bus_space_read_multi_4
#undef bus_space_write_multi_4
uint32_t a2560me_lan_read(bus_addr_t);
void a2560me_lan_write(bus_addr_t, uint32_t);
void a2560me_lan_read_multi(bus_addr_t, uint32_t *, bus_size_t);
void a2560me_lan_write_multi(bus_addr_t, const uint32_t *, bus_size_t);
static inline bool a2560me_lan_addr(bus_addr_t a)
{
	return a >= 0xfec00600 && a < 0xfec00800;
}
static inline uint32_t bus_space_read_4(bus_space_tag_t t, bus_space_handle_t h,
					bus_size_t o)
{
	return a2560me_lan_addr(h + o) ? a2560me_lan_read(h + o)
				       : a2560me_native_read_4(t, h, o);
}
static inline void bus_space_write_4(bus_space_tag_t t, bus_space_handle_t h,
				     bus_size_t o, uint32_t v)
{
	if (a2560me_lan_addr(h + o))
		a2560me_lan_write(h + o, v);
	else
		*(volatile uint32_t *)(h + o) = v;
}
static inline void bus_space_read_multi_4(bus_space_tag_t t,
					  bus_space_handle_t h, bus_size_t o,
					  uint32_t *p, bus_size_t n)
{
	if (a2560me_lan_addr(h + o)) {
		a2560me_lan_read_multi(h + o, p, n);
		return;
	}
	while (n--)
		*p++ = bus_space_read_4(t, h, o);
}
static inline void bus_space_write_multi_4(bus_space_tag_t t,
					   bus_space_handle_t h, bus_size_t o,
					   const uint32_t *p, bus_size_t n)
{
	if (a2560me_lan_addr(h + o)) {
		a2560me_lan_write_multi(h + o, p, n);
		return;
	}
	while (n--)
		bus_space_write_4(t, h, o, *p++);
}
#endif
