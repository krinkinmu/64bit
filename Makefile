CC ?= gcc
LD ?= ld

CFLAGS := -m64 -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -ffreestanding \
	-mcmodel=large -O3 -Wall -Wextra -Werror -pedantic -std=c99
LFLAGS := -nostdlib -nostdinc

SRC := main.c list.c console.c vga.c string.c stdio.c ctype.c stdlib.c \
	vsnprintf.c balloc.c memory.c
OBJ := $(SRC:.c=.o)
DEP := $(SRC:.c=.d)

ASM := bootstrap.S videomem.S
AOBJ:= $(ASM:.S=.o)

all: kernel

kernel: $(AOBJ) $(OBJ) kernel.ld
	ld $(LFLAGS) -T kernel.ld -o $@ $(AOBJ) $(OBJ)

%.o: %.S
	$(CC) -m64 -mcmodel=large -c $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -MMD -c $< -o $@

-include $(DEP)

.PHONY: clean
clean:
	rm -f kernel $(AOBJ) $(OBJ) $(DEP)
