#ifndef __INTERRUPT_H__
#define __INTERRUPT_H__

struct interrupt_frame {
	unsigned long r15;
	unsigned long r14;
	unsigned long r13;
	unsigned long r12;
	unsigned long r11;
	unsigned long r10;
	unsigned long r9;
	unsigned long r8;
	unsigned long rax;
	unsigned long rbx;
	unsigned long rcx;
	unsigned long rdx;
	unsigned long rbp;
	unsigned long rdi;
	unsigned long rsi;
	unsigned long intno;
	unsigned long error;
	unsigned long rip;
	unsigned long cs;
	unsigned long rflags;
	unsigned long rsp;
	unsigned long ss;
} __attribute__((packed));

typedef void (*isr_t)(struct interrupt_frame *frame);

inline static void local_irq_disable(void)
{ __asm__ volatile ("cli"); }

inline static void local_irq_enable(void)
{ __asm__ volatile ("sti"); }

void int_set(isr_t isr, int no);
void int_clear(int no);
void setup_int(void);

#endif /*__INTERRUPT_H__*/
