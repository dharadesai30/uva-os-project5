###############
# make all dependnecy of doom & itself
###############

source "config.mk"

set -e

pushd .

cd ${NEWLIB_BUILD_PATH} && make -j10 
cd ${MINIDSL_PATH} && make -j10
cd ${LIBNEW_PATH} && make -j10

popd 
# make doom 
make -j10
# install doom binary 
cp -f doomgeneric ${USR_PATH}/build/doom

# force re-generate sd.bin
cd ${USR_PATH} && rm -f sd.bin && make -j10

cd ${KERNEL_PATH} && make -j10
