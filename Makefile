CC ?= gcc
LD ?= ld

CFLAGS := -m64 -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -ffreestanding \
	-mcmodel=kernel -O3 -Wall -Wextra -Werror -pedantic -std=c99
LFLAGS := -nostdlib -nostdinc

SRC := main.c list.c console.c vga.c string.c stdio.c ctype.c stdlib.c \
	vsnprintf.c balloc.c memory.c interrupt.c paging.c
OBJ := $(SRC:.c=.o)
DEP := $(SRC:.c=.d)

ASM := bootstrap.S videomem.S entry.S
AOBJ:= $(ASM:.S=.o)

all: kernel

kernel: $(AOBJ) $(OBJ) kernel.ld
	ld $(LFLAGS) -T kernel.ld -o $@ $(AOBJ) $(OBJ)

entry.S: genint.py
	python $^ > $@

%.o: %.S
	$(CC) -c $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -MMD -c $< -o $@

-include $(DEP)

.PHONY: clean
clean:
	rm -f kernel $(AOBJ) $(OBJ) $(DEP) entry.S
