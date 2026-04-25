// fxl: July 2024. from Linux 6.10.2
// https://elixir.bootlin.com/linux/latest/source/arch/arm64/kernel/stacktrace.c
//
// terms:  NVHE - KVM nVHE hypervisor stack tracing support

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Stack tracing support
 *
 * Copyright (C) 2012 ARM Ltd.
 */
// #include <linux/kernel.h>
// #include <linux/efi.h>
// #include <linux/export.h>
// #include <linux/filter.h>
// #include <linux/ftrace.h>
// #include <linux/kprobes.h>
// #include <linux/sched.h>
// #include <linux/sched/debug.h>
// #include <linux/sched/task_stack.h>
// #include <linux/stacktrace.h>

// #include <asm/efi.h>
// #include <asm/irq.h>
// #include <asm/stack_pointer.h>
// #include <asm/stacktrace.h>

// kernel
#include "param.h"
#include "plat.h"
#include "utils.h"
#include "mmu.h"
#include "sched.h"

// stacktrace private
// #include "common.h"
// #include "compiler.h"

#undef CONFIG_KRETPROBES

#define ENOENT 1 
#define EINVAL 2

typedef int bool;
#define true 1
#define false 0

#ifndef NULL
#define NULL 0
#endif

#define __user
#define   noinline                      __attribute__((__noinline__))
#define noinstr     // __noinstr_section(".noinstr.text")

#define ptrauth_strip_kernel_insn_pac(ptr)	(ptr)
#define ptrauth_strip_user_insn_pac(ptr)	(ptr)


#define __scalar_type_to_expr_cases(type)				\
		unsigned type:	(unsigned type)0,			\
		signed type:	(signed type)0

#define __unqual_scalar_typeof(x) typeof(				\
		_Generic((x),						\
			 char:	(char)0,				\
			 __scalar_type_to_expr_cases(char),		\
			 __scalar_type_to_expr_cases(short),		\
			 __scalar_type_to_expr_cases(int),		\
			 __scalar_type_to_expr_cases(long),		\
			 __scalar_type_to_expr_cases(long long),	\
			 default: (x)))

#ifndef __READ_ONCE
#define __READ_ONCE(x)	(*(const volatile __unqual_scalar_typeof(x) *)&(x))
#endif

#define READ_ONCE(x)							\
({									\
	__READ_ONCE(x);							\
})

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

// fxl ... move it somewhere
/**
 * stack_trace_consume_fn - Callback for arch_stack_walk()
 * @cookie:	Caller supplied pointer handed back by arch_stack_walk()
 * @addr:	The stack entry address to consume
 *
 * Return:	True, if the entry was consumed or skipped
 *		False, if there is no space left to store
 */
typedef bool (*stack_trace_consume_fn)(void *cookie, unsigned long addr);

/////////////////////////////////////////////////////////////////////////
// common.h 

struct stack_info {
	unsigned long low;
	unsigned long high;
};

/**
 * struct unwind_state - state used for robust unwinding.
 *
 * @fp:          The fp value in the frame record (or the real fp)
 * @pc:          The lr value in the frame record (or the real lr)
 *
 * @stack:       The stack currently being unwound.
 * @stacks:      An array of stacks which can be unwound.
 * @nr_stacks:   The number of stacks in @stacks.
 */
struct unwind_state {
	unsigned long fp;
	unsigned long pc;

	struct stack_info stack;
	struct stack_info *stacks;
	int nr_stacks;
};

static inline struct stack_info stackinfo_get_unknown(void)
{
	return (struct stack_info) {
		.low = 0,
		.high = 0,
	};
}

static inline bool stackinfo_on_stack(const struct stack_info *info,
				      unsigned long sp, unsigned long size)
{
	if (!info->low)
		return false;

	if (sp < info->low || sp + size < sp || sp + size > info->high)
		return false;

	return true;
}

static inline void unwind_init_common(struct unwind_state *state)
{
	state->stack = stackinfo_get_unknown();
}

static struct stack_info *unwind_find_next_stack(const struct unwind_state *state,
						 unsigned long sp,
						 unsigned long size)
{
	for (int i = 0; i < state->nr_stacks; i++) {
		struct stack_info *info = &state->stacks[i];

		if (stackinfo_on_stack(info, sp, size))
			return info;
	}

	return NULL;
}

/**
 * unwind_consume_stack() - Check if an object is on an accessible stack,
 * updating stack boundaries so that future unwind steps cannot consume this
 * object again.
 *
 * @state: the current unwind state.
 * @sp:    the base address of the object.
 * @size:  the size of the object.
 *
 * Return: 0 upon success, an error code otherwise.
 */
