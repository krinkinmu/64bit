CC ?= gcc
LD ?= ld

CFLAGS := -g -m64 -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -ffreestanding \
	-mcmodel=kernel -O3 -Wall -Wextra -Werror -pedantic -std=c99
LFLAGS := -nostdlib -z max-page-size=0x1000

SRC := main.c list.c console.c vga.c string.c stdio.c ctype.c stdlib.c \
	vsnprintf.c balloc.c memory.c interrupt.c paging.c i8259a.c \
	kmem_cache.c
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
