// kernel boot code, shall be used with newer rpi3 firmware that builds in
//	armstub, which boots kernel at 0x80000 and EL2
// the firmware also provides spintable for SMP

// NB: qemu/gdb cannot do source-level debugging for boot.S (before MMU is on),
// b/c we are on PA

#include "param.h"
#include "sysregs.h"
#include "plat.h"
#include "mmu.h"

.section ".text.boot"

// use x28 for cpuid_reg throughout boot.S. x28 not to be used for other purposes
cpuid_reg 	.req 	x28 

// all cpus start here. 
// yet, on power up only cpu0 runs, which will release cpu1+ later (cf start_secondary_cores())
.globl _start
_start:		
	bl __asm_flush_dcache_all // seems needed for uboot, otherwise sometimes fail to boot
	// load core id to x28
	// cf: https://developer.arm.com/documentation/100095/0002/system-control/aarch64-register-descriptions/multiprocessor-affinity-register--el1
	mrs	cpuid_reg, mpidr_el1	
	and	cpuid_reg, cpuid_reg, #0xFF

1:
	// MMU off, until we set pgtables. cf: sysregs.h
	ldr	x0, =SCTLR_VALUE_MMU_DISABLED  
	msr	sctlr_el1, x0
	
	// No trapping EL0/EL1 access to advanced simd, fp registers.
	// cf: https://developer.arm.com/documentation/ddi0595/2021-03/AArch64-Registers/CPACR-EL1--Architectural-Feature-Access-Control-Register
	//	some code may contain them (e.g. usblib StringFormat uses q0/q1...)
	MRS     x1, CPACR_EL1
	ORR     x1, x1, #(0x3 << 20)    // Write CPACR_EL1.FPEN bits
	MSR     CPACR_EL1, x1
	ISB

	// For Cortex A53 SMP coherency, setting S3_1_c15_c2_1 is needed. 
	// However we assume that it's already done by firmware (armstub)

	/* -------- Exception level switch -------------- */
	// Check the current exception level: EL3 or EL2?	
	mrs x0, CurrentEL
  	lsr x0, x0, #2
	cmp x0, #3
	beq el3

	# Current EL: EL2 
	# set EL1 to be running in AArch64
	mrs	x0, hcr_el2
	orr	x0, x0, #HCR_RW  
	msr	hcr_el2, x0

	# prepare to switch to EL1
	mov x0, #SPSR_VALUE
	msr	spsr_el2, x0

	adr	x0, el1_entry
	msr	elr_el2, x0
	eret	// switch to EL1

el3: 		// Current EL: EL3
	// 	With rpi3 firmware (armstub) or qemu, kernel always starts in EL2; 
	//  We leave EL3 code here for completeness
  	ldr x0, =HCR_VALUE
  	msr hcr_el2, x0

	ldr	x0, =SCR_VALUE
	msr	scr_el3, x0

	# prepare to switch to EL1
	ldr	x0, =SPSR_VALUE
	msr	spsr_el3, x0

	adr	x0, el1_entry		
	msr	elr_el3, x0	
	eret	// switch to EL1				

	/* ------- Start of EL1 execution ------- */
el1_entry:	
	cbnz cpuid_reg, setup_sp 

	// Below: cpu0 only. Clean up bss region. 
	// bss_begin/end are linking addr (kernel virt). convert them to phys.
	// they are (at least) 8 bytes aligned in link script
	ldr	x0, =bss_begin
	ldr	x1, =bss_end
	ldr x2, =VA_START
	sub x0, x0, x2
	sub x1, x1, x2
	sub	x1, x1, x0
	bl 	memzero_aligned
	
	// Create kern pgtables
	// for readability, written in C. 
	// hence, grab a temporary SP (end of phys mem)
	ldr x0, =PHYS_SIZE
	mov sp, x0 	
	bl create_kern_pgtables
#if defined(PLAT_VIRT) || defined(PLAT_RPI3QEMU)
	bl	create_kern_idmap // see its comment
#endif	
	// NB: pgtables are set up, but not effective yet

	// Below: executed by all cpus
setup_sp: 	
	// Set virt sp = (cpuid_reg+1)*4096 + boot_stacks
	// (assuming linear mapping, phys 0 --> virt VA_START)	
	 
	/* STUDENT_TODO: your code here */

	mov x0, #(PAGE_SIZE) /* STUDENT_TODO: replace this */
	ldr x1, =boot_stacks /* STUDENT_TODO: replace this */
	add x0, x0, x1 /* STUDENT_TODO: replace this */

	mov sp, x0 		// NB: we aren't use sp yet -- until mmu is on

	// load kernel's pgtable
	adrp	x0, pg_dir			// @pg_dir: the virtual base address of pgtables. cf utils.h
	msr		ttbr1_el1, x0
	isb 		// memory barrier 

	// tcr: translation control reg. (xslat granule, user/kernel split, etc)	
	ldr	x0, =(TCR_VALUE)
	msr	tcr_el1, x0  // tcr_el1: Translation Control Register. cf mmu.h
	isb 		// memory barrier 
	
	// mair: memory attributes. cf mmu.h
	ldr	x0, =(MAIR_VALUE)	
	msr	mair_el1, x0

#ifdef PLAT_RPI3QEMU
	adrp	x0, idmap_dir	// all cores install idmap 
	msr	ttbr0_el1, x0	
#endif

	// all cpus: install irq vectors
	ldr x0, =vectors	// load VBAR_EL1 vector table addr (kern VA)
	msr	vbar_el1, x0	

	// below: cpu0 and cpu1+ jump to their respective code
	cmp cpuid_reg, #0
	b.eq 1f
	// cpu1+, the code entry is secondary_core_asm. 
	// load the linking (virt) addr of the entry to x2, 
	// turn on MMU, and jump to the entry
	 
	/* STUDENT_TODO: your code here */
	br 	x2

1:
	// cpu0 only, load the linking (virt) addr of kernel_main
	ldr	x2, =kernel_main  
	ldr x0, =SCTLR_VALUE // Turn on MMU. cf sysregs.h
	msr	sctlr_el1, x0	// BOOM! after this, this core runs on virtual addr
	br 	x2				// Jump to kernel_main() (kernel.c)

secondary_core_asm:	
	mov x0, cpuid_reg
	bl secondary_core