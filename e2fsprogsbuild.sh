#!/bin/sh
# $Id$

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
# Panther has libiconv, but Jag doesn't
# --with-libiconv-prefix=/usr 
#
# --with-included-gettext   -- this would be nice, but the po/ build fails
	./configure --prefix=/usr/local --mandir=/usr/local/share/man --disable-nls \
--without-libintl-prefix --without-libiconv-prefix --disable-fsck --enable-bsd-shlibs \
--with-ccopts="-DAPPLE_DARWIN=1 -DHAVE_EXT2_IOCTLS=1 -DSYS_fsctl=242 -pipe"
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
