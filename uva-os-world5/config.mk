# will be included by all targets, kernel & user apps

ARMGNU ?= aarch64-linux-gnu
CC = $(ARMGNU)-gcc-9
CPP = $(ARMGNU)-g++-9

# requires it to be a git repo. is there a better way?
VOS_REPO_PATH := $(shell git rev-parse --show-toplevel)
KERNEL_PATH=$(VOS_REPO_PATH)/kernel
USER_PATH=$(VOS_REPO_PATH)/usr

#  best guess libc location
VOS_HOME := $(shell dirname $(VOS_REPO_PATH))
LIBC_HOME=$(VOS_HOME)/newlib
LIBC_INC_PATH ?= $(LIBC_HOME)/newlib/libc/include 
LIBC_BUILD_PATH ?= $(LIBC_HOME)/build

LIBVOS_PATH=${USER_PATH}/libc-os
LIBVORBIS_PATH=${USER_PATH}/libvorbis
LIBMINISDL_PATH=${USER_PATH}/libminisdl

# for all built static libs 
LIB_BUILD_PATH=${USER_PATH}/build-lib

CRT0=${LIB_BUILD_PATH}/crt0.o
CRT0CPP=${LIB_BUILD_PATH}/crt0cpp.o
CRTI=${LIB_BUILD_PATH}/crti.o
CRTN=${LIB_BUILD_PATH}/crtn.o

# whether output verbose build commands? V=1 will do
ifeq ($(V),1)
        VB=
else
        VB=@
endif