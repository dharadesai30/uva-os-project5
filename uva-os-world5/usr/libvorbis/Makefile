include $(abspath ../../config.mk)

TARGETLIB = libvorbis.a

OBJS = stb_vorbis.o

COPS = -MMD -Wall -Werror -g -fno-omit-frame-pointer
COPS += -nostdinc -nostdlib -nostartfiles -ffreestanding
COPS += -O2
COPS += -I include
COPS += -I $(LIBC_INC_PATH)

# gcc specific, note diff paths for x64 ubuntu vs. aarch64 ubuntu (VM for Mac)
COPS += -I /usr/lib/gcc-cross/aarch64-linux-gnu/9/include/
COPS += -I /usr/lib/gcc/aarch64-linux-gnu/9/include/

include $(abspath ../librules.mk)

%.o: %.c
	@echo  USER CC $< "->" $@
	@$(CC) $(COPS) -MMD -c $< -o $@

clean: 
	rm -f *.o $(OBJS) *.d *.sym *.asm