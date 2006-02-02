#!/bin/sh
# $Id$
#
# Builds e2fsprogs
#
# Created for the ext2fsx project: http://sourceforge.net/projects/ext2fsx/
#
# Copyright 2003-2004,2006 Brian Bergstrand.
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
	EXT2BUILD="."
fi

if [ ! -d "${EXT2BUILD}/src/e2fsprogs" ]; then
	exit 1
fi

cd "${EXT2BUILD}/src/e2fsprogs"

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
# --with-included-gettext   -- this would be nice, but the po/ build fails
	CC=/usr/bin/gcc-3.3 ./configure --prefix=/usr/local --mandir=/usr/local/share/man --disable-nls \
--without-libintl-prefix --with-libiconv-prefix=/usr --disable-fsck --enable-bsd-shlibs \
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