static inline int unwind_consume_stack(struct unwind_state *state,
				       unsigned long sp,
				       unsigned long size)
{
	struct stack_info *next;

	if (stackinfo_on_stack(&state->stack, sp, size))
		goto found;

	next = unwind_find_next_stack(state, sp, size);
	if (!next)
		return -EINVAL;

	/*
	 * Stack transitions are strictly one-way, and once we've
	 * transitioned from one stack to another, it's never valid to
	 * unwind back to the old stack.
	 *
	 * Remove the current stack from the list of stacks so that it cannot
	 * be found on a subsequent transition.
	 *
	 * Note that stacks can nest in several valid orders, e.g.
	 *
	 *   TASK -> IRQ -> OVERFLOW -> SDEI_NORMAL
	 *   TASK -> SDEI_NORMAL -> SDEI_CRITICAL -> OVERFLOW
	 *   HYP -> OVERFLOW
	 *
	 * ... so we do not check the specific order of stack
	 * transitions.
	 */
	state->stack = *next;
	*next = stackinfo_get_unknown();

found:
	/*
	 * Future unwind steps can only consume stack above this frame record.
	 * Update the current stack to start immediately above it.
	 */
	state->stack.low = sp + size;
	return 0;
}

/**
 * unwind_next_frame_record() - Unwind to the next frame record.
 *
 * @state:        the current unwind state.
 *
 * Return: 0 upon success, an error code otherwise.
 */
static inline int
unwind_next_frame_record(struct unwind_state *state)
{
	unsigned long fp = state->fp;
	int err;

	if (fp & 0x7)
		return -EINVAL;

	err = unwind_consume_stack(state, fp, 16);  // fxl: read fp from stack?
	if (err)
		return err;

	/*
	 * Record this frame record's values.
	 */
	state->fp = __READ_ONCE(*(unsigned long *)(fp));
	state->pc = __READ_ONCE(*(unsigned long *)(fp + 8)); // fxl: load pc from "lr" of the record

	return 0;
}

/////////////////////////////////////////////////////////////////////////
// stacktrace.c

// fxl: get a task's stack boundary....
static inline struct stack_info stackinfo_get_task(const struct task_struct *tsk)
{
	// unsigned long low = (unsigned long)task_stack_page(tsk);
	unsigned long low = (unsigned long)(tsk);
	unsigned long high = low + THREAD_SIZE;

	return (struct stack_info) {
		.low = low,
		.high = high,
	};
}

/*
 * Kernel unwind state
 *
 * @common:      Common unwind state.
 * @task:        The task being unwound.
 * @kr_cur:      When KRETPROBES is selected, holds the kretprobe instance
 *               associated with the most recently encountered replacement lr
 *               value.
 */
struct kunwind_state {
	struct unwind_state common;
	struct task_struct *task;
#ifdef CONFIG_KRETPROBES
	struct llist_node *kr_cur;
#endif
};

static __always_inline void
kunwind_init(struct kunwind_state *state,
	     struct task_struct *task)
{
	unwind_init_common(&state->common);
	state->task = task;
}

/*
 * Start an unwind from a pt_regs (trapframe).
 *
 * The unwind will begin at the PC within the regs.
 *
 * The regs must be on a stack currently owned by the calling task.
 */
static __always_inline void
kunwind_init_from_regs(struct kunwind_state *state,
		       struct trapframe *regs)
{
	kunwind_init(state, myproc());

	state->common.fp = regs->regs[29];
	state->common.pc = regs->pc;
}

/*
 * Start an unwind from a caller.
 *
 * The unwind will begin at the caller of whichever function this is inlined
 * into.
 *
 * The function which invokes this must be noinline.
 */
static __always_inline void
kunwind_init_from_caller(struct kunwind_state *state)
{
	kunwind_init(state, myproc());

	state->common.fp = (unsigned long)__builtin_frame_address(1);
	state->common.pc = (unsigned long)__builtin_return_address(0);
}

// fxl: thread_info.h
#define thread_saved_pc(tsk)	\
	((unsigned long)(tsk->cpu_context.pc))
#define thread_saved_sp(tsk)	\
	((unsigned long)(tsk->cpu_context.sp))
#define thread_saved_fp(tsk)	\
	((unsigned long)(tsk->cpu_context.fp))

/*
 * Start an unwind from a blocked task.
 *
 * The unwind will begin at the blocked tasks saved PC (i.e. the caller of
 * cpu_switch_to()).
 *
 * The caller should ensure the task is blocked in cpu_switch_to() for the
 * duration of the unwind, or the unwind will be bogus. It is never valid to
 * call this for the current task.
 */
static __always_inline void
kunwind_init_from_task(struct kunwind_state *state,
		       struct task_struct *task)
{
	kunwind_init(state, task);

