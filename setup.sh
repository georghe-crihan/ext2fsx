#! /bin/sh
# @(#) $Id$

PATH=/usr/bin:/bin

SYSVER=`uname -r | awk -F. '{print $1}'`

if [ ! -d ./src/gnu ]; then
	echo "Invalid source directory."
	exit 1
fi

#args = src,dest
copydir() {
	for i in ${1}
	do
		#echo ${2}/`basename $i`
		if [ ! -d $i ]; then
			if [ ! -e ${2}/`basename $i` ]; then
				echo sudo cp $i ${2}/
			fi
		else
			if [ ! -d ${2}/`basename $i` ]; then
				echo sudo cp -R $i ${2}/
			else 
				copydir "`ls -d $i/* | grep -v CVS`" ${2}/`basename $i`
			fi
		fi
		
	done
}

# copy missing kernel headers
SYS_KERNF=/System/Library/Frameworks/Kernel.framework
KERNH=./src/depend/panther/kern/Headers
if [ ${SYSVER} -eq 6 ]; then
KERNH=./src/depend/jaguar/kern/Headers
fi

if [ ! -d ${SYS_KERNF} ]; then
echo "Warning: Kernel framework is not installed. Bailing."
exit 1
fi

copydir "`ls -d ${KERNH}/* | grep -v CVS`" ${SYS_KERNF}/Headers 

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
