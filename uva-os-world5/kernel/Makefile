include $(abspath ../config.mk)

ifeq ($(filter $(PLAT),virt rpi3qemu rpi3),)
$(error Invalid PLAT value. do "export PLAT=rpi3qemu" (for qemu build) or "export PLAT=rpi3" (for rpi3 build))
endif

# support for fat32 fs
# comment out == disable fatfs & secondary device
CONFIG_FAT ?= 1

BUILD_DIR = build-$(PLAT)
SRC_DIR = .

ARMGNU ?= aarch64-linux-gnu
COPS = -Wall -Werror -nostdlib -nostartfiles -ffreestanding \
-I$(SRC_DIR) \
-Iaddon/usb/include\
-mgeneral-regs-only \
-g 

# the default debug level, to be overridden by less verbose levels in individual files 
COPS += -DCONFIG_KAGE_GLOBAL_DEBUG_LEVEL=10   # verbose
# COPS += -DCONFIG_KAGE_GLOBAL_DEBUG_LEVEL=30   # info
# COPS += -DCONFIG_KAGE_GLOBAL_DEBUG_LEVEL=40   # warning

# Temporarily allow unused variables to avoid compiler complaints.
# This should be commented out after completing all quests.
COPS += -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function # STUDENT_TODO: replace this

ASMOPS = -I$(SRC_DIR)  -g 

##### platform specific flags, targets ########
ifeq (${PLAT}, virt)
COPS += -DPLAT_VIRT
ASMOPS += -DPLAT_VIRT
LINKSCR = $(SRC_DIR)/linker-virt.ld
KERNEL = kernel8-virt.img
endif

ifeq (${PLAT}, rpi3qemu)
COPS += -DPLAT_RPI3QEMU
ASMOPS += -DPLAT_RPI3QEMU
LINKSCR = $(SRC_DIR)/linker-rpi3qemu.ld
KERNEL = kernel8-rpi3qemu.img
endif

# if SCTLR_EL1 specifies alginment check, we must specify -mstrict-align here,
# otherwise compiler will issue unaligned access -- memory exception. 
ifeq (${PLAT}, rpi3)
COPS += -DPLAT_RPI3 -mcpu=cortex-a53+fp+simd -mstrict-align
ASMOPS += -DPLAT_RPI3
LINKSCR = $(SRC_DIR)/linker-rpi3.ld
KERNEL = kernel8-rpi3.img
endif

##### feature specific flags ####
ifeq (${CONFIG_FAT}, 1)
COPS += -DCONFIG_FAT
endif
#################################

##### the flags for certain .c files that must use -O0
COPS1 = $(COPS)
COPS1 += -O0 	
#################################

# generic
COPS += -O2
# COPS += -O0

# for stacktrace purpose
COPS += -fno-omit-frame-pointer -Wno-frame-address 

BUILD_INFO_HEADER = build_info.h

.PHONY: force_rebuild

# must come before any target 
all: $(BUILD_INFO_HEADER)
	$(MAKE) $(KERNEL)

clean:
	rm -rf $(BUILD_DIR) *.img $(BUILD_INFO_HEADER) *.img *.asm *.sym

# use -O0 only for user.c (launcher), for which -O2 emits references to kernel va and crashes things
$(BUILD_DIR)/user_c.o: $(SRC_DIR)/user.c
	@echo  KERNEL CC $< "->" $@
	$(VB) mkdir -p $(@D)
	$(VB) $(ARMGNU)-gcc-9 $(COPS1) -MMD -c $< -o $@

$(BUILD_DIR)/%_c.o: $(SRC_DIR)/%.c
	@echo  KERNEL CC $< "->" $@
	$(VB) mkdir -p $(@D)
	$(VB) $(ARMGNU)-gcc-9 $(COPS) -MMD -c $< -o $@

# font build rules. 
# NB: the symbols in the binary will have prefix "_binary". 
$(BUILD_DIR)/%_psf.o: %.psf
	$(VB) mkdir -p $(@D)
	$(VB) $(ARMGNU)-ld -r -b binary -o $@ $<

