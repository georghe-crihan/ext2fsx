/*
* Copyright 2004 Brian Bergstrand.
*
* Redistribution and use in source and binary forms, with or without modification, 
* are permitted provided that the following conditions are met:
*
* 1.	Redistributions of source code must retain the above copyright notice, this list of
*     conditions and the following disclaimer.
* 2.	Redistributions in binary form must reproduce the above copyright notice, this list of
*     conditions and the following disclaimer in the documentation and/or other materials provided
*     with the distribution.
* 3.	The name of the author may not be used to endorse or promote products derived from this
*     software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
* AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
* OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
* THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
*/

/*
 * This is a private implementation file. It should never be accessed by ExtFSDiskManager clients.
 */

#import "ExtFSMedia.h"
#import "ExtFSMediaController.h"

@protocol ExtFSMCP
- (void)updateMountStatus;
- (ExtFSMedia*)createMediaWithIOService:(io_service_t)service properties:(NSDictionary*)props;
- (int)updateMedia:(io_iterator_t)iter remove:(BOOL)remove;
- (BOOL)volumeDidUnmount:(NSString*)name;
- (void)removePending:(ExtFSMedia*)media;
@end

@interface ExtFSMediaController (Private)
- (void)postNotification:(NSArray*)args;
- (NSString*)pathForResource:(NSString*)resource;
@end

#define EFSMCPostNotification(note, obj, info) do {\
NSArray *args = [[NSArray alloc] initWithObjects:note, obj, info, nil]; \
[[ExtFSMediaController mediaController] performSelectorOnMainThread:@selector(postNotification:) \
withObject:args waitUntilDone:NO]; \
[args release]; \
} while(0)

#define EXTFS_DM_BNDL_ID @"net.sourceforge.ext2fsx.ExtFSDiskManager"
#define EFS_PROBE_RSRC @"efsprobe"

@interface ExtFSMedia (ExtFSMediaControllerPrivate)
- (void)updateAttributesFromIOService:(io_service_t)service;
- (void)setIsMounted:(struct statfs*)stat;
- (NSDictionary*)iconDescription;
- (void)addChild:(ExtFSMedia*)media;
- (void)remChild:(ExtFSMedia*)media;
- (io_service_t)copyIOService;
- (io_service_t)copyATAIOService; // Get ATABlockStorageDevice service
/* Implemented in ExtFSMedia.m -- this just gets rid of the compiler warnings. */
- (int)fsInfo;
@end

#ifndef EXT2FS_NAME
#define EXT2FS_NAME "ext2"
#endif
#ifndef EXT3FS_NAME
#define EXT3FS_NAME "ext3"
#endif

#define HFS_NAME "hfs"
#define UFS_NAME "ufs"
#define CD9660_NAME "cd9660"
#define CDAUDIO_NAME "cddafs"
#define UDF_NAME "udf"
#define MSDOS_NAME "msdos"
#define NTFS_NAME "ntfs"

#define efsIOTransportTypeMask 0x00000007
#define efsIOTransportBusMask  0xFFFFFFF8