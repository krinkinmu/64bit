#include "interrupt.h"
#include "memory.h"
#include "paging.h"
#include "stdio.h"
#include "vga.h"

static void timer_isr(int irq)
{
	printf("irq %d\n", irq);
}

void main(void)
{
	setup_vga();
	setup_ints();
	setup_memory();
	setup_buddy();
	setup_paging();

	register_irq_handler(0, &timer_isr);
	unmask_irq(0);
	local_irq_enable();

	while (1);
}
