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

E2VER=`sed -n -e "s/^.*E2FSPROGS_VERSION[^0-9]*\([0-9]*\.[0-9]*\).*$/\1/p" ./version.h`

if [ -f ./.e2configdone ]; then
	#make sure the version is the same
	CONFVER=`cat ./.e2configdone`
	if [ "$E2VER" != "$CONFVER" ]; then
		rm ./.e2configdone
		make clean
	fi
fi

if [ ! -f ./.e2configdone ]; then
	./configure --prefix=/usr/local --mandir=/usr/local/share/man --disable-nls \
--without-libintl-prefix --disable-fsck --enable-bsd-shlibs \
--with-ccopts="-DAPPLE_DARWIN=1 -pipe -traditional-cpp"
	if [ $? -ne 0 ]; then
		echo "configure failed!"
		exit $?
	fi
	echo "$E2VER" > ./.e2configdone
fi

# Certain files in intl are 444, and this can cause cp to fail
chmod -R u+w ./intl

#set no prebind
#since the exec's are linked against the shared libs using a relative path,
#pre-binding will never work anyway. Setting tells the loader to not
#even try pre-binding.
export LD_FORCE_NO_PREBIND=1

make

exit 0
