#ifdef PLAT_VIRT
#include "plat-virt.h"
#elif defined PLAT_RPI3QEMU 
#include "plat-rpi3qemu.h"
#define PLAT_ISQEMU 1
#elif defined PLAT_RPI3
#include "plat-rpi3qemu.h"
#define PLAT_ISQEMU 0
#else
#error "unimpl"
#endif



