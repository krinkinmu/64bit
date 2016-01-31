CC ?= gcc
LD ?= ld

CFLAGS := -g -m64 -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -ffreestanding \
	-mcmodel=kernel -Wall -Wextra -Werror -pedantic -std=c99 \
	-Wframe-larger-than=4096 -Wstack-usage=4096 -Wno-unknown-warning-option
LFLAGS := -nostdlib -z max-page-size=0x1000

SRC := main.c list.c console.c vga.c string.c stdio.c ctype.c stdlib.c \
	vsinkprintf.c balloc.c memory.c interrupt.c paging.c i8259a.c \
	kmem_cache.c threads.c time.c scheduler.c vfs.c rbtree.c ramfs.c \
	error.c ramfs_smoke_test.c locking.c ide.c ide_smoke_test.c misc.c \
	initramfs.c serial.c mm.c exec.c
OBJ := $(SRC:.c=.o)
DEP := $(SRC:.c=.d)

ASM := bootstrap.S videomem.S entry.S switch.S
AOBJ:= $(ASM:.S=.o)
ADEP:= $(ASM:.S=.d)

all: kernel

kernel: $(AOBJ) $(OBJ) kernel.ld
	$(LD) $(LFLAGS) -T kernel.ld -o $@ $(AOBJ) $(OBJ)

entry.S: genint.py
	python $^ > $@

%.o: %.S
	$(CC) -D__ASM_FILE__ -g -MMD -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -MMD -c $< -o $@

-include $(DEP)
-include $(ADEP)

.PHONY: clean
clean:
	rm -f kernel $(AOBJ) $(OBJ) $(DEP) $(ADEP) entry.S
