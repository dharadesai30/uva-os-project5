# cf: https://github.com/RT-Thread/rt-thread/blob/master/bsp/qemu-virt64-aarch64/qemu.sh
# tracing: https://qemu-project.gitlab.io/qemu/devel/tracing.html


/cs4414-shared/qemu/aarch64-softmmu/qemu-system-aarch64 -M virt,gic-version=2,virtualization=on \
-cpu cortex-a53 -m 128M -smp 4 -kernel ./kernel8.img -serial mon:stdio -nographic \
-global virtio-mmio.force-legacy=false \
-drive if=none,file=sd.bin,format=raw,id=blk0 -device virtio-blk-device,drive=blk0,bus=virtio-mmio-bus.0 \
-d int,trace:virtio_blk*,trace:virtio_mmio_setting_irq -D qemu.log 

# also for rpi3 qemu: 
#  https://github.com/RT-Thread/rt-thread/blob/master/bsp/raspberry-pi/raspi3-64/qemu-64.sh
# no much info

# trace events can be found by 
# info trace-events
