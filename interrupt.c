#include "interrupt.h"
#include "memory.h"

#define IDT_PRESENT (1ul << 47)
#define IDT_64INT   (14ul << 40)
#define IDT_64TRAP  (15ul << 40)
#define IDT_SIZE    256
#define IDT_HIGH    30

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


void isr_common_handler(struct interrupt_frame *ctx)
{
	const unsigned long intno = ctx->intno;

	isr_t isr = handler[intno];
	if (isr)
		isr(ctx);
}

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

void int_set(isr_t isr, int no)
{ handler[no] = isr; }

void int_clear(int no)
{ handler[no] = (isr_t)0; }

void setup_int(void)
{
	for (int i = 0; i != IDT_HIGH; ++i)
		setup_irq(isr_entry[i], i);

	for (int i = IDT_HIGH; i != IDT_SIZE; ++i)
		setup_trap(isr_entry[i], i);

	idt_ptr.size = sizeof(idt) - 1;
	idt_ptr.base = (unsigned long)idt;
	set_idt(&idt_ptr);
}
