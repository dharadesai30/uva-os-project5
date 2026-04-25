#ifndef _MMU_H
#define _MMU_H

// -------------------------- page size constants  ------------------------------ //
#define PAGE_MASK			    0xfffffffffffff000
#define PAGE_SHIFT	 	        12
#define TABLE_SHIFT 		    9
#define SECTION_SHIFT		(PAGE_SHIFT + TABLE_SHIFT)
#define SUPERSECTION_SHIFT      (PAGE_SHIFT + 2*TABLE_SHIFT)      //30, 2^30 = 1GB

#define PAGE_SIZE   		(1 << PAGE_SHIFT)	
#define SECTION_SIZE		(1 << SECTION_SHIFT)	
#define SUPERSECTION_SIZE       (1 << SUPERSECTION_SHIFT)

#define PGROUNDUP(sz)  (((sz)+PAGE_SIZE-1) & ~(PAGE_SIZE-1))
#define PGROUNDDOWN(a) (((a)) & ~(PAGE_SIZE-1))

// ------------------ phys mem layout ----------------------------------------//
// region reserved for ramdisk. the actual ramdisk can be smaller.
// at this time, uncompressed ramdisk is linked into kernel image and used in place. 
// we therefore don't need additional space
// in the future, compressed ramdisk can be linked, and decompressed into the region below.
// then we can reserve, e.g. 4MB for it
#define RAMDISK_SIZE     0 // (4*1024*1024U)   
// #define LOW_MEMORY      (PHYS_BASE + 2 * SECTION_SIZE)  // used by kernel image
#define HIGH_MEMORY     (PHYS_BASE + PHYS_SIZE - RAMDISK_SIZE)
// #define PAGING_MEMORY           (HIGH_MEMORY - LOW_MEMORY)
//#define PAGING_PAGES            (PAGING_MEMORY/PAGE_SIZE)
#define MAX_PAGING_PAGES        ((PHYS_SIZE-RAMDISK_SIZE)/PAGE_SIZE)

#ifndef __ASSEMBLER__
	// must be page aligned
	_Static_assert(!(RAMDISK_SIZE & (PAGE_SIZE-1)));
	_Static_assert(!(PHYS_SIZE & (PAGE_SIZE-1)));
#endif 

// -------------- page table related ------------------------ // 
#define PTRS_PER_TABLE			(1 << TABLE_SHIFT)

#define PGD_SHIFT			PAGE_SHIFT + 3*TABLE_SHIFT
#define PUD_SHIFT			PAGE_SHIFT + 2*TABLE_SHIFT
#define PMD_SHIFT			PAGE_SHIFT + TABLE_SHIFT

// size of kern pgtable tree. cf linker-XXX.ld, mm.c
#define PG_DIR_SIZE			(4 * PAGE_SIZE)

#ifndef __ASSEMBLER__
// the virtual base address of the pgtables. Its actual value is set by the linker. 
//  cf the linker script (e.g. linker-qemu.ld)
// array size here appeases gcc (out-of-bound check)
extern unsigned long pg_dir[PTRS_PER_TABLE*4];
#endif 

// -------------- page table formats, defs ------------------------ //  
/* reference: 
For quick overview, cf: "ARM 100940_0100_en" armv8-a address translation.

For detailed def, cf: 
"Overview of the VMSAv8-64 address translation stages"
https://armv8-ref.codingbelief.com/en/chapter_d4/d42_5_overview_of_the_vmsav8-64_address_translation.html

"VMSAv8-64 translation table level 0 level 1 and level 2 descriptor formats"
https://armv8-ref.codingbelief.com/en/chapter_d4/d43_1_vmsav8-64_translation_table_descriptor_formats.html
*/

/* in arm's lingo, we are using 4KB granule (lowest pgtable entry). that means: 
    1GB per PUD entry, 2MB per PMD entry */
