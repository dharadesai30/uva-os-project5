# fxl Apr 2024 mandates using gcc-9 (not gcc 11 which is default in Ubuntu 22.04)
include $(abspath ../config.mk)

# xv6 userspace lib, minimalist, no dep on libc
# ULIB = ulib.o printf.o usys_s.o umalloc.o tinyprintf.o uio.o

# xv6 user programs only dep on ULIB above, no libc, etc
UPROGS = \
	build/echo \
	build/ls \
	build/mkdir \
	build/forktest \
	build/usertests \
	build/sh	\
	build/cat	\
	build/grep	\
	build/kill	\
	build/ln	\
	build/rm	\
	build/wc	\
	build/sleep

# apps
# 	UI apps with low dependency 
UPROGS += build/slider
UPROGS += build/buzz
UPROGS += build/nes
# 	UI apps with rich dependency (libc, sdl, & etc
UPROGS += build/nplayer
UPROGS += build/sysmon
UPROGS += build/nes1
UPROGS += build/menu
# 	cpp, depends on libc
UPROGS += build/blockchain

COPS = -MMD -Wall -Werror -nostdlib -g -fno-omit-frame-pointer
COPS += -nostartfiles -ffreestanding -mgeneral-regs-only
COPS += -O0

# Temporarily allow unused variables to avoid compiler complaints.
# This should be commented out after completing all quests.
COPS += -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function # STUDENT_TODO: replace this

ASMOPS = -g

.PRECIOUS: $(ULIB)

all: sd.bin

# below: include all files under build/ in sd.bin, except *.debug which are elf files with debug info
sd.bin: $(UPROGS) mkfs
	$(VB) rm -f sd.bin
	$(VB) cp font.psf build/font.psf
	$(VB) cp logo.txt build/logo.txt
	$(VB) cp initrc.txt build/initrc.txt
	$(VB) cd build && ../mkfs ../sd.bin `ls | grep -v '\.debug$$'` || (echo "sd.bin FAILED"; rm -f ../sd.bin; exit 1)

ulib.a: $(ULIB)
	$(VB) rm -f $@
	$(VB) ar rc $@ $(ULIB)
	$(VB) ranlib $@

mkfs: mkfs.c
	$(VB) gcc -Werror -Wall -I. -o $@ $^

%.o: %.c
	$(VB) echo  USER CC $< "->" $@
	$(VB) $(CC) $(COPS) -MMD -c $< -o $@

%_s.o: %.S
	$(VB) echo  USER AS $< "->" $@
	$(VB) $(CC) $(ASMOPS) -MMD -c $< -o $@

##################
# apps that have their own source dirs
# TODO complete lib dependency
build/nplayer: build-lib/libc-os.a build-lib/libminisdl.a build-lib/libvorbis.a
	cd nplayer && $(MAKE)
	cp nplayer/nplayer build/nplayer.debug
	$(ARMGNU)-objcopy --strip-debug nplayer/nplayer build/nplayer

build/sysmon: build-lib/libc-os.a build-lib/libminisdl.a
	cd sysmon && $(MAKE)
	cp sysmon/sysmon build/sysmon.debug
	$(ARMGNU)-objcopy --strip-debug sysmon/sysmon build/sysmon
	
build/menu: build-lib/libc-os.a build-lib/libminisdl.a
	cd menu && $(MAKE)
	cp menu/menu build/menu.debug
	$(ARMGNU)-objcopy --strip-debug menu/menu build/menu

# TODO complete lib dependency
build/blockchain:
	cd blockchain && $(MAKE)
	cp blockchain/blockchain build/blockchain.debug
	$(ARMGNU)-objcopy --strip-debug blockchain/blockchain build/blockchain

build/nes: build-lib/libc-simple.a
	cd LiteNES && $(MAKE)
	cp LiteNES/nes build/nes.debug
	$(ARMGNU)-objcopy --strip-debug LiteNES/nes build/nes

# nes using sdl 
build/nes1: build-lib/libc-os.a build-lib/libminisdl.a
	cd LiteNES && $(MAKE) -f Makefile.sdl
	cp LiteNES/nes1 build/nes1.debug
	$(ARMGNU)-objcopy --strip-debug LiteNES/nes1 build/nes1

# to prevent makefile from its implicit rule which is shorter & preferred
# https://www.gnu.org/software/make/manual/html_node/Canceling-Rules.html
% : %.c

##################
# user libs 
build-lib/libc-os.a:
	cd libc-os && $(MAKE) install

build-lib/libvorbis.a:
	cd libvorbis && $(MAKE) install 

build-lib/libminisdl.a:
	cd libminisdl && $(MAKE) install 

build-lib/libc-simple.a:
	cd libc-simple && $(MAKE) install

######################
#  "lean" user apps, simple rule for building
build/%: %.o build-lib/libc-simple.a
	$(VB) echo  USER LD $< "->" $@
	$(VB) mkdir -p build
	$(VB) $(ARMGNU)-ld $(LDFLAGS) -T user.ld -o $@ $^
	$(VB) $(ARMGNU)-objdump -S $@ > $*.asm
	$(VB) $(ARMGNU)-objdump -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $*.sym

clean: 
	$(VB) cd libminisdl && $(MAKE) clean
	$(VB) cd libc-os && $(MAKE) clean
	$(VB) cd libvorbis && $(MAKE) clean
	$(VB) cd libc-simple && $(MAKE) clean
	$(VB) cd LiteNES && $(MAKE) clean
	$(VB) cd nplayer && $(MAKE) clean
	$(VB) cd sysmon && $(MAKE) clean
	$(VB) cd blockchain && $(MAKE) clean	
	$(VB) cd menu && $(MAKE) clean
	rm -f *.o $(UPROGS) build-lib/*.a build-lib/*.o mkfs *.d *.sym *.asm sd.bin