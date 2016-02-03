#include "thread_regs.h"
#include "interrupt.h"
#include "threads.h"
#include "irqchip.h"
#include "memory.h"
#include "string.h"
#include "stdio.h"
#include "error.h"
#include "mm.h"

#include <stdint.h>

#define IDT_PRESENT    ((uint64_t)1 << 47)
#define IDT_64INT      ((uint64_t)14 << 40)
#define IDT_64TRAP     ((uint64_t)15 << 40)
#define IDT_USER       ((uint64_t)3 << 45)
#define IDT_SYSCALL    0x80
#define IDT_SIZE       (IDT_SYSCALL + 1)
#define IDT_IRQS       16
#define IDT_EXCEPTIONS 32


struct idt_entry {
	uint64_t low;
	uint64_t high;
} __attribute__((packed));

struct idt_ptr {
	uint16_t size;
	uint64_t base;
} __attribute__((packed));


typedef void (*raw_isr_entry_t)(void);
extern raw_isr_entry_t isr_entry[];
extern void trap_handler(void);

static struct idt_entry idt[IDT_SIZE];
static struct idt_ptr idt_ptr;
static irq_t handler[IDT_IRQS];
static int irqmask_count[IDT_IRQS];
static const struct irqchip *irqchip;


static void __setup_idt_entry(struct idt_entry *entry, unsigned short cs,
			unsigned long isr, unsigned long flags)
{
	entry->low = (isr & 0xFFFFul) | ((uint64_t)cs << 16)
		| ((isr & 0xFFFF0000ul) << 32) | flags;
	entry->high = (isr >> 32) & 0xFFFFFFFFul;
}

static void setup_idt_entry(int no, unsigned short cs, unsigned long isr,
			unsigned long flags)
{
	DBG_ASSERT(no < IDT_SIZE);

	__setup_idt_entry(idt + no, cs, isr, flags | IDT_PRESENT);
}

static void clear_idt_entry(int no)
{
	memset(idt + no, 0, sizeof(idt[no]));
}

static void setup_irq(raw_isr_entry_t isr, int no)
{
	setup_idt_entry(no, KERNEL_CS, (unsigned long)isr, IDT_64INT);
}

static void setup_syscall(raw_isr_entry_t isr, int no)
{
	setup_idt_entry(no, KERNEL_CS, (unsigned long)isr,
				IDT_64TRAP | IDT_USER);
}

static void set_idt(const struct idt_ptr *ptr)
{ __asm__ volatile ("lidt (%0)" : : "a"(ptr)); }

static void dump_error_frame(const struct thread_regs *frame)
{
	printf("exception %ld (%ld) at %#lx:%#lx\n", frame->intno, frame->error,
				frame->cs, frame->rip);
	puts("register state:");
	printf("\tstack %lx:%lx, rflags %#lx\n", frame->ss, frame->rsp,
				frame->rflags);
	printf("\trax %#lx, rbx %#lx, rcx %#lx, rdx %#lx\n", frame->rax,
				frame->rbx, frame->rcx, frame->rdx);
	printf("\trbp %#lx, rdi %#lx, rsi %#lx\n", frame->rbp, frame->rdi,
				frame->rsi);
	printf("\tr8 %#lx, r9 %#lx, r10 %#lx, r11 %#lx\n", frame->r8, frame->r9,
				frame->r10, frame->r11);
	printf("\tr12 %#lx, r13 %#lx, r14 %#lx, r15 %#lx\n", frame->r12,
				frame->r13, frame->r14, frame->r15);
}

static void default_exception_handler(struct thread_regs *frame)
{
	static const char *error[] = {
		"division error",
		"debug exception",
		"NMI interrupt",
		"breakpoint exception",
		"overflow exception",
		"bound range exception",
		"invalid opcode",
		"floating-point device not available",
		"double fault",
		"coprocessor segment overrun",
		"invalid tss",
		"segment not present",
		"stack fault",
		"general protection error",
		"page fault",
		"x86 FPU floating-point error",
		"alignment check exception",
		"machine-check exception",
		"SIMD floating-point exception",
		"virtualization exception",
		"undefined exception",
		"undefined exception",
		"undefined exception",
		"undefined exception",
		"undefined exception",
		"undefined exception",
		"undefined exception",
		"undefined exception",
		"undefined exception",
		"undefined exception",
		"undefined exception"
	};

	puts(error[frame->intno]);
	dump_error_frame(frame);
	while (1);
}

static inline void mask_irq(int irq)
{ if (irqmask_count[irq]++ == 0) irqchip_mask(irqchip, irq); }

static inline void unmask_irq(int irq)
{ if (--irqmask_count[irq] == 0) irqchip_unmask(irqchip, irq); }

static inline void ack_irq(int irq)
{ irqchip_eoi(irqchip, irq); }

static inline virt_t load_cr2(void)
{
	virt_t cr2;

	__asm__ volatile ("movq %%cr2, %0" : "=a"(cr2));
	return cr2;
}

static int page_fault_handler(struct thread_regs *ctx)
{
	const int access = (ctx->error & BIT_CONST(1)) != 0
				? VMA_ACCESS_WRITE
				: VMA_ACCESS_READ;
	const virt_t vaddr = load_cr2();
	struct thread *thread = current();

	if (!thread)
		return -EINVAL;

	local_irq_enable();

	return mm_page_fault(thread, vaddr, access);
}

void isr_common_handler(struct thread_regs *ctx)
{
	const int intno = ctx->intno;

	if (intno < IDT_EXCEPTIONS) {
		if (intno == 14 && !page_fault_handler(ctx))
			return;
		default_exception_handler(ctx);
		return;
	}

	const int irqno = intno - IDT_EXCEPTIONS;
	const irq_t irq = handler[irqno];

	mask_irq(irqno);
	ack_irq(irqno);
	if (irq)
		irq(irqno);
	unmask_irq(irqno);

	if (need_resched())
		schedule();
}

void register_irq_handler(int irq, irq_t isr)
{
	DBG_ASSERT(irq < IDT_IRQS);

	const int intno = irq + IDT_EXCEPTIONS;

	handler[irq] = isr;
	setup_irq(isr_entry[intno], intno);
	unmask_irq(irq);
}

void unregister_irq_handler(int irq, irq_t isr)
{
	DBG_ASSERT(irq < IDT_IRQS);

	const int intno = irq + IDT_EXCEPTIONS;

	if (handler[irq] == isr) {
		mask_irq(irq);
		handler[irq] = (irq_t)0;
		clear_idt_entry(intno);
	}
}

void setup_ints(void)
{
	for (int i = 0; i != IDT_IRQS; ++i)
		irqmask_count[i] = 1;

	for (int i = 0; i != IDT_EXCEPTIONS; ++i)
		setup_irq(isr_entry[i], i);

	extern void syscall_handler(void);

	setup_syscall(&syscall_handler, IDT_SYSCALL);

	idt_ptr.size = sizeof(idt) - 1;
	idt_ptr.base = (uintptr_t)idt;
	set_idt(&idt_ptr);

	irqchip = &i8259a;
	irqchip_map(irqchip, IDT_EXCEPTIONS);
}
