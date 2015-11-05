CC ?= gcc
LD ?= ld

CFLAGS := -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -ffreestanding -O3 \
	-Wall -Wextra -Werror -pedantic
LFLAGS := -s -nostdlib -nostdinc

SRC := main.c list.c console.c vga.c string.c stdio.c
OBJ := $(SRC:.c=.o)
DEP := $(SRC:.c=.d)

ASM := bootstrap.S videomem.S
AOBJ:= $(ASM:.S=.o)

all: kernel

kernel: $(AOBJ) $(OBJ) kernel.ld
	ld $(LFLAGS) -T kernel.ld -o $@ $(AOBJ) $(OBJ)

%.o: %.S
	$(CC) -m64 -c $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -MMD -c $< -o $@

-include $(DEP)

.PHONY: clean
clean:
	rm -f kernel $(AOBJ) $(OBJ) $(DEP)
