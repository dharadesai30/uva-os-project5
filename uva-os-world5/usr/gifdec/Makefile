# a simple test program for png decoding
# dependency: libc, libminisdl

include $(abspath ../../config.mk)

PROG = gifdec-ex

OBJS = gifdec.o gifdec-ex.o

# gcc options
CFLAGS += -MMD -Wall -Werror -g -fno-omit-frame-pointer
CFLAGS += -nostdinc -nostdlib -nostartfiles -ffreestanding
CFLAGS += -Wno-error=maybe-uninitialized
# CFLAGS += -O2
# about 1.5x improvement
CFLAGS += -O3
CFLAGS += -ftree-vectorize -ffast-math 
CFLAGS += -march=armv8-a

# gcc specific, note diff paths for x64 ubuntu vs. aarch64 ubuntu (VM for Mac)
CFLAGS += -I /usr/lib/gcc-cross/aarch64-linux-gnu/9/include/
CFLAGS += -I /usr/lib/gcc/aarch64-linux-gnu/9/include/

# Temporarily allow unused variables to avoid compiler complaints.
# This should be commented out after completing all quests.
CFLAGS += -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function # STUDENT_TODO: replace this

# lib dependency
LIBDEP += $(LIBC_BUILD_PATH)/libc.a
LIBDEP += $(LIB_BUILD_PATH)/libc-os.a
LIBDEP += $(LIB_BUILD_PATH)/libminisdl.a

# lib inc 
CFLAGS += -I .
CFLAGS += -I $(LIBC_INC_PATH)
CFLAGS += -I $(LIBVOS_PATH)
CFLAGS += -I $(LIBMINISDL_PATH)/include

LDFLAGS = -static -T ../user.ld -nostdlib -nostartfiles -ffreestanding -static
LDFLAGS += -L $(LIBC_BUILD_PATH)
LDFLAGS += -L $(LIB_BUILD_PATH)

# note the order
LIBS += -lminisdl -lc-os
LIBS += -lc
LIBS += -lgcc

all: $(PROG)

$(PROG): $(OBJS) $(LIBDEP)
	$(VB) $(CC) $(LDFLAGS) $(CRT0) $(OBJS) $(LIBS)  -o $@
	
%.o: %.c
	$(VB) $(CC) $(CFLAGS) -o $@ -c $<

clean:
	rm -f $(OBJS) $(PROG) 
	rm -f *.d