	state->common.fp = thread_saved_fp(task);
	state->common.pc = thread_saved_pc(task);
}

// fxl: what's this for?? recover return addr to state->common.pc??
static __always_inline int
kunwind_recover_return_address(struct kunwind_state *state)
{
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	if (state->task->ret_stack &&
	    (state->common.pc == (unsigned long)return_to_handler)) {
		unsigned long orig_pc;
		orig_pc = ftrace_graph_ret_addr(state->task, NULL,
						state->common.pc,
						(void *)state->common.fp);
		if (WARN_ON_ONCE(state->common.pc == orig_pc))
			return -EINVAL;
		state->common.pc = orig_pc;
	}
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

#ifdef CONFIG_KRETPROBES
	if (is_kretprobe_trampoline(state->common.pc)) {
		unsigned long orig_pc;
		orig_pc = kretprobe_find_ret_addr(state->task,
						  (void *)state->common.fp,
						  &state->kr_cur);
		state->common.pc = orig_pc;
	}
#endif /* CONFIG_KRETPROBES */

	return 0;
}

/*
 * Unwind from one frame record (A) to the next frame record (B).
 *
 * We terminate early if the location of B indicates a malformed chain of frame
 * records (e.g. a cycle), determined based on the location and fp value of A
 * and the location (but not the fp value) of B.
 */
static __always_inline int
kunwind_next(struct kunwind_state *state)       // fxl: this seems the core func
{
	struct task_struct *tsk = state->task;
	unsigned long fp = state->common.fp;
	int err;

	/* Final frame; nothing to unwind */
    // fxl: stackframe --- the saved initial fp 
	if (fp == (unsigned long)task_pt_regs(tsk)->stackframe)
		return -ENOENT;

	err = unwind_next_frame_record(&state->common);
	if (err)
		return err;

	state->common.pc = ptrauth_strip_kernel_insn_pac(state->common.pc);

	return kunwind_recover_return_address(state);
}

typedef bool (*kunwind_consume_fn)(const struct kunwind_state *state, void *cookie);

static __always_inline void
do_kunwind(struct kunwind_state *state, kunwind_consume_fn consume_state,
	   void *cookie)
{
	if (kunwind_recover_return_address(state))
		return;

	while (1) {
		int ret;

		if (!consume_state(state, cookie))  // fxl: callback?? consume once
			break;
		ret = kunwind_next(state);
		if (ret < 0)
			break;
	}
}

/*
 * Per-cpu stacks are only accessible when unwinding the current task in a
 * non-preemptible context.
 */
#define STACKINFO_CPU(name)					\
	({							\
		((task == current) && !preemptible())		\
			? stackinfo_get_##name()		\
			: stackinfo_get_unknown();		\
	})

/*
 * SDEI stacks are only accessible when unwinding the current task in an NMI
 * context.
 */
#define STACKINFO_SDEI(name)					\
	({							\
		((task == current) && in_nmi())			\
			? stackinfo_get_sdei_##name()		\
			: stackinfo_get_unknown();		\
	})

#define STACKINFO_EFI						\
	({							\
		((task == current) && current_in_efi())		\
			? stackinfo_get_efi()			\
			: stackinfo_get_unknown();		\
	})

static __always_inline void
kunwind_stack_walk(kunwind_consume_fn consume_state,
		   void *cookie, struct task_struct *task,
		   struct trapframe *regs)
{
    // fxl: these stacks (low,high) will be searched by unwinder to see
    //  if given sp,size falls into a stack 
	struct stack_info stacks[] = {
		stackinfo_get_task(task),
		// STACKINFO_CPU(irq),     // fxl: also stackinfo_get_irq()? unwind irq stack? TBD
#if defined(CONFIG_VMAP_STACK)
		STACKINFO_CPU(overflow),
#endif
#if defined(CONFIG_VMAP_STACK) && defined(CONFIG_ARM_SDE_INTERFACE)
		STACKINFO_SDEI(normal),
		STACKINFO_SDEI(critical),
#endif
#ifdef CONFIG_EFI
		STACKINFO_EFI,
#endif
	};
	struct kunwind_state state = {
		.common = {
			.stacks = stacks,
			.nr_stacks = ARRAY_SIZE(stacks),
		},
	};

	if (regs) {
		if (task != myproc())
			return;		// fxl:otherwise unreliable?
		kunwind_init_from_regs(&state, regs);
	} else if (task == myproc()) {
		kunwind_init_from_caller(&state);
	} else {
		kunwind_init_from_task(&state, task);
	}

	do_kunwind(&state, consume_state, cookie);
}

struct kunwind_consume_entry_data {
	stack_trace_consume_fn consume_entry;
	void *cookie;
};

