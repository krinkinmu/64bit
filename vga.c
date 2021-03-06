#include "console.h"
#include "memory.h"
#include "vga.h"

static char *const VGAMEM = VA(0xb8000);
static const int ROWS = 25;
static const int COLS = 80;
static const char CATTR = 7;

static int row, col;

static void vga_newline(void)
{
	char * const ptr = VGAMEM + 2 * row * COLS;

	for (int i = 0; i != 2 * COLS; i += 2) {
		*(ptr + i) = ' ';
		*(ptr + i + 1) = CATTR;
	}
}

static void vga_putchar(int c)
{
	static const int TAB_WIDTH = 8;

	const unsigned pos = 2 * (row * COLS + col);

	switch (c) {
	default:
		*(VGAMEM + pos) = c;
		*(VGAMEM + pos + 1) = CATTR;

		if (++col != COLS)
			return;
	case '\n':
		row = (row + 1) % ROWS;
		vga_newline();
	case '\r':
		col = 0;
		break;
	case '\t':
		for (int i = 0; i != TAB_WIDTH; ++i)
			vga_putchar(' ');
		break;
	}
}

static void clrscr(void)
{
	for (int i = 0; i != 2 * ROWS * COLS; i += 2) {
		*(VGAMEM + i) = ' ';
		*(VGAMEM + i + 1) = CATTR;
	}
}

static void vga_write(const char *str, unsigned long size)
{
	for (unsigned long i = 0; i != size; ++i)
		vga_putchar(str[i]);
}

void setup_vga(void)
{
	static struct console vga_console;

	vga_console.write = &vga_write;
	clrscr();
	register_console(&vga_console);
}
