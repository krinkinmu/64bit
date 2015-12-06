#include "interrupt.h"
#include "memory.h"
#include "stdio.h"

#define IDT_PRESENT    (1ul << 47)
#define IDT_64INT      (14ul << 40)
#define IDT_64TRAP     (15ul << 40)
#define IDT_SIZE       128
#define IDT_EXCEPTIONS 32
#define IDT_IRQS       16
#define IDT_INTS       (IDT_IRQS + IDT_EXCEPTIONS)

struct idt_entry {
	unsigned long low;
	unsigned long high;
} __attribute__((packed));

struct idt_ptr {
	unsigned short size;
	unsigned long base;
} __attribute__((packed));


typedef void (*raw_isr_entry_t)(void);
extern raw_isr_entry_t isr_entry[];

static struct idt_entry idt[IDT_SIZE];
static struct idt_ptr idt_ptr;
static isr_t handler[IDT_SIZE];


static void setup_idt_entry(struct idt_entry *entry, unsigned short cs,
			unsigned long isr, unsigned long flags)
{
	entry->low = (isr & 0xFFFFul) | ((unsigned long)cs << 16)
		| ((isr & 0xFFFF0000ul) << 32) | flags;
	entry->high = (isr >> 32) & 0xFFFFFFFFul;
}

static void setup_irq(raw_isr_entry_t isr, int no)
{
	setup_idt_entry(idt + no, KERNEL_CS, (unsigned long)isr,
				IDT_64INT | IDT_PRESENT);
}

static void setup_trap(raw_isr_entry_t isr, int no)
{
	setup_idt_entry(idt + no, KERNEL_CS, (unsigned long)isr,
				IDT_64TRAP | IDT_PRESENT);
}

static void set_idt(const struct idt_ptr *ptr)
{ __asm__ volatile ("lidt (%0)" : : "a"(ptr)); }

static void dump_error_frame(const struct interrupt_frame *frame)
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

static void default_exception_handler(struct interrupt_frame *frame)
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

void isr_common_handler(struct interrupt_frame *ctx)
{
	const unsigned long intno = ctx->intno;
	const isr_t isr = handler[intno];

	if (!isr && intno < IDT_EXCEPTIONS) {
		default_exception_handler(ctx);
		return;
	}

	if (isr) isr(ctx);
}

void int_set(isr_t isr, int no)
{ handler[no] = isr; }

void int_clear(int no)
{ handler[no] = (isr_t)0; }

void setup_int(void)
{
	for (int i = 0; i != IDT_INTS; ++i)
		setup_irq(isr_entry[i], i);

	for (int i = IDT_INTS; i != IDT_SIZE; ++i)
		setup_trap(isr_entry[i], i);

	idt_ptr.size = sizeof(idt) - 1;
	idt_ptr.base = (unsigned long)idt;
	set_idt(&idt_ptr);
}
