// #define K2_DEBUG_VERBOSE
#define K2_DEBUG_WARN

#include <stddef.h>
#include <stdint.h>

#include "plat.h"
#include "utils.h"
#include "mmu.h"
#include "sched.h"

// unittests.c
extern void test_ktimer(); 
extern void test_sys_sleep();
extern void test_malloc(); 
extern void test_mbox(); 
extern void test_usb_kb(); 
extern void test_usb_storage(); 
extern void test_fb(); 
extern void test_sound(); 
extern void test_sd(); 
extern void test_kernel_tasks();
extern void test_spinlock();
extern void test_sem();
extern void test_sf();

extern void kconsole_test(); // mbox.c

// 1st user process
extern unsigned long user_begin;	// cf the linker script
extern unsigned long user_end;
extern void user_process(); // user.c

// main body of kernel thread
void kernel_process() {
	unsigned long begin = (unsigned long)&user_begin;  	// defined in linker-qemu.ld
	unsigned long end = (unsigned long)&user_end;
	unsigned long process = (unsigned long)&user_process; // user.c

	// test_ktimer(); while (1);
	// test_sys_sleep(); while (1); 
	// test_malloc(); while (1); 
	// test_mbox(); while (1); 
	// test_usb_kb(); while (1); 
	// test_usb_storage(); while (1); 
	// test_fb(); while (1); 
	// test_sound(); while (1); 
	// test_sd(); while (1); 	// works for both rpi3 hw & qemu
	// test_spinlock(); while (1);
	// test_kernel_tasks(); while (1);
	// kconsole_test(); while (1);
	// while (1) {test_sem();} while (1);	
	// dump_mem_info(); test_sf(); while (1);
	// show_stack(myproc(), "");

	printf("Kernel process started at EL %d, pid %d\r\n", get_el(), myproc()->pid);
	int err = move_to_user_mode(begin, end - begin, process - begin);
	if (err < 0){
		printf("Error while moving process to user mode\n\r");
	} else I("move_to_user_mode ok");
	// this func is called from ret_from_fork (entry.S). after returning from 
	// this func, it goes back to ret_from_fork and performs kernel_exit there. 
	// hence, trapframe populated by move_to_user_mode() will take effect. 
}

void uart_send_string(char* str);

// the table used by firmware that wants to "park" the cores. implemented by:
// 	 - rpi3 firmware (https://github.com/raspberrypi/tools/tree/master/armstubs)
//   - qemu 
unsigned long *core_flags = (unsigned long *)PA2VA(0x000000D8); 

// entry for core1--3
// called from boot.S, after setting up sp, MMU, pgtables, etc
//quest: multicore
void secondary_core(int core_id)
{
	core_flags[core_id] = 0; // notifying cpu0: this cpu has MMU up, so cpu0 can go

	printf("Hello core %d\n", core_id);

	// make sure this core's sched tick is on:
	// initialize generic timer (per core), enable irq controller (per core),
	// and enable irq for this core 
	 
	/* STUDENT_TODO: your code here */

	// so far, on boot stack and as the "idle" task
	schedule(); 
	while (1)	// cf kernel_main()
		asm volatile("wfi"); 
}

extern unsigned long _start;  // boot.S

// invoked by kernel_main(). core0 only
//quest: multicore
static void start_secondary_cores(void) {		
	// wake up cpu1+ by changing core_flags, by writing a start address (phys) to 
	// their corresponding core_flags.
	 
	/* STUDENT_TODO: your code here */

	// cpu0: Flush the whole kernel memory
	// 1. make core_flags update visible to cpu1+ (which has no cache/mmu yet)
	// 2. cpu1+ were down when cpu0 was init kernel state; so they might
	// have missed cache transactions and therefore could see stale kernel states
	// e.g. cpu1+ couldn't see the effect of init_printf() and hence failed to 
	// print msgs. 
	__asm_flush_dcache_range((void *)VA_START,  (void*)VA_START + DEVICE_BASE); 

	asm volatile ("dsb sy");
	asm volatile ("sev");
	
	// wait for cpu1+ to boot
	int timeout=100; // ms
	for (int i=1; i <NCPU; i++) {
		while (core_flags[i]) {
			if (timeout-- <= 0) {
				E("failed to start core %d", i); 
				E("coreflags %lx %lx %lx %lx", core_flags[0], core_flags[1], core_flags[2], 
					core_flags[3]);
				BUG(); 
			}
			ms_delay(1); 
		}
	}
	I("start cores ok");
}

extern void dump_pgdir(void); // mm.c

// core0 only
//quest: FAT, multicore
void kernel_main() {
	uart_init();
	init_printf(NULL, putc);	
	printf("------ kernel boot ------  core %d\n\r", cpuid());
	printf("build time (kernel.c) %s %s\n", __DATE__, __TIME__); // simplicity 
		
	paging_init(); 
	dump_pgdir();
	sched_init(); 	// must be before schedule() or timertick() 
	fb_init(); 		// reserve fb memory other page allocations
	consoleinit(); 	
	binit();         // buffer cache
    iinit();         // inode table
    fileinit();      // file table
	ramdisk_init(); 	// ramdisk - blk dev1
	sys_timer_init(); 		// kernel timer: delay, timekeeping...
	/* STUDENT_TODO: your code here */
	enable_interrupt_controller(0/*coreid*/);
	enable_irq();
	
	if (usbkb_init() == 0) I("usb kb init done"); 
#ifdef PLAT_RPI3	
	gamehat_gpio_init(); 
#endif	
	// start cpu1+
	/* STUDENT_TODO: your code here */
	generic_timer_init();  // sched ticks alive. preemptive scheduler is on

	// now cpu is on its boot stack (boot.S) belonging to the idle task. 
	// schedule() will jump off to kernel stacks belonging to normal tasks
	// (i.e. init_task as set up in sched_init(), sched.c)
	schedule(); 
	// only when scheduler has no normal tasks to run for the current cpu,
	// the cpu switches back to the boot stack and returns here
    while (1) {
        // don't call schedule(), otherwise each irq calls schedule(): too much
        // instead, let timer_tick() throttle & decide when to call schedule()
        V("idle task");
        asm volatile("wfi");
    }
}

// the 1st task (other than "idle"), created by sched_init()
void init(int arg/*ignored*/) {
	int wpid; 
    W("entering init");

	// create a kern task as our launchpad: running kernel tests, launch user
	//  tasks, etc.
	int res = copy_process(PF_KTHREAD, (unsigned long)&kernel_process, 0/*arg*/,
		 "kern-1"); BUG_ON(res<0); 
        
	start_sf(); // start surface flinger 

	while (1) {
		wpid = wait(0 /* does not care about status */); 
		if (wpid < 0) {
			W("init: wait failed with %d", wpid);
			panic("init: maybe no child. has nothing to do. bye"); 
		} else {
			I("wait returns pid=%d", wpid);
			// a parentless task 
		}
	}
}