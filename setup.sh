#! /bin/sh
# @(#) $Id$

PATH=/usr/bin:/bin

if [ ! -d ./src/gnu ]; then
	echo "Invalid source directory."
	exit 1
fi

#perms

chmod u+x ./MakeInstall.sh ./Resources/post* ./Resources/pre* ./e2fsprogsbuild.sh

#links

cd ./src

if [ ! -e linux ]; then
	ln -s ./gnu/ext2fs ./linux
fi

cd ./gnu/ext2fs

ln ext2_fs.h ext3_fs.h

exit 0

# journal source

ln ./journal/jbd.h jbd.h
