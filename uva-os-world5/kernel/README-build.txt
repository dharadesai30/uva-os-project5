README-build

###########
# build for rpi3 qemu
to clean up: 
./cleanall.sh

(optional) to configure the kernel 
change kernel/param.h 

to build everything 
export PLAT=rpi3qemu
./makeall.sh

to run 
./run-rpi3qemu.sh full 
(kenrel boots... type:)
doom


###########
# build for rpi3 (hw)
./cleanall.sh
export PLAT=rpi
./makeall.sh

# will copy the kernel image to d:/trampframe
# cp kernel8-rpi3.img /mnt/d/tmp/kernel8-rpi3.img

# insert sd card, copy to sd card 
copy "d:\tmp\kernel8-rpi3.img" f:\kernel8-rpi3.img

copy 9999-download.bat delay-exit.bat to windows desktop
double click 9999-download.bat

