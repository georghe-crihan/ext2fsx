#!/bin/sh

PATH="/usr/bin:/bin:/usr/sbin:/sbin"

#src root
if [ -z "${SRCROOT}" ]; then
	echo "SRCROOT is not defined, using current directory"
	SRCROOT="."
fi

if [ ! -d "${SRCROOT}/src/e2fsprogs" ]; then
	exit 1
fi

cd "${SRCROOT}/src/e2fsprogs"

#The patches are now in CVS
#if [ ! -f ./.e2patchdone ]; then
#	patch -p1 < ../../e2diff.txt
#	if [ $? -ne 0 ]; then
#		echo "patch failed!"
#		exit $?
#	fi
#	touch ./.e2patchdone
#fi

if [ ! -f ./.e2configdone ]; then
	./configure --prefix=/usr/local --mandir=/usr/local/share/man \
--disable-fsck --enable-bsd-shlibs --with-ccopts="-DAPPLE_DARWIN=1"
	if [ $? -ne 0 ]; then
		echo "configure failed!"
		exit $?
	fi
	touch ./.e2configdone
fi

#set no prebind
#since the exec's are linked against the libs using a relative path,
#pre-binding will never work anyway. Setting tells the loader to not
#even try pre-binding.
export LD_FORCE_NO_PREBIND=1

#make clean
make

exit 0
