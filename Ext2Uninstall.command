#!/bin/sh

PATH="/usr/bin:/bin:/usr/sbin:/sbin"

sudo rm -rf /System/Library/Extensions/ext2fs.kext
sudo rm -rf /System/Library/FileSystems/ext2.fs
sudo rm /sbin/mount_ext2
sudo rm /usr/share/man/man8/mount_ext2.8

#e2fsprogs
sudo rm /sbin/fsck_ext2 /sbin/newfs_ext2
cd /usr/local/bin
sudo rm ./chattr ./lsattr ./uuidgen
cd ../info
sudo rm ./libext2fs.info.gz
cd ../lib
sudo rm ./libcom_err* ./libe2p* ./libext2fs* ./libss* ./libuuid*
cd ../sbin
sudo rm ./badblocks ./debugfs ./dumpe2fs ./e2fsck ./e2image ./e2label ./findfs ./fsck.ext2
sudo rm ./fsck.ext3 ./mke2fs ./mkfs.ext* ./mklost+found ./resize2fs ./tune2fs
cd ../share
sudo rm E2FSPROGS_COPYRIGHT
cd man/man1
sudo rm ./chattr.1 ./lsattr.1 ./uuidgen.1
cd ../man8
sudo rm ./badblocks.8 ./debugfs.8 ./dumpe2fs.8 ./e2fsck.8 ./e2image.8 ./e2label.8 ./findfs.8 ./fsck_ext2.8 ./newfs_ext2.8
sudo rm ./fsck.ext2.8 ./fsck.ext3.8 ./mke2fs.8 ./mkfs.ext* ./mklost+found.8 ./resize2fs.8 ./tune2fs.8

#Rebuild the kext cache
if [ -f ./System/Library/Extensions.kextcache ]; then
	sudo rm ./System/Library/Extensions.kextcache
	sudo kextcache -k ./System/Library/Extensions
fi

sudo rm -rf /Library/Receipts/Ext2FS.pkg
