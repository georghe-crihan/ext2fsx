#!/bin/sh

PATH="/usr/bin:/bin:/usr/sbin:/sbin"

sudo rm -rf /System/Library/Extensions/ext2fs.kext
sudo rm -rf /System/Library/FileSystems/ext2.fs
sudo rm /sbin/mount_ext2
sudo rm /usr/share/man/man8/mount_ext2.8

#Rebuild the kext cache
if [ -f ./System/Library/Extensions.kextcache ]; then
	sudo rm ./System/Library/Extensions.kextcache
	sudo kextcache -k ./System/Library/Extensions
fi

rm -rf /Library/Receipts/Ext2FS.pkg
