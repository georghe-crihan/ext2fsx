#!/bin/sh
# @(#) $Id$
#
# Creates Installer package for Ext2 distribution
#
# Created for the ext2fsx project: http://sourceforge.net/projects/ext2fsx/
#
# Copyright 2003-2004 Brian Bergstrand.
#
# Redistribution and use in source and binary forms, with or without modification, 
# are permitted provided that the following conditions are met:
#
# 1.    Redistributions of source code must retain the above copyright notice, this list of
#     conditions and the following disclaimer.
# 2.    Redistributions in binary form must reproduce the above copyright notice, this list of
#     conditions and the following disclaimer in the documentation and/or other materials provided
#     with the distribution.
# 3.    The name of the author may not be used to endorse or promote products derived from this
#     software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
# WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
# AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
# OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
# THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

PATH="/usr/bin:/bin:/usr/sbin:/sbin"

#src root
if [ -z "${EXT2BUILD}" ]; then
	echo "EXT2BUILD is not defined, using current directory"
	EXT2BUILD=`pwd`
fi

BUILD="${EXT2BUILD}/build"

if [ ! -d "${BUILD}" ]; then
	echo "Invalid source directory."
	exit 1
fi

SYMS="${BUILD}/.symbols.tar"

usage() {
	echo "Usage: $0 [options] 'version string'"
	echo "	-i Only build the disk image (using the current package repository)"
	echo "	-h Show this message"
	echo "	-p Only build the package repository"
	echo "Note: -i and -p are mutually exclusive. If both are specified, then the last one takes precedence."
	exit 1
}

VER=

#################### BUILD PACKAGE REPOSITORY ###################

