#!/bin/sh

#must be run as root

TEMPFILE=/tmp/burnfs
MNT=/mnt/burn

if [ ! -d $MNT ]; then
	mkdir -p $MNT
fi

dd if=/dev/zero of=$TEMPFILE bs=1024k count=220

/sbin/mkfs -t ext2 -L "EXT2 TEST" -F -b 2048 $TEMPFILE
/sbin/tune2fs $TEMPFILE -c 0 -i 0
mount -t ext2 -o loop=/dev/loop0 $TEMPFILE $MNT

cp -dR /usr/src/linux-2.4.18-14/kernel $MNT/
cp -dR /usr/src/linux-2.4.18-14/fs $MNT/
umount $MNT

# system dependent settings
DEVICEID="CRX195E1"
# BASH trick: x is an array holding the words from the command results
x=(`cdrecord -scanbus | grep $DEVICEID`)
DEV=${x[0]}
if [ -z "$DEV" ]
then
    echo CD writer device not found
    exit 1
fi

# burn CD
x=(`ls -l $TEMPFILE`)
SIZE=${x[4]}
cdrecord -vv speed=12 dev=$DEV -tsize=$SIZE -data -multi -eject $TEMPFILE