# ramdisk build rules
#$(BUILD_DIR)/%_bin.o: %.bin
$(BUILD_DIR)/sd_bin.o: $(USER_PATH)/sd.bin
	$(VB) mkdir -p $(@D)
	$(VB) $(ARMGNU)-ld -r -b binary -o $@ $<

# asm rule 
$(BUILD_DIR)/%_s.o: $(SRC_DIR)/%.S
	@echo  KERNEL AS $< "->" $@
	$(VB) $(ARMGNU)-gcc-9 $(ASMOPS) -MMD -c $< -o $@

# addon: usb lib
addon/usb/lib/libuspi.a:
	cd addon/usb/lib && $(MAKE)

# generic kernel
C_OBJS = $(BUILD_DIR)/bio_c.o
C_OBJS += $(BUILD_DIR)/console_c.o
C_OBJS += $(BUILD_DIR)/exec_c.o
C_OBJS += $(BUILD_DIR)/file_c.o
C_OBJS += $(BUILD_DIR)/fs_c.o
C_OBJS += $(BUILD_DIR)/irq_c.o
C_OBJS += $(BUILD_DIR)/kernel_c.o
C_OBJS += $(BUILD_DIR)/log_c.o
C_OBJS += $(BUILD_DIR)/vm_c.o
C_OBJS += $(BUILD_DIR)/alloc_c.o
C_OBJS += $(BUILD_DIR)/pipe_c.o
C_OBJS += $(BUILD_DIR)/printf_c.o
C_OBJS += $(BUILD_DIR)/ramdisk_c.o
C_OBJS += $(BUILD_DIR)/sched_c.o
C_OBJS += $(BUILD_DIR)/sleeplock_c.o
C_OBJS += $(BUILD_DIR)/spinlock_c.o
C_OBJS += $(BUILD_DIR)/string_c.o
C_OBJS += $(BUILD_DIR)/sys_c.o
C_OBJS += $(BUILD_DIR)/sysfile_c.o
C_OBJS += $(BUILD_DIR)/timer_c.o
C_OBJS += $(BUILD_DIR)/user_c.o
C_OBJS += $(BUILD_DIR)/mbox_c.o
C_OBJS += $(BUILD_DIR)/unittests_c.o
# surface flinger 
C_OBJS += $(BUILD_DIR)/sf_c.o
# backtrace support
C_OBJS += $(BUILD_DIR)/stacktrace_c.o
# kernel console 
C_OBJS += $(BUILD_DIR)/kconsole_c.o

ASM_OBJS = $(BUILD_DIR)/boot_s.o
ASM_OBJS += $(BUILD_DIR)/entry_s.o
ASM_OBJS += $(BUILD_DIR)/sched_s.o
ASM_OBJS += $(BUILD_DIR)/user_sys_s.o
ASM_OBJS += $(BUILD_DIR)/utils_s.o

# binary objs: ramdisk image, font
BIN_OBJS = $(BUILD_DIR)/sd_bin.o
BIN_OBJS += $(BUILD_DIR)/font_psf.o

# ------- Rpi3 QEMU specific source -------
ifeq (${PLAT}, rpi3qemu)
C_OBJS += $(BUILD_DIR)/mini_uart_c.o
# usb glue
C_OBJS += $(BUILD_DIR)/uspibind_c.o		
C_OBJS += $(BUILD_DIR)/kb_c.o
# sound. only an interface. do nothing
C_OBJS += $(BUILD_DIR)/sound-emu_c.o
# usb devices (kb, gamepad, etc
BIN_OBJS += addon/usb/lib/libuspi.a		
# (sound support - NA)
# sd card
C_OBJS += $(BUILD_DIR)/sd_c.o
# C_OBJS += $(BUILD_DIR)/gpio_c.o # test only
endif

