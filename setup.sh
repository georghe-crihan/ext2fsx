#! /bin/sh
#
# This shell script is released as part of the ext2fsx project under the GNU
# GPL.  For more information on the GPL please visit
# http://www.fsf.org/licenses/gpl.html for more information.
#
# For more information about the ext2fsx project please visit:
# http://sourceforge.net/projects/ext2fsx/
#
# Script written by Brian Bergstrand
# @(#) $Id$
echo "###################################################################"
echo "# WARNING:                                                         "
echo "#------------------------------------------------------------------"
echo "# This script will attempt to install necessary header files into  "
echo "# your established Kernel frameworks directory.  This will require "
echo "# Administrator or ROOT privlidges.                                "
echo "# This is your chance to stop before any changes occur.            "
echo "###################################################################"
echo "Do you wish to continue (y/n)? [n] "
read OKAY
OKAY=${OKAY:-n}

if test $OKAY = "n" -o $OKAY = "N"; then
echo "Exiting setup script... "
exit 1
fi


PATH=/usr/bin:/bin

echo -n "Establishing kernel version... "
SYSVER=`uname -r | awk -F. '{print $1}'`
echo ${SYSVER}

if [ ! -d ./src/gnu ]; then
	echo "Invalid source directory. Exiting."
	exit 1
fi

#
#args = src,dest
#
copydir() {
	for i in ${1}
	do
		TARGET=${2}/`basename $i`
		#echo ${TARGET}
		if [ ! -d "$i" ]; then
			if [ ! -e "${TARGET}" ]; then
				echo -n "Copying ${TARGET}..."
				sudo cp "$i" "${2}/"
                echo "done"
			fi
		else
			if [ ! -d "${TARGET}" ]; then
				echo -n "Copying ${TARGET}..."
				sudo cp -R $i "${2}/"
                echo "done"
			else 
				copydir "`ls -d $i/* | grep -v CVS`" "${TARGET}"
			fi
		fi
		
	done
}


# copy missing kernel headers
SYS_KERNF=/System/Library/Frameworks/Kernel.framework
echo -n "Determining missing kernel headers path... "

KERNH=0
case ${SYSVER} in
	6 )
		KERNH=./src/depend/jaguar/kern/Headers
	;;
	7 )
		KERNH=./src/depend/panther/kern/Headers
	;;
	#8 )
	#	KERNH=./src/depend/tiger/kern/Headers
	#;;
	* )
		echo "Warning: Support for kernel version ${SYSVER} is not available. Bailing."
		exit 1
	;;
esac
echo $KERNH

if [ ! -d ${SYS_KERNF} ]; then
echo "Warning: Kernel framework is not installed. Bailing."
exit 1
fi

echo "Beginning header file copy process..."
copydir "`ls -d ${KERNH}/* | grep -v CVS`" ${SYS_KERNF}/Headers 
echo "Header file copy process complete"
#
#make sure the perms are correct
#
echo -n "Setting new header file permissions... "
find ${SYS_KERNF}/Headers/ -type d | xargs sudo chmod go+rx
find ${SYS_KERNF}/Headers/ -type f | xargs sudo chmod go+r
echo "done"

#build and install disklib
echo ""
echo "###################################################################"
echo "#                                                                  "
echo "# Starting to build and install the disklib library                "
echo "#                                                                  "
echo "###################################################################"
echo ""
cd ./src/depend/disklib
make
sudo make install
echo "Disklib install complete"
cd ../../../

#perms

echo -n "Setting permissions... "
chmod u+x ./MakeInstall.sh ./Resources/post* ./Resources/pre* ./e2fsprogsbuild.sh \
./test/mkksym.sh
echo "done"

#links

cd ./src

if [ ! -e linux ]; then
    echo -n "Creating linux softlink... "
	ln -s ./gnu/ext2fs ./linux
    echo "done"
fi

cd ./gnu/ext2fs

if [ ! -e ext3_fs.h ]; then
    echo -n "Creating ext3_fs.h softlink... "
    ln ext2_fs.h ext3_fs.h
    echo "done"
fi

echo "End of setup script"
exit 0

# journal source

ln ./journal/jbd.h jbd.h
