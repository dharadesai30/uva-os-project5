pushd .
cd kernel/addon/usb/lib
make clean
popd

pushd .
cd usr/
make clean
popd

pushd .
cd kernel/
make clean
popd
