# a minimum userspace lib for "simple userspace"

include $(abspath ../../config.mk)

TARGETLIB = libc-simple.a

OBJS = printf.o tinyprintf.o ulibc.o usys_s.o umalloc.o uio.o

COPS = -MMD -Wall -Werror -g -fno-omit-frame-pointer
COPS += -nostdinc -nostdlib -nostartfiles -ffreestanding
COPS += -O2
COPS += -I..
# gcc specific, note diff paths for x64 ubuntu vs. aarch64 ubuntu (VM for Mac)
COPS += -I /usr/lib/gcc-cross/aarch64-linux-gnu/9/include/
COPS += -I /usr/lib/gcc/aarch64-linux-gnu/9/include/

include $(abspath ../librules.mk)

%.o: %.c
	$(VB) echo  USER CC $< "->" $@
	$(VB) $(CC) $(COPS) -MMD -c $< -o $@

%_s.o: %.S
	$(VB) echo  USER AS $< "->" $@
	$(VB) $(CC) $(ASMOPS) -MMD -c $< -o $@

clean: 
	rm -f *.o $(OBJS) *.d *.sym *.asm