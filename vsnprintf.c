#include "vsnprintf.h"
#include <inttypes.h>
#include "string.h"
#include "stdlib.h"
#include "ctype.h"

enum format_flag {
	FF_SIGNED = (1 << 0),
	FF_ZEROPAD = (1 << 1),
	FF_SIGN = (1 << 2),
	FF_PREFIX = (1 << 3)
};

enum format_type {
	FT_NONE,
	FT_CHAR,
	FT_STR,
	FT_PERCENT,
	FT_PTR,
	FT_INTMAX,
	FT_SIZE,
	FT_PTRDIFF,
	FT_LLONG,
	FT_LONG,
	FT_INT,
	FT_SHORT,
	FT_BYTE,
	FT_INVALID
};

struct format_spec {
	unsigned long type;
	unsigned long flags;
	unsigned base;
	int qualifier;
	int width;
};

static int format_decode(const char *fmt, struct format_spec *spec)
{
	static const char *length_mod = "hljzt";

	const char *start = fmt;

	spec->type = FT_NONE;
	while (*fmt && *fmt != '%')
		++fmt;

	if (fmt != start || !*fmt)
		return fmt - start;

	spec->flags = 0;
	while (1) {
		int found = 1;
		++fmt;

		switch (*fmt) {
		case '+':
			spec->flags |= FF_SIGN;
			break;
		case '#':
			spec->flags |= FF_PREFIX;
			break;
		default:
			found = 0;
		}

		if (!found)
			break;
	};

	spec->width = 0;
	if (isdigit(*fmt))
		spec->width = strtol(fmt, (char **)&fmt, 10);

	spec->qualifier = 0;
	if (strchr(length_mod, *fmt)) {
		spec->qualifier = *fmt++;

		if (*fmt == spec->qualifier) {
			spec->qualifier = toupper(spec->qualifier);
			++fmt;
		}
	}

	spec->base = 10;
	switch (*fmt) {
	case 'c':
		spec->type = FT_CHAR;
		return ++fmt - start;
	case 's':
		spec->type = FT_STR;
		return ++fmt - start;
	case '%':
		spec->type = FT_PERCENT;
		return ++fmt - start;
	case 'p':
		spec->type = FT_PTR;
		spec->base = 16;
		spec->flags = FF_PREFIX;
		return ++fmt - start;
	case 'd':
	case 'i':
		spec->flags |= FF_SIGNED;
	case 'o':
	case 'x':
	case 'X':
		if (*fmt == 'o')
			spec->base = 8;
		if (*fmt == 'X' || *fmt == 'x')
			spec->base = 16;
	case 'u':
		break;
	default:
		spec->type = FT_INVALID;
	}

	switch (spec->qualifier) {
	case 'H':
		spec->type = FT_BYTE;
		break;
	case 'h':
		spec->type = FT_SHORT;
		break;
	case 'l':
		spec->type = FT_LONG;
		break;
	case 'L':
		spec->type = FT_LLONG;
		break;
	case 'j':
		spec->type = FT_INTMAX;
		break;
	case 'z':
		spec->type = FT_SIZE;
		break;
	case 't':
		spec->type = FT_PTRDIFF;
		break;
	default:
		spec->type = FT_INT;
		break;
	}

	return ++fmt - start;
}

static int untoa(uintmax_t value, char *str, int base)
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
	return pos;
}

static char *format_number(char *buf, char *end, uintmax_t value,
			const struct format_spec *spec)
{
	const intmax_t svalue = value;

	char buffer[256];
	const char *sign = "";
	const char *prefix = "";
	const char *num;
	char *ret;
	int len, padding;

	if ((spec->flags & FF_SIGNED) && svalue < 0) {
		len = untoa(-svalue, buffer, spec->base) + 1;
		sign = "-";
	} else {
		len = untoa(value, buffer, spec->base);
		if (spec->flags & FF_SIGN) {
			sign = "+";
			++len;
		}
	}
	num = buffer;

	if (spec->flags & FF_PREFIX) {
		if (spec->base == 8) {
			prefix = "0";
			++len;
		} else if (spec->base == 16) {
			prefix = "0x";
			len += 2;
		}
	}

	padding = 0;
	if (len < spec->width)
		padding = spec->width - len;
	ret = buf + len + padding;

	while (padding && buf < end) {
		*buf++ = ' ';
		--padding;
	}

	while (*sign && buf < end)
		*buf++ = *sign++;

	while (*prefix && buf < end)
		*buf++ = *prefix++;

	while (*num && buf < end)
		*buf++ = *num++;

	return ret;
}

int vsnprintf(char *buf, const size_t size, const char *fmt, va_list args)
{
	struct format_spec spec;
	char *str = buf, *end = str + size;

	while (*fmt) {
		const char *save = fmt;
		const int read = format_decode(fmt, &spec);

		fmt += read;

		switch (spec.type) {
		case FT_NONE: {
			int tocopy = read;

			if (str < end) {
				if (tocopy > end - str)
					tocopy = end - str;
				memcpy(str, save, tocopy);
			}
			str += read;
			break;
		}

		case FT_CHAR: {
			char c = va_arg(args, int);

			while (spec.width-- > 0) {
				if (str < end)
					*str = ' ';
				++str;
			}

			if (str < end)
				*str = c;
			++str;
			break;
		}

		case FT_STR: {
			const char *toprint = va_arg(args, const char *);
			const int len = strlen(toprint);

			while (spec.width > len) {
				if (str < end)
					*str = ' ';
				++str;
				--spec.width;
			}

			while (*toprint) {
				if (str < end)
					*str = *toprint;
				++str;
				++toprint;
			}
			break;
		}

		case FT_PERCENT:
			if (str < end)
				*str = '%';
			++str;
			break;

		case FT_INVALID:
			if (str < end)
				*str = '?';
			++str;
			break;

		default: {
			uintmax_t value;

			switch (spec.type) {
			case FT_PTR:
				value = (uintmax_t)va_arg(args, void *);
				break;
			case FT_LLONG:
				value = va_arg(args, long long);
				break;
			case FT_LONG:
				value = va_arg(args, long);
				break;
			case FT_INTMAX:
				value = va_arg(args, intmax_t);
				break;
			case FT_SIZE:
				value = va_arg(args, size_t);
				break;
			case FT_PTRDIFF:
				value = va_arg(args, ptrdiff_t);
				break;
			default:
				value = va_arg(args, int);
				break;
			}

			str = format_number(str, end, value, &spec);
			break;
		}

		}
	}

	if (size) {
		if (str >= end)
			*(end - 1) = 0;
		else
			*str = 0;
	}
	return str - buf;
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list args;
	int cs;

	va_start(args, fmt);
	cs = vsnprintf(buf, size, fmt, args);
	va_end(args);
	return cs;
}
