#!/bin/sh
cc -F/System/Library/PrivateFrameworks \
-framework Foundation -framework IOKit -framework DiskArbitration \
-g -I. -Icoremgr -I.. -I../gnu/ext2fs/linux/include -o extmgr \
test_main.m coremgr/ExtFSMediaController.m coremgr/ExtFSMedia.m
