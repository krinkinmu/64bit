static char * const videomem = (char *)0xb8000;
static const char attr = 7;
static const unsigned rows = 25;
static const unsigned cols = 80;

void main(void)
{
	unsigned i;

	// clear screen just to test memory access
	for (i = 0; i != rows * cols; ++i) {
		*(videomem + 2 * i) = ' ';
		*(videomem + 2 * i + 1) = attr;
	};

	while (1);
}
