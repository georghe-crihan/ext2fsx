#!/bin/sh
cc -F/System/Library/PrivateFrameworks \
-framework Foundation -framework IOKit -framework DiskArbitration \
-g -I./ -Icore/ -I../ -o extmgr test_main.m core/ExtFSMediaController.m core/ExtFSMedia.m
