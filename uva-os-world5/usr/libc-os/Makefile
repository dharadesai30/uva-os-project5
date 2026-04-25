# libc-os: our kernel's glue for libc (newlib)
# needed for any user apps that depend on libc

include $(abspath ../../config.mk)

# TARGETOBJS: *.o files to be linked directly (not as .a) 
# 	they shall be linked to target prog in specific order. 
#	b/c c/cpp apps need different crt0 (cf crt0.c|cpp), crt0 shall separate from 
# 	from libc-os which both c/cpp need to link to
TARGETOBJS = crt0.o crt0cpp.o crti.o crtn.o

# the lib to build, rules in librules.mk
TARGETLIB = libc-os.a
OBJS = syscall.o usys_s.o io.o

CFLAGS = -MMD -Wall -Werror -fno-omit-frame-pointer
CFLAGS += -nostdinc -nostdlib -nostartfiles -ffreestanding -mgeneral-regs-only
CFLAGS += -Os -std=c99
CFLAGS += -I $(VOS_REPO_PATH)
CFLAGS += -I $(LIBC_INC_PATH)

CFLAGS += -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function # STUDENT_TODO: replace this

# gcc specific, note diff paths for x64 ubuntu vs. aarch64 ubuntu (VM for Mac)
CFLAGS += -I /usr/lib/gcc-cross/aarch64-linux-gnu/9/include/
CFLAGS += -I /usr/lib/gcc/aarch64-linux-gnu/9/include/

ASMOPS += -I $(VOS_REPO_PATH)

# this must come frist 
all: ${TARGETLIB} ${TARGETOBJS}

# install: ${TARGETLIB} ${TARGETOBJS}
# 	@mkdir -p $(LIB_BUILD_PATH)
# 	cp -f $(TARGETLIB) $(LIB_BUILD_PATH)
# 	cp -f ${TARGETOBJS} $(LIB_BUILD_PATH)

include $(abspath ../librules.mk)

%.o: %.c 
	@echo  USER CC $< "->" $@
	@$(CC) $(CFLAGS) -MMD -c $< -o $@

%_s.o: %.S
	@echo  USER AS $< "->" $@
	@$(CC) $(ASMOPS) -MMD -c $< -o $@

clean: 
	rm -f *.o *.a *.d *.sym *.asm