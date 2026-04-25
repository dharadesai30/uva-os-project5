# a "lean" user program. minimum dependency: 
# ulib; no dependency on libc 

include $(abspath ../../config.mk)

CFLAGS  := -MMD -Wall -Werror -nostdlib -g -fno-omit-frame-pointer
CFLAGS  += -nostartfiles -ffreestanding -mgeneral-regs-only
CFLAGS  += -O2  # Os to shrink binary size, e..g to fit xv6 fs limit

LDFLAGS := 

# CFILES  := $(shell find . -name "*.c" ! -name "*sdl.c")
CFILES  := common.c  cpu-addressing.c  cpu.c  fce.c memory.c  mmc.c  ppu.c  psg.c
CFILES  += hal.c main.c 
OBJS    := $(CFILES:%.c=%.o)
LIB     := ../build-lib/libc-simple.a
# OBJS	+= ../ulib.o ../printf.o ../usys_s.o ../umalloc.o
# OBJS	+= ../uio.o ../tinyprintf.o

%.o: %.c
	$(VB) echo + CC $< "->" $@
	$(VB) $(CC) $(CFLAGS) -c -o $@ $<
 
nes: $(OBJS) $(LIB)
	$(VB) echo + LD "->" $@
	$(VB) aarch64-linux-gnu-ld $(LDFLAGS) -T nes.ld -o $@ $^

-include $(patsubst %.o, %.d, $(OBJS))

.PHONY: clean

clean:
	rm -rf nes *.o *.d
