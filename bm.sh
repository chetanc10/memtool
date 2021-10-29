#!/bin/bash

# Update this to cross-compiler-gcc path for non-x86 build-host platforms
#CC=/tmp/work/imx8mqevk-poky-linux/linux-imx/4.14.98-r0/recipe-sysroot-native/usr/bin/aarch64-poky-linux/aarch64-poky-linux-gcc
#SYSROOT=tmp/work/imx8mqevk-poky-linux/imx-boot/0.2-r0/recipe-sysroot
#CFLAGS="--sysroot ${SYSROOT}"
CC=gcc

[ ! "$1" ] && echo "Usage: bm.sh <local|sys> [clean]" && exit 0
[ "$1" != "local" ] && [ "$1" != "sys" ] && echo "Usage: bm.sh <local|sys> [clean]" && exit 1
[ "$2" ] && [ "$2" != "clean" ] && echo "Usage: bm.sh <local|sys> [clean]" && exit 2

Target="$1"
CleanRequest="$2"
if [ "$Target" == "local" ]; then
	# To install current directory only to be invoked as normal user binaries
	TargetDir=./bin/
else
	# To install in System Path so that memtool binaries are available as linux commands
	TargetDir=/usr/bin/
fi

[ "$CleanRequest" ] && rm -rf ${TargetDir}/md* ${TargetDir}/mw* && rm -rf memtool && exit

# Remove memtool binary and it's symbolic links before build
rm -rf ./bin
mkdir ./bin

# Build memtool now
${CC} -Wall ${CFLAGS} memtool.c -o memtool
[ $? -ne 0 ] && exit 1

# Setup bin folder with binary symbolic links afresh
mv memtool ${TargetDir}/md
cd ${TargetDir}
cp md mw
ln -s md md.b
ln -s md md.w
ln -s md md.l
ln -s md md.q
ln -s mw mw.b
ln -s mw mw.w
ln -s mw mw.l
ln -s mw mw.q
