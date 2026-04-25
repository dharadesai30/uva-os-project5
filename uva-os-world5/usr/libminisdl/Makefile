include $(abspath ../../config.mk)

TARGETLIB = libminisdl.a

OBJS = video.o keyboard.o event.o timer.o general.o 
OBJS += audio.o

COPS = -MMD -Wall -Werror -g -fno-omit-frame-pointer
COPS += -nostdinc -nostdlib -nostartfiles -ffreestanding
COPS += -Os
COPS += -I include
COPS += -I $(LIBC_INC_PATH)
COPS += -I $(LIBVOS_PATH)

# Temporarily allow unused variables to avoid compiler complaints.
# This should be commented out after completing all quests.
COPS += -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function # STUDENT_TODO: replace this

# gcc specific, note diff paths for x64 ubuntu vs. aarch64 ubuntu (VM for Mac)
COPS += -I /usr/lib/gcc-cross/aarch64-linux-gnu/9/include/
COPS += -I /usr/lib/gcc/aarch64-linux-gnu/9/include/

include $(abspath ../librules.mk)

%.o: src/%.c
	@echo  USER CC $< "->" $@
	@$(CC) $(COPS) -MMD -c $< -o $@

clean: 
	rm -f *.o $(OBJS) src/*.d *.d *.sym *.asm