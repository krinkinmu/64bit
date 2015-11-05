#include "console.h"
#include "vga.h"

static char *const VGAMEM = (char *)0xb8000;
static const unsigned ROWS = 25;
static const unsigned COLS = 80;
static const char CATTR = 7;

static unsigned row, col;

static void newline(void)
{
	char * const ptr = VGAMEM + 2 * row * col;
	unsigned i;

	for (i = 0; i != 2 * COLS; i += 2) {
		*(ptr + i) = ' ';
		*(ptr + i + 1) = CATTR;
	}
}

static void putchar(int c)
{
	const unsigned pos = 2 * (row * COLS + col);

	switch (c) {
	default:
		*(VGAMEM + pos) = c;
		*(VGAMEM + pos + 1) = CATTR;

		if (++col != COLS)
			return;
	case '\n':
		row = (row + 1) % ROWS;
		newline();
	case '\r':
		col = 0;
		break;
	}
}

static void clrscr(void)
{
	unsigned i;

	for (i = 0; i != 2 * ROWS * COLS; i += 2) {
		*(VGAMEM + i) = ' ';
		*(VGAMEM + i + 1) = CATTR;
	}
}

static void write(const char *str, unsigned long size)
{
	unsigned long i;

	for (i = 0; i != size; ++i)
		putchar(str[i]);
}

void setup_vga(void)
{
	static struct console vga_console;

	vga_console.write = &write;
	clrscr();
	register_console(&vga_console);
}
