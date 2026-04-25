# build userspace, then kernel

pushd .
cd kernel/addon/usb/lib
make -j10
popd

########
# user libs 
pushd . 
cd usr/libc-os 
make
popd

pushd . 
cd usr/libvorbis
make
popd

pushd . 
cd usr/libminisdl
make
popd

########
# user apps
pushd .
cd usr
make -j10
# make clean && make -j20
popd

# doom 
pushd .
cd usr/doomgeneric/doomgeneric
make -j10
# make clean && make -j20
popd

pushd .
cd kernel
make -j10
# make clean && make -j20
popd