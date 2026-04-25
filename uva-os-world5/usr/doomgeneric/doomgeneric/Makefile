################################################################
#		fxl: based on -sdl makefile
# cf makeall.sh on rebuilding & packing sd.bin

include $(abspath ../../../config.mk)

PROG=doom
# subdirectory for objects
OBJDIR=build

# gcc options
CFLAGS+=-ggdb3 -O2    # -Os (now has some bug with this  .. page permission
CFLAGS += -nostdlib -nostdinc -nostartfiles -ffreestanding
CFLAGS+=-Wall -DNORMALUNIX -DLINUX -DSNDSERV -D_DEFAULT_SOURCE # -DUSEASM
#CFLAGS+=-DFEATURE_SOUND $(SDL_CFLAGS)

# Temporarily allow unused variables to avoid compiler complaints.
# This should be commented out after completing all quests.
CFLAGS += -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function -Wno-unused-label # STUDENT_TODO: replace this

# gcc specific, note diff paths for x64 ubuntu vs. aarch64 ubuntu (VM for Mac)
CFLAGS += -I /usr/lib/gcc-cross/aarch64-linux-gnu/9/include/
CFLAGS += -I /usr/lib/gcc/aarch64-linux-gnu/9/include/

# lib dependency
LIBDEP += $(LIBC_BUILD_PATH)/libm.a
LIBDEP += $(LIBC_BUILD_PATH)/libc.a
LIBDEP += $(LIB_BUILD_PATH)/libc-os.a
LIBDEP += $(LIB_BUILD_PATH)/libminisdl.a

# lib inc 
CFLAGS += -I $(LIBC_INC_PATH)
CFLAGS += -I $(LIBVOS_PATH)
CFLAGS += -I $(LIBMINISDL_PATH)/include

LDFLAGS += -Wl,--gc-sections
LDFLAGS += -static -T user.ld -nostdlib -nostartfiles -ffreestanding -static
LDFLAGS += -L $(LIBC_BUILD_PATH)
LDFLAGS += -L $(LIB_BUILD_PATH)

# note the order
LIBS += -lminisdl -lc-os
LIBS += -lm
LIBS += -lc
LIBS += -lgcc

SRC_DOOM = dummy.o am_map.o doomdef.o doomstat.o dstrings.o d_event.o d_items.o d_iwad.o d_loop.o d_main.o d_mode.o d_net.o f_finale.o f_wipe.o g_game.o hu_lib.o hu_stuff.o info.o i_cdmus.o i_endoom.o i_joystick.o i_scale.o i_sound.o i_system.o i_timer.o memio.o m_argv.o m_bbox.o m_cheat.o m_config.o m_controls.o m_fixed.o m_menu.o m_misc.o m_random.o p_ceilng.o p_doors.o p_enemy.o p_floor.o p_inter.o p_lights.o p_map.o p_maputl.o p_mobj.o p_plats.o p_pspr.o p_saveg.o p_setup.o p_sight.o p_spec.o p_switch.o p_telept.o p_tick.o p_user.o r_bsp.o r_data.o r_draw.o r_main.o r_plane.o r_segs.o r_sky.o r_things.o sha1.o sounds.o statdump.o st_lib.o st_stuff.o s_sound.o tables.o v_video.o wi_stuff.o w_checksum.o w_file.o w_main.o w_wad.o z_zone.o w_file_stdc.o i_input.o i_video.o doomgeneric.o 
SRC_DOOM += doomgeneric_minisdl.o
# SRC_DOOM += mus2mid.o i_sdlmusic.o i_sdlsound.o
OBJS += $(addprefix $(OBJDIR)/, $(SRC_DOOM))

all:	 $(PROG)

clean:
	rm -rf $(OBJDIR)
	rm -f $(OUTPUT)
	rm -f $(OUTPUT).gdb
	rm -f $(OUTPUT).map

# make and install ...(debug and stripped versions)

$(PROG): $(OBJS) $(LIBDEP)
	@echo [Linking $@]
	$(VB) $(CC) $(LDFLAGS) $(CRT0) $(OBJS) $(LIBS) -o $@
	cp $(PROG) $(USER_PATH)/build/$(PROG).debug
	$(ARMGNU)-strip $(PROG)
	cp $(PROG) $(USER_PATH)/build/$(PROG)

#	$(VB) $(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) \
#	-o $(OUTPUT) $(LIBS)

$(OBJS): | $(OBJDIR)

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/%.o:	%.c
	@echo [Compiling $<]
	$(VB) $(CC) $(CFLAGS) -c $< -o $@

print:
	@echo OBJS: $(OBJS)

