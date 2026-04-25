# a music player program
# dependency: libc, libminisdl, libvorbis

include $(abspath ../../config.mk)

PROG = nplayer

OBJS = nplayer.o

# gcc options
CFLAGS += -MMD -Wall -Werror -g -fno-omit-frame-pointer
CFLAGS += -nostdinc -nostdlib -nostartfiles -ffreestanding
CFLAGS += -O2

# gcc specific, note diff paths for x64 ubuntu vs. aarch64 ubuntu (VM for Mac)
CFLAGS += -I /usr/lib/gcc-cross/aarch64-linux-gnu/9/include/
CFLAGS += -I /usr/lib/gcc/aarch64-linux-gnu/9/include/

# Temporarily allow unused variables to avoid compiler complaints.
# This should be commented out after completing all quests.
CFLAGS += -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function -Wno-unused-label # STUDENT_TODO: replace this

# lib dependency
LIBDEP += $(LIBC_BUILD_PATH)/libm.a
LIBDEP += $(LIBC_BUILD_PATH)/libc.a
LIBDEP += $(LIB_BUILD_PATH)/libc-os.a
LIBDEP += $(LIB_BUILD_PATH)/libminisdl.a
LIBDEP += $(LIB_BUILD_PATH)/libvorbis.a

# lib inc 
CFLAGS += -I $(LIBC_INC_PATH)
CFLAGS += -I $(LIBVOS_PATH)
CFLAGS += -I $(LIBVORBIS_PATH)
CFLAGS += -I $(LIBMINISDL_PATH)/include

LDFLAGS = -static -T ../user.ld -nostdlib -nostartfiles -ffreestanding -static
LDFLAGS += -L $(LIBC_BUILD_PATH)
LDFLAGS += -L $(LIB_BUILD_PATH)

# note the order
LIBS += -lvorbis -lminisdl -lc-os
LIBS += -lm
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
