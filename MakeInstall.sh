#!/bin/sh

PATH="/usr/bin:/bin:/usr/sbin:/sbin"

if [ -z "${EXT2BUILD}" ]; then
	echo "EXT2BUILD is not defined, using current directory"
	EXT2BUILD="."
fi

BUILD="${EXT2BUILD}/build"
INSTALL="${BUILD}/install"

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

cp -R "${BUILD}/ext2fs.kext" "${INSTALL}/System/Library/Extensions"
cp -R "${BUILD}/ext2.fs" "${INSTALL}/System/Library/FileSystems"
cp "${BUILD}/mount_ext2" "${INSTALL}/sbin"
cp "${EXT2BUILD}/src/mount_ext2fs/mount_ext2fs.8" "${INSTALL}/usr/share/man/man8/mount_ext2.8"

#strip binaries for prod build here

#get rid of unwanted files
find "${INSTALL}" -name "\.DS_Store" -exec rm {} \;
find "${INSTALL}" -name "pbdevelopment.plist" -exec rm {} \;
find "${INSTALL}" -name "CVS" -type d -exec rm -fr {} \;

sudo chown -R root:wheel "${INSTALL}"
sudo chmod -R go-w "${INSTALL}"
