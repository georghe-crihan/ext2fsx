/*
* Copyright 2003 Brian Bergstrand.
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

#ifdef EXT_MGR_GUI
#import <Cocoa/Cocoa.h>
#else
#import <Foundation/Foundation.h>
#endif

typedef enum {
   fsTypeExt2,
   fsTypeExt3,
   fsTypeHFS,
   fsTypeHFSPlus,
   fsTypeHFSJ, /* Journal */
   fsTypeHFSJCS, /* Journal, Case Sensisitive */
   fsTypeUFS,
   fsTypeCD9660,
   fsTypeCDAudio,
   fsTypeUDF,
   fsTypeMSDOS,
   fsTypeNTFS,
   fsTypeUnknown,
   fsTypeNULL
}ExtFSType;

struct superblock;

@interface ExtFSMedia : NSObject
{
@private
   ExtFSMedia *_parent;
   id _children;
   
   id _media, _iconDesc, _object;
   NSString *_where, *_ioregName, *_volName;
   struct superblock *_sb;
   u_int64_t _size, _blockCount, _blockAvail;
   u_int32_t _devBlockSize, _fsBlockSize, _attributeFlags,
      _volCaps, _lastFSUpdate, _fileCount, _dirCount;
   ExtFSType _fsType;
#ifdef EXT_MGR_GUI
   NSImage *_icon;
#endif
}

- (ExtFSMedia*)initWithIORegProperties:(NSDictionary*)properties;

/* Note: This is merely an context association for the object user.
   It is neither retained, nor released by the object. */
- (id)representedObject;
- (void)setRepresentedObject:(id)object;

- (ExtFSMedia*)parent;
- (NSArray*)children;
- (unsigned)childCount;

/* Device */
- (NSString*)ioRegistryName;
- (NSString*)bsdName;
#ifdef EXT_MGR_GUI
- (NSImage*)icon;
#endif
- (BOOL)isEjectable;
- (BOOL)canMount;
- (BOOL)isMounted;
- (BOOL)isWritable;
- (BOOL)isWholeDisk;
- (BOOL)isLeafDisk;
- (BOOL)isDVDROM;
- (BOOL)isCDROM;
- (BOOL)usesDiskArb;
- (void)setUsesDiskArb:(BOOL)diskarb;
- (u_int64_t)size; /* bytes */
- (u_int32_t)blockSize;

/* All filesystems */
- (ExtFSType)fsType;
- (NSString*)mountPoint;
- (u_int64_t)availableSize; /* bytes */
- (u_int64_t)blockCount;
- (u_int64_t)fileCount;
- (u_int64_t)dirCount;
- (NSString*)volName;
- (BOOL)hasJournal;
- (BOOL)isJournaled;
- (BOOL)isCaseSensitive;
- (BOOL)isCasePreserving;
- (BOOL)hasSparseFiles;
- (BOOL)hasSuper;

/* Ext2/3 Only */
- (BOOL)isExtFS;
- (NSString*)uuidString;
- (BOOL)hasIndexedDirs;
- (BOOL)hasLargeFiles;

/* Util */
- (NSComparisonResult)compare:(ExtFSMedia *)media;

@end

extern NSString *ExtFSMediaNotificationUpdatedInfo;
extern NSString *ExtFSMediaNotificationChildChange;

#define BSDNAMESTR(media) (char *)[[(media) bsdName] UTF8String]
#define MOUNTPOINTSTR(media) (char *)[[(media) mountPoint] UTF8String]

/* Internal flags */

enum {
   kfsDiskArb		= (1<<0), /* Mount/unmount with Disk Arb */
   kfsMounted		= (1<<1),
   kfsWritable		= (1<<2),
   kfsEjectable	= (1<<3),
   kfsWholeDisk	= (1<<4),
   kfsLeafDisk		= (1<<5),
   kfsCDROM			= (1<<6),
   kfsDVDROM		= (1<<7),
   kfsGetAttrlist	= (1<<9),  /* getattrlist supported */
   kfsIconNotFound	= (1<<10),
   kfsNoMount		= (1<<11) /* Not mountable (partition map, driver partition, etc) */
};

#define NSSTR(cstr) [NSString stringWithCString:(cstr)]
