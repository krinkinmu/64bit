CC ?= gcc
LD ?= gcc

CFLAGS := -g -m64 -ffreestanding
LFLAGS := -nostlib -z max-page-size=0x1000

ASRC   := libsys.S
AOBJ   := $(ASRC:.S=.o)
ADEP   := $(ASRC:.S=.d)

SRC    := ctype.c string.c stdlib.c vsnprintf.c main.c
OBJ    := $(AOBJ) $(SRC:.c=.o)
DEP    := $(ADEP) $(SRC:.c=.d)

test: $(OBJ) test.ld
	$(LD) $(LFLAGS) -T test.ld -o $@ $(OBJ)

%.o: %.c
	$(CC) -g -MMD $(CFLAGS) -c $< -o $@

%.o: %.S
	$(CC) -D__ASM_FILE__ -g -MMD -c $< -o $@

-include $(DEP)

.PHONY: clean
clean:
	rm -f test $(OBJ) $(DEP)
