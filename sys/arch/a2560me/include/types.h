/*	$NetBSD: types.h,v 1.2 2026/07/21 14:35:28 thorpej Exp $	*/

#ifndef _A2560ME_TYPES_H_
#define	_A2560ME_TYPES_H_

#include <m68k/types.h>

#if defined(_KERNEL) && !defined(_MODULE)
#define __HAVE_M68K_INTR_VECTORED
#define	__HAVE_DEVICE_REGISTER
#endif

#endif /* _A2560ME_TYPES_H_ */
