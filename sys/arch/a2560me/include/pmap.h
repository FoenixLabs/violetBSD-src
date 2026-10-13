/* NetBSD/a2560me: SRAM page tables, supervisor-only XIP and device windows. */
#ifndef _A2560ME_PMAP_H_
#define _A2560ME_PMAP_H_
#include <m68k/mmu_40.h>
#ifdef _KERNEL_OPT
#include "opt_a2560me.h"
#endif
/* TT matching granularity is 16 MiB. Only the populated subranges are used. */
#define MACHINE_DTT0 (0xfe000000 | TTR40_E | TTR40_SUPER | PTE40_CM_NC_SER)
#ifndef A2560ME_RAM
#define MACHINE_DTT1 (0xff000000 | TTR40_E | TTR40_SUPER | PTE_WP)
#define MACHINE_ITT1 MACHINE_DTT1
/* The flash text is mapped by TT; the page-table image starts with SRAM data. */
#define PMAP_BOOTSTRAP_TEXT_START 0x00010000
#define PMAP_BOOTSTRAP_TEXT_END 0x00010000
#else
#define MACHINE_DTT1 0
#define MACHINE_ITT1 0
#endif /* flash XIP */
#include <m68k/pmap_68k.h>
#ifndef _LOCORE
#ifdef A2560ME_PV_CHECK
#define PMAP_MD_PV_CHECK
#endif
#if defined(A2560ME_DDR_NOCACHE)
#ifndef A2560ME_DDR64
#error A2560ME_DDR_NOCACHE requires A2560ME_DDR64
#endif
/* Opt-in comparison: physical DDR only, never SRAM or MMIO. */
#define PMAP_MD_PA_NOCACHE(pa) \
    ((pa) >= 0x00800000U && (pa) < 0x04800000U)
#endif
#define PMAP_MACHINE_CHECK_BOOTSTRAP_ALLOCATIONS
void pmap_machine_check_bootstrap_allocations(paddr_t, paddr_t);
#endif
#endif
