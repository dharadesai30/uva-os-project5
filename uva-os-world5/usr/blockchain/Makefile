# based on
# https://github.com/NJU-ProjectN/am-kernels/tree/master/kernels/blockchain

include $(abspath ../../config.mk)

OBJS=\
Blockchain_x.o \
Block_x.o \
main_x.o \
sha256_x.o

# cpptest_x.o \

CXXFLAGS = -fno-rtti -fno-exceptions -std=c++17
CXXFLAGS += -fno-use-cxa-atexit
CXXFLAGS += -O2 -g
# CXXFLAGS += -O0 -g
CXXFLAGS += -mcpu=cortex-a53+fp+simd

# lib inc 
CXXFLAGS += -I $(LIBC_INC_PATH)
CXXFLAGS += -I $(LIBVOS_PATH)

# CPPLDFLAGS = -static -T usercpp.ld -nostdlib -nostartfiles -ffreestanding -static
CPPLDFLAGS = -static -T ../user.ld -nostdlib -nostartfiles -ffreestanding -static
CPPLDFLAGS += -L $(LIBC_BUILD_PATH)
CPPLDFLAGS += -L $(LIB_BUILD_PATH)

LIBS += -lc -lc-os -lc
# needed by strtod.c
LIBS += -lm
# needed for soft fp routines. e.g. __trunctfdf2 
LIBS += -lgcc

# below: stripping dbg info shrinks executable ~500KB -> ~100KB
blockchain: $(OBJS)
	$(CPP) $(CPPLDFLAGS) -o $@ $(CRT0CPP) $(CRTI) $^ $(CRTN) $(LIBS)
	$(ARMGNU)-objdump -dS $@ > $@.asm

clean:
	rm -f $(OBJS) blockchain

%_x.o: %.cpp
	$(CPP) -c $(CXXFLAGS) -o $@ $<