build_package() {

INSTALL="${BUILD}/install"

if [ ! -d "${INSTALL}" ]; then
	mkdir "${INSTALL}"
else
	sudo rm -rf "${INSTALL}"
	mkdir "${INSTALL}"
fi

BUILDLOG=`basename ${0}`
BUILDLOG=/tmp/Ext2${BUILDLOG}.log
touch ${BUILDLOG}

mkdir -p "${INSTALL}/System/Library/Extensions"
mkdir -p "${INSTALL}/System/Library/Filesystems"
mkdir -p "${INSTALL}/Library/PreferencePanes"
mkdir -p "${INSTALL}/Library/Frameworks"
mkdir "${INSTALL}/sbin"
mkdir -p "${INSTALL}/usr/share/man/man8"
mkdir -p "${INSTALL}/usr/local/lib"
mkdir -p "${INSTALL}/usr/local/share/doc"
mkdir -p "${INSTALL}/usr/local/man"

#install e2fsprogs
cd "${EXT2BUILD}/src/e2fsprogs"
echo "Installing e2fsprogs..."
DESTDIR="${INSTALL}" make install >> ${BUILDLOG} 2>&1

cd "${EXT2BUILD}"

echo "Installing other userland utils..."
cd "${INSTALL}/usr/local/share/man/man8"
ln -f e2fsck.8 ./fsck_ext2.8
ln -f mke2fs.8 ./newfs_ext2.8

# link them into man also, since MANPATH defaults to /usr/local/man
cd "${INSTALL}/usr/local/share/man"
PAGES=`find . -type f`
for i in ${PAGES}
do
if [ ! -d ../../man/`dirname $i` ]; then
mkdir ../../man/`dirname $i`
fi
ln -f $i ../../man/$i
done


#lib sym links
cd "${INSTALL}/usr/local/lib"

ln -sf ./libblkid.2.0.dylib ./libblkid.dylib
# note this can conflict with the system lib /usr/lib/libcom_err.dylib
ln -sf ./libcom_err.1.1.dylib ./libcom_err.dylib
ln -sf ./libcom_err.1.1.dylib ./libcom_err_e2.dylib
ln -sf ./libe2p.2.1.dylib ./libe2p.dylib
ln -sf ./libext2fs.2.1.dylib ./libext2fs.dylib
ln -sf ./libss.1.0.dylib ./libss.dylib
ln -sf ./libuuid.1.1.dylib ./libuuid.dylib

cd "${EXT2BUILD}"

cp -pR "${BUILD}/ExtFSManager.prefPane" "${INSTALL}/Library/PreferencePanes/"

cp -pR "${BUILD}/ext2fs.kext" "${INSTALL}/System/Library/Extensions"
cp -pR "${BUILD}/ext2.fs" "${INSTALL}/System/Library/Filesystems"

#mount
cp -p "${BUILD}/mount_ext2" "${INSTALL}/sbin"
cp -p "${EXT2BUILD}/src/mount_ext2fs/mount_ext2fs.8" "${INSTALL}/usr/share/man/man8/mount_ext2.8"

#newfs
cp -p "${BUILD}/newfs_ext2" "${INSTALL}/sbin"

#fsck
cp -p "${BUILD}/fsck_ext2" "${INSTALL}/sbin"

#e2undel
cp -p "${BUILD}/e2undel" "${INSTALL}/usr/local/sbin"
cp -p "${EXT2BUILD}/src/e2undel/README" "${INSTALL}/usr/local/share/doc/E2UNDEL_README"

#frameworks
echo "Installing frameworks..."
cp -pR "${BUILD}/ExtFSDiskManager.framework" "${INSTALL}/Library/Frameworks/"

echo "Installing kernel driver(s)..."
PANK="${BUILD}/ext2fs_panther.kext"
JAGK="${BUILD}/ext2fs_jag.kext"
KMOD="Contents/MacOS/ext2fs"
# XXX -- hack to determine if we are building a debug version
DBG=`nm -m "${BUILD}/ext2fs.kext/${KMOD}" | grep logwakeup`
# build the jag version of the kext if needed
if [ ! -d "${JAGK}" ] || [ ! `find "${JAGK}/${KMOD}" -newer "${BUILD}/ext2fs.kext/${KMOD}"` ]; then
mv "${BUILD}/ext2fs.kext" "${PANK}"
BUILDER=pbxbuild
if [ -d "${EXT2BUILD}/ext2fsX.xcode" ]; then
	BUILDER=xcodebuild
	cd "${EXT2BUILD}/ext2fsX.xcode"
else
	cd "${EXT2BUILD}/ext2fsX.pbproj"
fi
BUILDSTYLE="JagDeployment"
if [ "${DBG}" != "" ]; then
	BUILDSTYLE="JagDevelopment"
fi
echo "Building Jaguar Kext..."
${BUILDER} -target ext2_kext -buildstyle ${BUILDSTYLE} clean >> ${BUILDLOG} 2>&1
${BUILDER} -target ext2_kext -buildstyle ${BUILDSTYLE} build >> ${BUILDLOG} 2>&1
if [ ! -d "${BUILD}/ext2fs.kext" ]; then
	mv "${PANK}" "${BUILD}/ext2fs.kext"
	echo "Jag Kext build failed! Stopping. See ${BUILDLOG}."
	exit 1
fi
# Set the correct kernel dependency version
sed -f "${EXT2BUILD}/inst/infover.sed" "${PANK}/Contents/Info.plist" \
> "${BUILD}/ext2fs.kext/Contents/Info.plist"
#save jag kext
if [ -d "${JAGK}" ]; then
	rm -rf "${JAGK}"
fi
mv "${BUILD}/ext2fs.kext" "${JAGK}"
#build clean, so rebuilding from XCode/PB won't pick up stale object files.
${BUILDER} -target ext2_kext -buildstyle ${BUILDSTYLE} clean > /dev/null 2>&1
mv "${PANK}" "${BUILD}/ext2fs.kext"
# Get back to the root
cd "${EXT2BUILD}"
else
echo "Using existing Jaguar Kext"
fi # -newer test
#copy to install
cp -pR "${JAGK}" "${INSTALL}/System/Library/Extensions/ext2fs_jag.kext"

echo "Removing unwanted files..."
#get rid of unwanted files
find "${INSTALL}" -name ".DS_Store" | xargs rm
find "${INSTALL}" -name "pbdevelopment.plist" | xargs rm
find "${INSTALL}" -name "CVS" -type d | xargs rm -fr

#e2fsprogs copyright
cp -p "${EXT2BUILD}/src/e2fsprogs/COPYING" "${INSTALL}/usr/local/share/doc/E2FSPROGS_COPYRIGHT"

# strip for prod build
if [ "${DBG}" == "" ]; then
	# make archive of full symbols
	cd "${INSTALL}"
	if [ -f "${SYMS}" ]; then
		rm "${SYMS}"
	fi
	tar -cf "${SYMS}" ./System/Library/Extensions
	tar -rf "${SYMS}" ./Library/Frameworks/ExtFSDiskManager.framework
	
	echo "Stripping driver symbols..."
	cd "./System/Library/Extensions"
	for i in `ls -Fd *.kext`
	do
		strip -S "${i}${KMOD}"
	done
	
	echo "Stripping frameworks..."
	strip -x "${INSTALL}/Library/Frameworks/ExtFSDiskManager.framework/Versions/Current/ExtFSDiskManager"
	
	cd "${EXT2BUILD}"
fi

# set perms
echo "Setting permissions..."
chmod -R go-w "${INSTALL}"
chmod 775 "${INSTALL}/Library" "${INSTALL}/Library/PreferencePanes" "${INSTALL}/Library/Frameworks"
chmod -R u-w "${INSTALL}"/sbin/* "${INSTALL}"/usr/local/bin/* "${INSTALL}"/usr/local/sbin/* \
"${INSTALL}"/usr/local/lib/*
sudo chown -R root:wheel "${INSTALL}"
sudo chgrp admin "${INSTALL}/Library" "${INSTALL}/Library/PreferencePanes" \
"${INSTALL}/Library/Frameworks"

echo "Build done."

if [ -f ${BUILDLOG} ]; then
	rm ${BUILDLOG}
fi

}

########################## DISK IMAGE CREATION ###########################

build_image() {

# Create the disk image
echo "Creating disk image..."
TMP="/tmp/Ext2FS_${VER}.dmg"
IMAGE="${HOME}/Desktop/Ext2FS_${VER}.dmg"
VNAME="Ext2 Filesystem ${VER}"
VOL="/Volumes/${VNAME}"

hdiutil create -megabytes 5 "${TMP}"
DEVICE=`hdid -nomount "${TMP}" | grep Apple_HFS | cut -f1`
/sbin/newfs_hfs -v "${VNAME}" ${DEVICE}
hdiutil eject ${DEVICE}
DEVICE=`hdid "${TMP}" | sed -n 1p | cut -f1`

if [ ! -d "${VOL}" ]; then
	echo "Volume failed to mount!"
	exit 1
fi

if [ -z "${EXT2BUILD}" ]; then
	EXT2DIR=`pwd`
else
	EXT2DIR="${EXT2BUILD}"
fi

echo "Building Package..."
# Jag tools location
PKGMKR=/Developer/Applications/PackageMaker.app/Contents/MacOS/PackageMaker
if [ ! -f ${PKGMKR} ]; then
# Panther tools location
PKGMKR=/Developer/Applications/Utilities/PackageMaker.app/Contents/MacOS/PackageMaker
fi
"${PKGMKR}" -build -p "${VOL}/Ext2FS.pkg" -f "${EXT2DIR}/build/install" \
-r "${EXT2DIR}/Resources" -i "${EXT2DIR}/pkginfo/Info.plist" \
-d "${EXT2DIR}/pkginfo/Description.plist"

echo "Copying files..."

cp -p "${EXT2DIR}/Resources/English.lproj/ReadMe.rtf" "${VOL}"
cp -p "${EXT2DIR}/Changes.rtf" "${VOL}"
cp -p "${EXT2DIR}/Ext2Uninstall.command" "${VOL}"
chmod 555 "${VOL}/Ext2Uninstall.command"

if [ -f "${SYMS}" ]; then
	cp "${SYMS}" "${VOL}/"
	gzip -9 "${VOL}/"`basename "${SYMS}"`
	rm "${SYMS}"
fi

echo "Finishing..."

hdiutil eject ${DEVICE}

#convert to compressed image
hdiutil convert "${TMP}" -format UDZO -o "${IMAGE}" -ov
rm "${TMP}"

md5 "${IMAGE}"

}

########################## MAIN ##############################

if [ $# -eq 0 ]; then 
	usage
	exit 1
fi

#0 = build both, 1 = build image only, 2 = build package only
WHAT=0

#parse options
while : ; do
	case $# in 0) break ;; esac
	
	case "$1" in
		-i )
			WHAT=1
			shift
		;;
		-h )
			usage
		;;
		-p )
			WHAT=2
			shift
		;;
		-? | --* )
			echo "Invalid option"
			usage
		;;
		* )
			#last arg is version string
			VER="$1"
			shift
			break
		;;
	esac
done

#main
if [ "${VER}" = "" ]; then
	usage
fi

echo "Version number is: $VER"

case "$WHAT" in
	0 )
		build_package
		build_image
		break
	;;
	1 )
		echo "Warning. The Jaguar kext will not be built. Continue? [y]"
		read CONT
		case "${CONT}" in
			"n" | "N" )
				exit
				break
			;;
			* )
				break
			;;
		esac
		build_image
		break
	;;
	2 )
		build_package
		break
	;;
	* )
		break
	;;
esac

exit 0
