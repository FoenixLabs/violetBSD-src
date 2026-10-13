/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _A2560ME_DISKLABEL_H_
#define _A2560ME_DISKLABEL_H_
#define NUMBOOT 0
#define MAXPARTITIONS 16
#define RAW_PART 3
#define LABELUSESMBR 1
#define LABELSECTOR 1
#define LABELOFFSET 0
#if HAVE_NBTOOL_CONFIG_H
#include <nbinclude/sys/bootblock.h>
#else
#include <sys/bootblock.h>
#endif
struct cpu_disklabel { struct mbr_partition mbrparts[MBR_PART_COUNT]; };
#endif
