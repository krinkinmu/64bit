#include "interrupt.h"
#include "kernel.h"
#include "ioport.h"
#include "time.h"

/*
 * Timer/Counter Control Register Format:
 * +-----+-----+-----+-----+-----+-----+-----+-----+
 * | sc1 | sc0 | rw1 | rw0 | M2  | M1  | M0  | BCD |
 * +-----+-----+-----+-----+-----+-----+-----+-----+
 *
 * sc1 and sc0 select counter (0-2)
 * rw1 and rw0 select read/write mode:
 *  - rw1 == 0 and rw0 == 1 - read/write LSB
 *  - rw1 == 1 and rw0 == 0 - read/write MSB
 *  - rw1 == 1 and rw0 == 1 - first read/write LSB, then MSB
 * M2, M1 and M0 - Counter Mode Select
 * BCD - BCD vs Binary number encoding
 */
#define I8254_CTRL_PORT     0x43
#define I8254_CH0_DATA_PORT 0x40
#define I8254_FREQUENCY     1193180ul
#define I8254_IRQ           0

#define I8254_CTRL_RW0      5
#define I8254_CTRL_RW1      6
#define I8254_CTRL_M0       1
#define I8254_CTRL_M1       2
#define I8254_CTRL_M2       3

#define I8254_SELECT_CH0    0ul
#define I8254_BINARY        0ul
#define I8254_LOW_BYTE      BIT_CONST(I8254_CTRL_RW0)
#define I8254_HIGH_BYTE     BIT_CONST(I8254_CTRL_RW1)
#define I8254_HILO_BYTES    (I8254_LOW_BYTE | I8254_HIGH_BYTE)
#define I8254_RATE_GEN      BIT_CONST(I8254_CTRL_M1)


static unsigned long long jiffies_value;


static unsigned long i8254_divisor(unsigned long freq)
{ return I8254_FREQUENCY / freq; }

static void i8254_set_frequency(unsigned long freq)
{
	const unsigned long divisor = i8254_divisor(freq);
	const unsigned char cmd = I8254_SELECT_CH0 | I8254_HILO_BYTES
				| I8254_RATE_GEN | I8254_SELECT_CH0;

	out8(I8254_CTRL_PORT, cmd);
	out8(I8254_CH0_DATA_PORT, divisor & BITS(7, 0));
	out8(I8254_CH0_DATA_PORT, divisor & BITS(15, 8));
}

static void i8254_interrupt_handler(int irq)
{
	(void) irq;

	++jiffies_value;
}

unsigned long long jiffies(void)
{ return jiffies_value; }

void setup_time(void)
{
	i8254_set_frequency(HZ);
	register_irq_handler(I8254_IRQ, &i8254_interrupt_handler);
}
