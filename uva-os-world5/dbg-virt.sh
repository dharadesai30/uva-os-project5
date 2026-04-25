# cf: https://github.com/RT-Thread/rt-thread/blob/master/bsp/qemu-virt64-aarch64/qemu.sh

p1-gen-hash-ports() {
    export MYGDBPORT=`echo -n ${USER} | md5sum | cut -c1-8 | printf "%d\n" 0x$(cat -) | awk '{printf "%.0f\n", 50000 + (($1 / 0xffffffff) * 10000)}'`
    echo "set gdb port: ${MYGDBPORT}"
}

p1-gen-hash-ports

echo "Listen at port: ${MYGDBPORT}"
echo "**To terminate QEMU, press Ctrl-a then x"
echo 
echo "  Next: in a separate window, launch gdb: "
echo "      gdb-multiarch build/kernel8.elf "
echo 
echo "  Example gdb commands -- "
echo "      (gdb) file build/kernel8.elf"
echo "      (gdb) target remote :${MYGDBPORT}"
echo "      (gdb) layout asm"
echo "  You may want to have a custom ~/.gdbinit "
echo "	Details: https://fxlin.github.io/p1-kernel/gdb/"
echo " ------------------------------------------------"

/cs4414-shared/qemu/aarch64-softmmu/qemu-system-aarch64 -M virt,gic-version=2,virtualization=on \
-cpu cortex-a53 -m 128M -smp 4 -kernel ./kernel8.img -serial mon:stdio -nographic \
-global virtio-mmio.force-legacy=false \
-drive if=none,file=sd.bin,format=raw,id=blk0 -device virtio-blk-device,drive=blk0,bus=virtio-mmio-bus.0 \
    -gdb tcp::${MYGDBPORT} -S
