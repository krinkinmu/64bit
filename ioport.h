#ifndef __IOPORT_H__
#define __IOPORT_H__

static inline void out8(unsigned short port, unsigned char data)
{ __asm__ volatile("outb %0, %1" : : "a"(data), "d"(port)); }

#endif /* __IOPORT_H__ */
