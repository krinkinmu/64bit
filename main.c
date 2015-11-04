static char * const videomem = (char *)0xb8000;
static const char attr = 7;
static const unsigned rows = 25;
static const unsigned cols = 80;

static unsigned row, col;

static void newline(void)
{
	char * const ptr = videomem + 2 * row * cols;
	unsigned i;

	for (i = 0; i != cols; ++i) {
		*(ptr + 2 * i) = ' ';
		*(ptr + 2 * i + 1) = attr;
	}
}

static void putchar(int c)
{
	const unsigned pos = row * cols + col;

	switch (c) {
	default:
		*(videomem + pos * 2) = c;
		*(videomem + pos * 2 + 1) = attr;
		if (++col != cols)
			return;
	case '\n':
		row = (row + 1) % rows;
		newline();
	case '\r':
		col = 0;
		break;
	}
}

static void puts(const char *str)
{
	while (*str)
		putchar(*str++);
	putchar('\n');
}

static void ultoa(unsigned long value, char *str, int base)
{
	static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";

	unsigned pos = 0, i;

	do {
		str[pos++] = digits[value % base];
		value /= base;
	} while (value);

	for (i = 0; i != pos / 2; ++i) {
		const char tmp = str[i];
		str[i] = str[pos - i - 1];
		str[pos - i - 1] = tmp;
	}
	str[pos] = 0;
}

void main(void *ptr, const char *cmdline)
{
	unsigned i;

	(void) ptr;
	(void) ultoa;

	for (i = 0; i != rows * cols; ++i) {
		*(videomem + 2 * i) = ' ';
		*(videomem + 2 * i + 1) = attr;
	};

	puts(cmdline);

	while (1);
}
