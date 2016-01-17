CC ?= gcc

CFLAGS := -g -m64 -Ofast -Wall -Wextra -Werror -pedantic -std=c99 -D_BSD_SOURCE
LFLAGS :=

SRC := mkfs.c disk-io.c
OBJ := $(SRC:.c=.o)
DEB := $(SRC:.c=.d)

all: mkfs

mkfs: $(OBJ)
	$(CC) $(LFLAGS) $(OBJ) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -MMD -c $< -o $@

-include $(DEP)

.PHONY: clean
clean:
	rm -f mkfs $(OBJ) $(DEP)