#define MM_TYPE_PAGE_TABLE		0x3
#define MM_TYPE_PAGE 			0x3
#define MM_TYPE_BLOCK			0x1         // block -- pointed by either a PUD entry or a PMD entry

/* ref:
Access flag (AF). "D4.4.1 Memory access control"
https://armv8-ref.codingbelief.com/en/chapter_d4/d44_1_memory_access_control.html#
*/
#define MM_AF		(0x1<<10)  // "indicates when a page or section of memory is accessed for the first time". if 0, exception. to be set by kernel

#define MM_SH_INNER (0x3<<8) // 00 Non-shareable 10b outer sharable 11b inner sharable

/* Access permission, bit7:6 in descriptor */
#define MM_AP_MASK              (0x3UL << 6)
#define MM_AP_EL1_RW            (0x0 << 6)      // EL0:no access; EL1/2/3: rw
#define MM_AP_RW                (0x1 << 6)      // EL0/1/2/3: rw
#define MM_AP_EL0_RO            (0x2 << 6)      // EL0:ro; EL1/2/3: rw
#define MM_AP_RO                (0x3 << 6)      // EL0/1/2/3: ro

/* "D4.3.3 Memory attribute fields in the VMSAv8-64 translation table format descriptors" */
#define MM_XN (1UL<<54)    // execute-never (Unprivileged execute never for EL1/EL0)
#define MM_PXN (1UL<<53)   // Privileged execute-never bit (Determines whether the region is executable at EL1)
#define MM_XN_MASK              (0x3UL << 53)

#define MM_ACCESS (MM_AF | MM_SH_INNER)

// extract perm bits from pte
#define PTE_TO_PERM(x)      (x & (unsigned long)(MM_AP_MASK | MM_XN_MASK))
// extract pa from pte. assuming pa within 32bits TODO: fix this for larger phys mem system...
#define PTE_TO_PA(x)        (x & (0xffffffff) & PAGE_MASK)
#ifndef __ASSEMBLER__    
    _Static_assert(PHYS_SIZE < 0xffffffff);
#endif

/*
 * Memory region attributes:
 *
 *   n = AttrIndx[2:0]
 *			        n	MAIR
 *   DEVICE_nGnRnE	000	00000000
 *   NORMAL_NC		001	01000100        Normal Memory, Outer Non-Cacheable; Inner Non-Cacheable (ldrx/strx will trigger mem exception)
 *   NORMAL 		002	11111111        Normal Memory, inner/outer shareable, writeback, r/w allocation
 *   NORMAL_WT			10111011
 *                                      https://forums.raspberrypi.com/viewtopic.php?t=207173
 *   https://developer.arm.com/documentation/ddi0500/d/system-control/aarch64-register-descriptions/memory-attribute-indirection-register--el1
 *   cf Google "armv8 MAIR_EL1" or "den0024"
 * 
 *   NORMAL_NC is only good for single core; exclusive ldr/str (e.g. for spinlocks) on it 
 *   will throw memory exception
 */
#define MT_DEVICE_nGnRnE 		0x0
#define MT_NORMAL_NC			0x1
#define MT_NORMAL   			0x2

// https://patchwork.kernel.org/project/linux-arm-kernel/patch/20191211184027.20130-5-catalin.marinas@arm.com/
#define MT_DEVICE_nGnRnE_FLAGS		0x00  // "equivalent to Strongly Ordered memory in the ARMv7 architecture".
#define MT_NORMAL_NC_FLAGS  		0x44
#define MT_NORMAL_WT_FLAGS  		0xbb
#define MT_NORMAL_FLAGS  		    0xff

#define MAIR_VALUE			((MT_DEVICE_nGnRnE_FLAGS << (8 * MT_DEVICE_nGnRnE))     \
                                    | (MT_NORMAL_NC_FLAGS << (8 * MT_NORMAL_NC))    \
                                    | (MT_NORMAL_FLAGS << (8 * MT_NORMAL)))

