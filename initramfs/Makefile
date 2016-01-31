CC ?= gcc
LD ?= gcc

CFLAGS := -g -m64
LFLAGS := -nostlib -z max-page-size=0x1000
SRC    := test.S
OBJ    := $(SRC:.S=.o)
DEP    := $(SRC:.S=.d)

test: $(OBJ) test.ld
	$(LD) $(LFLAGS) -T test.ld -o $@ $(OBJ)

%.o: %.S
	$(CC) -D__ASM_FILE__ -g -MMD -c $< -o $@

-include $(DEP)

.PHONY: clean
clean:
	rm -f test $(OBJ) $(DEP)
