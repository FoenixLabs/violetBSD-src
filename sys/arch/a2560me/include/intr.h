/*	$NetBSD: intr.h,v 1.3 2026/04/04 00:55:45 thorpej Exp $	*/

/*-
 * Copyright (c) 2023 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _A2560ME_INTR_H_
#define _A2560ME_INTR_H_

#include <machine/psl.h>

#define	IPL_NONE	0
#define	IPL_SOFTCLOCK	1	/* clock software interrupts */
#define	IPL_SOFTBIO	1	/* block software interrupts */
#define	IPL_SOFTNET	1	/* network software interrupts */
#define	IPL_SOFTSERIAL	1	/* serial software interrupts */
#define	IPL_VM		5
#define	IPL_SCHED	6
#define	IPL_HIGH	7
#define	NIPL		8

#if defined(_KERNEL) || defined(_KMEMUSER)
typedef struct {
	uint16_t _psl;
} ipl_cookie_t;
#endif

#ifdef _KERNEL
#define spl0()			_spl0()
#define splsoftclock()		splraise1()
#define splsoftbio()		splraise1()
#define splsoftnet()		splraise1()
#define splsoftserial()		splraise1()
#define splvm()			spl7()
#define splsched()		spl7()
#define splhigh()		spl7()

#ifndef _LOCORE

extern const uint16_t ipl2psl_table[NIPL];

typedef int ipl_t;

static __inline ipl_cookie_t
makeiplcookie(ipl_t ipl)
{

	return (ipl_cookie_t){._psl = ipl2psl_table[ipl]};
}

static __inline int
splraiseipl(ipl_cookie_t icookie)
{

	return _splraise(icookie._psl);
}

static __inline void
splx(int sr)
{

	__asm volatile("movw %0,%%sr" : : "di" (sr));
}

#define	INTR_STRING_BUFSIZE	64

void	intr_init(void);
void	*intr_establish(int (*)(void *), void *, int, int, int);
void	intr_disestablish(void *);
const char *intr_string(void *, char *, size_t);

struct device;			/* XXX */
void	intr_register_pic(struct device *, int);

/* Flags for intr_establish() */
#define	INTR_F_MPSAFE		__BIT(0)

#ifdef _A2560ME_INTR_PRIVATE
#include <sys/cpu.h>
#include <sys/device.h>

void	m68k_intr_autovec(struct clockframe);
#endif /* _A2560ME_INTR_PRIVATE */

#endif /* !_LOCORE */
#endif /* _KERNEL */

#endif /* _A2560ME_INTR_H_ */