#define MMU_FLAGS	 		(MM_TYPE_BLOCK | (MT_NORMAL << 2) | MM_ACCESS)	    /* block (eg section) granuarlity, memory */
#define MMU_DEVICE_FLAGS	(MM_TYPE_BLOCK | (MT_DEVICE_nGnRnE << 2) | MM_ACCESS)	/* block (eg section) granuarlity, devices */
#define MMU_PTE_FLAGS		(MM_TYPE_PAGE | (MT_NORMAL << 2) | MM_ACCESS)	    /* need to be used with MP_AP_xxx, MM_XN */

// https://developer.arm.com/documentation/ddi0601/2024-03/AArch64-Registers/TCR-EL1--Translation-Control-Register--EL1-?lang=en
// cf: circle memory64.cpp, 
#define TCR_T0SZ			(64 - 48)       // The size offset of the memory region addressed by TTBR0_EL1.
#define TCR_T1SZ			((64 - 48) << 16) // ... for TTBR1_EL1
#define TCR_TG0_4K			(0 << 14)       // Granule size 4KB for the TTBR0_EL1 
#define TCR_TG1_4K			(2 << 30)       // Granule size 4KB for the TTBR1_EL1
// NB above: granule is for level3 pgtable entry. does NOT mean that we cannot map blocks 
// at lv1 and lv2. 
// #define TCR_VALUE			(TCR_T0SZ | TCR_T1SZ | TCR_TG0_4K | TCR_TG1_4K)

// t0sz = 64-48=16
#define TCR_VALUE  ( (0b00LL << 37) |   /* TBI=0, no tagging */\
					 (0b000LL << 32) |  /* IPS= 32 bit ... 000 = 32bit, 001 = 36bit, 010 = 40bit */\
					 (0b10LL << 30)  |  /* TG1=4k ... options are 10=4KB, 01=16KB, 11=64KB ... take care differs from TG0 */\
					 (0b11LL << 28)  |  /* SH1=3 inner ... options 00 = Non-shareable, 01 = INVALID, 10 = Outer Shareable, 11 = Inner Shareable */\
					 (0b01LL << 26)  |  /* ORGN1=1 write back .. options 00 = Non-cacheable, 01 = Write back cacheable, 10 = Write thru cacheable, 11 = Write Back Non-cacheable */\
					 (0b01LL << 24)  |  /* IRGN1=1 write back .. options 00 = Non-cacheable, 01 = Write back cacheable, 10 = Write thru cacheable, 11 = Write Back Non-cacheable */\
					 (0b0LL  << 23)  |  /* EPD1 ... Translation table walk disable for translations using TTBR0_EL1  0 = walk, 1 = generate fault */\
					 (16LL   << 16)  |  /* T1SZ=25 (512G) ... The region size is 2 POWER (64-T1SZ) bytes */\
					 (0b00LL << 14)  |  /* TG0=4k  ... options are 00=4KB, 01=64KB, 10=16KB,  ... take care differs from TG1 */\
					 (0b11LL << 12)  |  /* SH0=3 inner ... .. options 00 = Non-shareable, 01 = INVALID, 10 = Outer Shareable, 11 = Inner Shareable */\
					 (0b01LL << 10)  |  /* ORGN0=1 write back .. options 00 = Non-cacheable, 01 = Write back cacheable, 10 = Write thru cacheable, 11 = Write Back Non-cacheable */\
					 (0b01LL << 8)   |  /* IRGN0=1 write back .. options 00 = Non-cacheable, 01 = Write back cacheable, 10 = Write thru cacheable, 11 = Write Back Non-cacheable */\
					 (0b0LL  << 7)   |  /* EPD0  ... Translation table walk disable for translations using TTBR0_EL1  0 = walk, 1 = generate fault */\
					 (16LL   << 0) ) 	/* T0SZ=25 (512G)  ... The region size is 2 POWER (64-T0SZ) bytes */

#endif
