include $(abspath ../../config.mk)

PROG = menu

OBJS = menu.o menu-sh.o

# gcc options
COPS += -MMD -Wall -Werror -g -fno-omit-frame-pointer
COPS += -nostdinc -nostdlib -nostartfiles -ffreestanding
COPS += -O2

# gcc specific 
COPS += -I /usr/lib/gcc-cross/aarch64-linux-gnu/9/include/
COPS += -I /usr/lib/gcc/aarch64-linux-gnu/9/include/

# lib dependency
LIBDEP += $(LIBC_BUILD_PATH)/libc.a
LIBDEP += $(LIB_BUILD_PATH)/libc-os.a
LIBDEP += $(LIB_BUILD_PATH)/libminisdl.a

# lib inc 
COPS += -I $(LIBC_INC_PATH)
COPS += -I $(LIBVOS_PATH)
COPS += -I $(LIBMINISDL_PATH)/include

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
	$(VB) $(CC) $(COPS) -o $@ -c $<

clean:
	rm -f $(OBJS) $(PROG)