// fxl: invoke the callback 
static __always_inline bool
arch_kunwind_consume_entry(const struct kunwind_state *state, void *cookie)
{
	struct kunwind_consume_entry_data *data = cookie;
	return data->consume_entry(data->cookie, state->common.pc);
}

// fxl: the entry of "a stack walk"
noinline noinstr void arch_stack_walk(stack_trace_consume_fn consume_entry,
			      void *cookie, struct task_struct *task,
			      struct trapframe *regs)
{
	struct kunwind_consume_entry_data data = {
		.consume_entry = consume_entry,
		.cookie = cookie,
	};

	kunwind_stack_walk(arch_kunwind_consume_entry, &data, task, regs);
}

// fxl: a "consume fn"
static bool dump_backtrace_entry(void *arg, unsigned long where)
{
	// char *loglvl = arg;
	// printf("%s %pSb\n", loglvl, (void *)where); // fxl: this also prints symbol name?
    // printf("%s 0x%lx\n", loglvl, where);
	// printf("0x%lx \\ \n", where);
	printf("0x%lx \\ \n", where-4); // prev insn?
	return true;
}

// "regs" can be 0; if so unwind starts from the current stackframe (lr, fp) 
// otherwise, unwind from regs::stack_frame (i.e. frame record
void dump_backtrace(struct trapframe *regs, struct task_struct *tsk,
		    const char *loglvl)
{
	printf("%s(regs = %p tsk = %p)\n", __func__, regs, tsk);

	// if (regs && user_mode(regs))
	// 	return;

	if (!tsk)
		tsk = myproc();

	// if (!try_get_task_stack(tsk))
	// 	return;

	printf("%s cpu %d call trace:\n", __func__, cpuid());
#ifdef PLAT_RPI3QEMU	
	printf("addr2line -e ./kernel/build-rpi3qemu/kernel8.elf \\ \n");
#else
	printf("addr2line -e ./kernel/build-rpi3/kernel8.elf \\ \n");
#endif
	arch_stack_walk(dump_backtrace_entry, (void *)loglvl, tsk, regs);

	// put_task_stack(tsk);
}

// void show_stack(struct task_struct *tsk, unsigned long *sp, const char *loglvl)
void show_stack(struct task_struct *tsk, const char *loglvl)
{
	dump_backtrace(NULL, tsk, loglvl);
    __asm volatile ("" ::: "memory");
	// barrier();
}

/*
 * The struct defined for userspace stack frame in AARCH64 mode.
 */
struct frame_tail {
	struct frame_tail	__user *fp;
	unsigned long		lr;
} __attribute__((packed));

/*
 * Get the return address for a single stackframe and return a pointer to the
 * next frame tail.
 */
static struct frame_tail __user *
unwind_user_frame(struct frame_tail __user *tail, void *cookie,
	       stack_trace_consume_fn consume_entry)
{
	struct frame_tail buftail;
	unsigned long err;
	unsigned long lr;

	/* Also check accessibility of one struct frame_tail beyond */
	// if (!access_ok(tail, sizeof(buftail)))
	// 	return NULL;		// TBD

	// pagefault_disable();
    // fxl: copy a record from user...
	// err = __copy_from_user_inatomic(&buftail, tail, sizeof(buftail));
    err = copyin(myproc()->mm, (char*)&buftail, (unsigned long)tail, sizeof(buftail));
	// pagefault_enable();

	if (err)
		return NULL;

	lr = ptrauth_strip_user_insn_pac(buftail.lr);

	if (!consume_entry(cookie, lr))
		return NULL;

	/*
	 * Frame pointers should strictly progress back up the stack
	 * (towards higher addresses).
	 */
	if (tail >= buftail.fp)
		return NULL;

	return buftail.fp;
}

static void arch_stack_walk_user(stack_trace_consume_fn consume_entry, void *cookie,
					const struct trapframe *regs)
{
	if (!consume_entry(cookie, regs->pc))       // fxl: consume pc first??
		return;
	
    /* AARCH64 mode */
    struct frame_tail __user *tail;
    // fxl: start from user fp (saved in trapframe
    tail = (struct frame_tail __user *)regs->regs[29];
    while (tail && !((unsigned long)tail & 0x7))
        tail = unwind_user_frame(tail, cookie, consume_entry);
}

void show_stack_user(void) {
	struct trapframe *p = task_pt_regs(myproc()); 

	printf("%s: Call trace (pid %d %s):\n", __func__, 
		myproc()->pid, myproc()->name);
	printf("addr2line -e ./usr/build/%s.debug \\ \n", myproc()->name);
	arch_stack_walk_user(dump_backtrace_entry, 0/*cookie*/,p);

	__asm volatile ("" ::: "memory");	
}