# ------- Rpi3 specific source -------
ifeq (${PLAT}, rpi3)
C_OBJS += $(BUILD_DIR)/mini_uart_c.o
# usb glue
C_OBJS += $(BUILD_DIR)/uspibind_c.o		
C_OBJS += $(BUILD_DIR)/kb_c.o
# sound 		
C_OBJS += $(BUILD_DIR)/sound_c.o
C_OBJS += $(BUILD_DIR)/gpio_c.o
C_OBJS += $(BUILD_DIR)/machineinfo_c.o	
# usb devices (kb, gamepad, etc
BIN_OBJS += addon/usb/lib/libuspi.a
# C_OBJS += $(BUILD_DIR)/ampibind_c.o		# sound(vchiq) glue
# BIN_OBJS += addon/sound/ampi/libampi.a		# hdmi sound lib
# sd card
C_OBJS += $(BUILD_DIR)/sd_c.o
endif

ifeq (${PLAT}, virt)
C_OBJS += $(BUILD_DIR)/pl011_c.o
C_OBJS += $(BUILD_DIR)/virtio_disk_c.o
endif

##### feature specific source ####
ifeq (${CONFIG_FAT}, 1)
C_OBJS += $(BUILD_DIR)/ff_c.o
# needed to support fat long file names (LFN) beyond 8.3 format
C_OBJS += $(BUILD_DIR)/ffunicode_c.o  
endif
#################################

DEP_FILES = $(C_OBJS:%.o=%.d)
-include $(DEP_FILES)

#################################
# build info 
$(BUILD_INFO_HEADER): force_rebuild
	$(VB) echo "GEN BUILD INFO"
	@echo "#ifndef BUILD_INFO_H" > $(BUILD_INFO_HEADER)
	@echo "#define BUILD_INFO_H" >> $(BUILD_INFO_HEADER)
	@echo "#define BUILD_DATE \"$$(date '+%Y-%m-%d %H:%M:%S')\"" >> $(BUILD_INFO_HEADER)
	@echo "#define BUILD_MACHINE \"$$(hostname)\"" >> $(BUILD_INFO_HEADER)
	@echo "#define GIT_COMMIT \"$$(git rev-parse --short HEAD)\"" >> $(BUILD_INFO_HEADER)
	@echo "#endif // BUILD_INFO_H" >> $(BUILD_INFO_HEADER)

#################################

BOOTFS_DIR=/media/$(USER)/bootfs

$(KERNEL): $(LINKSCR) $(C_OBJS) $(ASM_OBJS) $(BIN_OBJS)
	$(VB) echo "KERNEL LD"
	$(VB) $(ARMGNU)-ld -T $(LINKSCR) -o $(BUILD_DIR)/kernel8.elf $(C_OBJS) $(ASM_OBJS) $(BIN_OBJS)
	$(VB) $(ARMGNU)-objcopy $(BUILD_DIR)/kernel8.elf -O binary $(KERNEL)
	$(VB) $(ARMGNU)-objdump -dS $(BUILD_DIR)/kernel8.elf > kernel8.asm
	$(VB) nm --numeric-sort $(BUILD_DIR)/kernel8.elf > kernel8.sym
	$(VB) echo KERNEL INSTALL
	# WSL2: copy the kernel img to local dir, if exists
	$(VB) if [ -d /mnt/c/data/tmp ]; then \
		cp $(KERNEL) /mnt/c/data/tmp/$(KERNEL); \
	fi
	# VM/Linux: copy to dir like "/media/student/bootfs/"
	$(VB) if [ -d $(BOOTFS_DIR) ]; then \
		if [ ! -f $(BOOTFS_DIR)/$(KERNEL) ]; then \
			echo "Warning: Kernel $(KERNEL) does not exist in $(BOOTFS_DIR). Do you want to copy it there? (y/n)"; \
			read CONFIRMATION; \
			if [ "$$CONFIRMATION" = "y" ]; then \
				cp $(KERNEL) $(BOOTFS_DIR)/$(KERNEL); \
			else \
				echo "Skipping kernel copy to $(BOOTFS_DIR)."; \
			fi; \
		else \
			cp $(KERNEL) $(BOOTFS_DIR)/$(KERNEL); \
		fi; \
	fi
