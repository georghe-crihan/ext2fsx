#!/bin/sh

PATH="/usr/bin:/bin:/usr/sbin:/sbin"

#src root
if [ -z "${EXT2BUILD}" ]; then
	echo "EXT2BUILD is not defined, using current directory"
	EXT2BUILD=`pwd`
fi

BUILD="${EXT2BUILD}/build"
INSTALL="${BUILD}/install"

if [ ! -d "${BUILD}" ]; then
	echo "Invalid source directory."
	exit 1
fi

if [ ! -d "${INSTALL}" ]; then
	mkdir "${INSTALL}"
else
	sudo rm -rf "${INSTALL}"
	mkdir "${INSTALL}"
fi

mkdir -p "${INSTALL}/System/Library/Extensions"
mkdir -p "${INSTALL}/System/Library/FileSystems"
mkdir "${INSTALL}/sbin"
mkdir -p "${INSTALL}/usr/share/man/man8"
mkdir -p "${INSTALL}/usr/local/lib"

#install e2fsprogs
cd "${EXT2BUILD}/src/e2fsprogs"
DESTDIR="${INSTALL}" make install

cd "${EXT2BUILD}"

#mv fsck to /sbin and then remake the links
mv "${INSTALL}/usr/local/sbin/e2fsck" "${INSTALL}/sbin/fsck_ext2"

cd "${INSTALL}/usr/local/sbin"
rm fsck.ext2 fsck.ext3
ln -sf ../../../sbin/fsck_ext2 ./e2fsck
ln -sf ../../../sbin/fsck_ext2 ./fsck.ext2
ln -sf ../../../sbin/fsck_ext2 ./fsck.ext3

cd ../share/man/man8
ln -f e2fsck.8 ./fsck_ext2.8
ln -f mke2fs.8 ./newfs_ext2.8

#lib sym links
cd "${INSTALL}/usr/local/lib"

ln -sf ./libblkid.2.0.dylib ./libblkid.dylib
ln -sf ./libcom_err.1.0.dylib ./libcom_err.dylib
ln -sf ./libe2p.2.1.dylib ./libe2p.dylib
ln -sf ./libext2fs.2.1.dylib ./libext2fs.dylib
ln -sf ./libss.1.0.dylib ./libss.dylib
ln -sf ./libuuid.1.1.dylib ./libuuid.dylib

cd "${EXT2BUILD}"

cp -pR "${BUILD}/ext2fs.kext" "${INSTALL}/System/Library/Extensions"
cp -pR "${BUILD}/ext2.fs" "${INSTALL}/System/Library/FileSystems"

#mount
cp -p "${BUILD}/mount_ext2" "${INSTALL}/sbin"
cp -p "${EXT2BUILD}/src/mount_ext2fs/mount_ext2fs.8" "${INSTALL}/usr/share/man/man8/mount_ext2.8"

#newfs
cp -p "${BUILD}/newfs_ext2" "${INSTALL}/sbin"

#strip binaries for prod build here

#get rid of unwanted files
find "${INSTALL}" -name "\.DS_Store" -exec rm {} \;
find "${INSTALL}" -name "pbdevelopment.plist" -exec rm {} \;
find "${INSTALL}" -name "CVS" -type d -exec rm -fr {} \;

#e2fsprogs copyright
cp -p "${EXT2BUILD}/src/e2fsprogs/COPYING" "${INSTALL}/usr/local/share/E2FSPROGS_COPYRIGHT"

sudo chown -R root:wheel "${INSTALL}"
sudo chmod -R go-w "${INSTALL}"
sudo chmod -R u-w build/install/sbin/* 
sudo chmod -R u-w build/install/usr/local/bin/*
sudo chmod -R u-w build/install/usr/local/sbin/* 
sudo chmod -R u-w build/install/usr/local/lib/*