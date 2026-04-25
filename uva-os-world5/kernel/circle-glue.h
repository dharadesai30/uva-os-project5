//
// types.h
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2014-2018  R. Stange <rsta2@o2online.de>
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#ifndef _circle_types_h
#define _circle_types_h

#include <assert.h>

typedef unsigned char		u8;
typedef unsigned short		u16;
typedef unsigned int		u32;

typedef signed char		s8;
typedef signed short		s16;
typedef signed int		s32;

#if AARCH == 32
typedef unsigned long long	u64;
typedef signed long long	s64;

typedef int			intptr;
typedef unsigned int		uintptr;

typedef unsigned int		size_t;
typedef int			ssize_t;
#else
typedef unsigned long		u64;
typedef signed long		s64;

typedef long			intptr;
typedef unsigned long		uintptr;

typedef unsigned long		size_t;
typedef long			ssize_t;
#endif

#ifdef __cplusplus
typedef bool		boolean;
#define FALSE		false
#define TRUE		true
#else
typedef char		boolean;
#define FALSE		0
#define TRUE		1
#endif
_Static_assert (sizeof (boolean) == 1);


// synchironizes.h
#define PeripheralEntry()	((void) 0)	// ignored here
#define PeripheralExit()	((void) 0)

// macros.h
#define PACKED		__attribute__ ((packed))
#define	ALIGN(n)	__attribute__ ((aligned (n)))
#define NORETURN	__attribute__ ((noreturn))
#define NOOPT		__attribute__ ((optimize (0)))
#define STDOPT		__attribute__ ((optimize (2)))
#define MAXOPT		__attribute__ ((optimize (3)))
#define WEAK		__attribute__ ((weak))

// pa->va
#define VA_START 			0xffff000000000000
#define VA2PA(x) ((unsigned long)x - VA_START)          // kernel va to pa
#define PA2VA(x) ((void *)((unsigned long)x + VA_START))  // pa to kernel va

#define write32(a,v) put32va(a,(v))
#define read32(a)   get32va((a))

// cache ops, util.S
void __asm_invalidate_dcache_range(void* start_addr, void* end_addr);
void __asm_flush_dcache_range(void* start_addr, void* end_addr);

static inline void CleanAndInvalidateDataCacheRange (u64 nAddress, u64 nLength) {
    __asm_invalidate_dcache_range(PA2VA(nAddress), PA2VA(nAddress+nLength-1));
}

// machineinfo.c
unsigned CMachineInfo_AllocateDMAChannel(unsigned nChannel);
void CMachineInfo_FreeDMAChannel (unsigned nChannel);
boolean CMachineInfo_ArePWMChannelsSwapped (void);
unsigned CMachineInfo_GetGPIOClockSourceRate(unsigned nSourceId);

// Circle machineinfo.cpp. preserve the logic here for future rpi4 porting
	// DMA channel resource management
#if RASPPI <= 3
#define DMA_CHANNEL_MAX		12			// channels 0-12 are supported
#else
#define DMA_CHANNEL_MAX		7			// legacy channels 0-7 are supported
#define DMA_CHANNEL_EXT_MIN	11			// DMA4 channels 11-14 are supported
#define DMA_CHANNEL_EXT_MAX	14
#endif
#define DMA_CHANNEL__MASK	0x0F			// explicit channel number
#define DMA_CHANNEL_NONE	0x80			// returned if no channel available
#define DMA_CHANNEL_NORMAL	0x81			// normal DMA engine requested
#define DMA_CHANNEL_LITE	0x82			// lite (or normal) DMA engine requested
#if RASPPI >= 4
#define DMA_CHANNEL_EXTENDED	0x83			// "large address" DMA4 engine requested
#endif

#endif
