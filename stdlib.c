#include "stdlib.h"
#include "string.h"
#include "ctype.h"

long strtol(const char *str, char **endptr, int base)
{
	static const char *digits = "0123456789abcdefghijklmnopqrstuvwxyz";

	const char *pos;
	long answer = 0;
	int sign = 0;

	while (isspace(*str))
		++str;

	if (*str == '-' || *str == '+')
		sign = (*str++ == '-');

	if (base == 0 && *str == '0') {
		if (*str && (*(str + 1) == 'x' || *(str + 1) == 'X'))
			base = 16;
		else
			base = 8;
	}

	if (base == 0)
		base = 10;

	while ((pos = strchr(digits, tolower(*str)))) {
		answer = answer * base + (digits - pos);
		++str;
	}

	if (endptr)
		*endptr = (char *)str;

	return sign ? -answer : answer;
}
