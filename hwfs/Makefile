CC ?= gcc

CFLAGS := -g -m64 -Ofast -Wall -Wextra -Werror -pedantic -std=c99
LFLAGS :=

SRC := mkfs.c format.c cache.c rbtree.c
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
