#ifndef _PLAT_VIRT_H
#define _PLAT_VIRT_H

// QEMU virt, for qemu's "-M virt"
// will be included by both .S and .c

// ---------------- memory layout --------------------------- //
// cf qemu monitor "info mtree"
// [0x0, 0x4000:0000) 1GB of iomem, 
//              in which [0x0,0x1000:0000) for various on-chip devices
//                       [0x1000:0000, ) for pcie mmio
// [0x4000:0000, ) DRAM, size is specified by qemu cmd line. 
#define DEVICE_BASE     0x00000000          // iomem base
#define DEVICE_SIZE     0x40000000

#define PHYS_BASE       0x40000000              // start of sys mem
#define KERNEL_START    0x40080000              // qemu will load ker to this addr and boot
#define PHYS_SIZE       (1 *128*1024*1024U)    // size of phys mem, "qemu ... -m 128M ..."

// ---------------- gic irq controller --------------------------- //
// from qemu mon "info mtree"
#define QEMU_GIC_DIST_BASE          0x08000000
#define QEMU_GIC_CPU_BASE           0x08010000

// ---------------- uart, pl011 --------------------------- //
#define UART_PHYS   0x09000000      // from qemu "info mtree"
#define IRQ_UART_PL011  (32 + 1)        // "info mtree" shows irq=1

// cf: "Aarch64 programmer's guide: generic timer" 3.4 Interrupts
// "This means that each core sees its EL1 physical timer as INTID 30..."
#define IRQ_ARM_GENERIC_TIMER       30

// ---------------- virtio disk --------------------------- //
// about aarch64 virtio devices, cf: 
// https://github.com/RT-Thread/rt-thread/blob/master/components/drivers/virtio/virtio.h#L66
// device id: invalid 0, net 1, block 2, console 3, gpu 16, input 18
// irq num == (32+16+id) 
// cf: https://github.com/RT-Thread/rt-thread/blob/00c6800e4e532b6996bc9cb3ce0e8e4cffed2245/bsp/qemu-virt64-aarch64/drivers/drv_virtio.c#L83
// 
// however I can only got things to work  with "qemu -global virtio-mmio.force-legacy=false" which 
// enforces mmio ver=2 (as xv6 risc), in which virt-blk irq=48 (by trying out)
#define VIRTIO0_PHYS    0x0a000000
#define IRQ_VIRTIO0     (32+16)      

#endif