/*
* Copyright 2003-2004 Brian Bergstrand.
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

#import <Cocoa/Cocoa.h>

/*!
@enum ExtFSType
@abstract Filesystem type id's.
@constant fsTypeExt2 Ext2 filesystem id.
@constant fsTypeExt3 Ext3 journaled filesystem id.
@constant fsTypeHFS Macintosh HFS filesystem id.
@constant fsTypeHFSPlus Macintosh HFS Extended filesystem id.
@constant fsTypeHFSJ Macintosh HFS Extended Journaled filesystem id.
@constant fsTypeHFSJCS Macintosh HFS Extended Journaled and Case Sensistive filesystem id.
@constant fsTypeUFS UFS (Unix)filesystem id.
@constant fsTypeCD9660 ISO 9660 filesystem id.
@constant fsTypeCDAudio CD Audio filesystem id.
@constant fsTypeUDF UDF (DVD) filesystem id.
@constant fsTypeMSDOS FAT filesystem id. Includes FAT12, FAT16 and FAT32
@constant fsTypeNTFS NTFS filesystem id.
@constant fsTypeUnknown Unknown filesystem id.
@constant fsTypeNULL Placeholder for an empty marker.
*/
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

// Forward declaration for an ExtFSMedia private type.
struct superblock;

/*!
@class ExtFSMedia
@abstract Representation of filesystem and/or device.
@discussion Instances of this class can be used to query
a filesystem or device for its properties.
*/
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
   NSImage *_icon;
}

/*!
@method initWithIORegProperties:
@abstract Preferred initializer method.
@param properties A dictionary of the device properties returned
from the IO Registry.
@result A new ExtFSMedia object or nil if there was an error.
The object will be returned auto-released.
*/
- (ExtFSMedia*)initWithIORegProperties:(NSDictionary*)properties;

/*!
@method representedObject
@abstract Access associated context object.
@result Context object or nil if a context object
has not been set.
*/
- (id)representedObject;
/*!
@method setRepresentedObject
@abstract Associate some object with the target ExtFSMedia object
@discussion This is merely a context association for the object client.
It is neither retained, nor released by the ExtFSMedia object.
*/
- (void)setRepresentedObject:(id)object;

/*!
@method parent
@abstract Access the parent of the target object.
@discussion The parent object is determined from the IO Registry
hierarchy.
@result ExtFSMedia object that is the direct parent of
the target object -- nil is returned if the target has
no parent.
*/
- (ExtFSMedia*)parent;
/*!
@method children
@abstract Access the children of the target object.
@discussion Children are determined from the IO Registry hierarchy.
@result An array of ExtFSMedia objects that are directly descended
from the target object -- nil is returned if the target has
no descendants.
*/
- (NSArray*)children;
/*!
@method childCount
@abstract Convenience method to obtain the child count of the target.
@result Integer containing the number of children.
*/
- (unsigned)childCount;

/* Device */
/*!
@method ioRegistryName
@abstract Access the name of the object as the IO Registry
identifies it.
@result String containing the IOKit name.
*/
- (NSString*)ioRegistryName;
/*!
@method bsdName
@abstract Access the device name of the object as the
BSD kernel identifies it.
@result String containing the kernel device name.
*/
- (NSString*)bsdName;

/*!
@method icon
@abstract Access the icon of the object as determined by the IO Registry.
@result Preferred device image for the target object.
*/
- (NSImage*)icon;

/*!
@method isEjectable
@abstract Determine if the media is ejectable from its enclosure.
@result YES if the media can be ejected, otherwise NO.
*/
- (BOOL)isEjectable;
/*!
@method canMount
@abstract Determine if the media is mountable.
@result YES if the media can be mounted, otherwise NO.
*/
- (BOOL)canMount;
/*!
@method isMounted
@abstract Determine if the media is currently mounted.
@result YES if the filesystem on the device is currently mounted, otherwise NO.
*/
- (BOOL)isMounted;
/*!
@method isWritable
@abstract Determine if the media/filesystem is writable.
@discussion If the media is mounted, then this applies to
the filesystem only. Otherwise, it applies only to the media.
@result YES if the filesystem or media is writable, otherwise NO.
*/
- (BOOL)isWritable;
/*!
@method isWholeDisk
@abstract Determine if the target object represents the media
as a whole (ie the total disk, not a partition of the disk).
@result YES or NO.
*/
- (BOOL)isWholeDisk;
/*!
@method isLeafDisk
@abstract Determine if the media contains any partitions.
@result YES if the media does not contain partitions, otherwise NO.
*/
- (BOOL)isLeafDisk;
/*!
@method isDVDROM
@abstract Determine if the media is a DVD.
@result YES if the media is a DVD, otherwise NO.
*/
- (BOOL)isDVDROM;
/*!
@method isCDROM
@abstract Determine if the media is a CD.
@result YES if the media is a CD, otherwise NO.
*/
- (BOOL)isCDROM;
/*!
@method usesDiskArb
@abstract Determine if the media is managed by the Disk Arbitration
daemon.
@result YES if the media is DA managed, otherwise NO.
*/
- (BOOL)usesDiskArb;
/*!
@method size
@abstract Determine the size of the filesystem or media.
@discussion If the media is mounted, then this applies to
the filesystem only. Otherwise, it applies only to the media.
@result Size of the filesystem or media in bytes.
*/
- (u_int64_t)size; /* bytes */
/*!
@method blockSize
@abstract Determine the block size of the filesystem or media.
@discussion If the media is mounted, then this applies to
the filesystem only. Otherwise, it applies only to the media.
@result Size of the filesystem or media block size in bytes.
*/
- (u_int32_t)blockSize;

/*!
@method fsType
@abstract Determine the filesystem type.
@discussion If the media is not mounted, the result will always
be fsTypeUnknown.
@result The filesystem type id.
*/
- (ExtFSType)fsType;
/*!
@method mountPoint
@abstract Determine the directory that the filesystem is mounted on.
@result String containing mount path, or nil if the media is not mounted.
*/
- (NSString*)mountPoint;
/*!
@method availableSize
@abstract Determine the filesystem's available space.
@result Size of available space in bytes. Always 0 if the filesystem is
not mounted.
*/
- (u_int64_t)availableSize;
/*!
@method blockCount
@abstract Determine the block count of the filesystem or media.
@discussion If the media is mounted, then this applies to
the filesystem only. Otherwise, it applies only to the media.
@result Number of blocks in the filesystem or media.
*/
- (u_int64_t)blockCount;
/*!
@method fileCount
@abstract Determine the number of files in the filesystem.
@result The number of files, or 0 if the media is not mounted.
*/
- (u_int64_t)fileCount;
/*!
@method dirCount
@abstract Determine the number of directories in the filesystem.
@discussion The filesystem must support the getattrlist() sys call or
0 will be returned.
@result The number of directories, or 0 if the media is not mounted.
*/
- (u_int64_t)dirCount;
/*!
@method volName
@abstract Get the filesystem name.
@result String containing the filesystem name or nil if it
cannot be determined (ie the media is not mounted).
*/
- (NSString*)volName;
/*!
@method hasPermissions
@abstract Determine if filesystem permissions are in effect.
@result YES if permissions are active, otherwise NO.
Always NO if the media is not mounted.
*/
- (BOOL)hasPermissions;
/*!
@method hasJournal
@abstract Determine if the filesystem has a journal log.
@result YES if a journal is present, otherwise NO.
Always NO if the media is not mounted.
*/
- (BOOL)hasJournal;
/*!
@method isJournaled
@abstract Determine if the filesystem journal is active.
@discussion A filesystem may have a journal on disk, but it
may not be currently in use.
@result YES if the journal is active, otherwise NO.
Always NO if the media is not mounted.
*/
- (BOOL)isJournaled;
/*!
@method isCaseSensitive
@abstract Determine if the filesystem uses case-sensitive file names.
@result YES or NO. Always NO if the media is not mounted.
*/
- (BOOL)isCaseSensitive;
/*!
@method isCasePreserving
@abstract Determine if the filesystem preserves file name case.
@discussion HFS is case-preserving, but not case-sensitive, 
Ext2/UFS is both and FAT is neither.
@result YES or NO. Always NO if the media is not mounted.
*/
- (BOOL)isCasePreserving;
/*!
@method hasSparseFiles
@abstract Determine if the filesystem supports sparse files.
@discussion Sparse files are files with "holes" in them, the filesystem
will automatically return zero's when these "holes" are accessed. HFS does
not support sparse files, while Ext2 and UFS do.
@result YES if sparse files are supported, otherwise NO.
Always NO if the media is not mounted.
*/
- (BOOL)hasSparseFiles;

/*!
@method isExtFS
@abstract Convenience method to determine if a filesystem is Ext2 or Ext3.
@result YES if the filesystem is Ext2/3, otherwise NO.
Always NO if the media is not mounted.
*/
- (BOOL)isExtFS;
/*!
@method uuidString
@abstract Get the filesystem UUID as a string.
@discussion This is only supported by Ext2/3 currrently.
@result String containing UUID or nil if a UUID is not present.
Always nil if the media is not mounted.
*/
- (NSString*)uuidString;
/*!
@method hasIndexedDirs
@abstract Determine if directory indexing is active.
@discussion Directory indexing is an Ext2/3 specific option.
It greatly speeds up file name lookup for large directories.
@result YES if indexing is active, otherwise NO.
Always NO if the media is not mounted or the filesystem is not Ext2/3.
*/
- (BOOL)hasIndexedDirs;
/*!
@method hasLargeFiles
@abstract Determine if the filesystem supports large files (> 2GB).
@discussion This only works with Ext2/3 filesystems currently.
@result YES if large files are supported, otherwise NO.
Always NO if the media is not mounted or the filesystem is not Ext2/3.
*/
- (BOOL)hasLargeFiles;

/*!
@method compare:
@abstract Determine the equality of one object compared to another.
@param media The object to compare the target object to.
@result NSOrderedSame if the objects are equal.
*/
- (NSComparisonResult)compare:(ExtFSMedia *)media;

@end

/*!
@const ExtFSMediaNotificationUpdatedInfo
@abstract This notification is sent to the default Notification center
when the filesystem information has been updated (available space, file count, etc).
The changed media object is attached.
*/
extern NSString * const ExtFSMediaNotificationUpdatedInfo;
/*!
@const ExtFSMediaNotificationChildChange
@abstract This notification is sent to the default Notification center
when a child is added or removed. The parent object is attached.
*/
extern NSString * const ExtFSMediaNotificationChildChange;

/*!
@defined BSDNAMESTR
@abstract Convenience macro to get an object's device name as a C string.
*/
#define BSDNAMESTR(media) (char *)[[(media) bsdName] UTF8String]
/*!
@defined MOUNTPOINTSTR
@abstract Convenience macro to get an object's mount point name as a C string.
*/
#define MOUNTPOINTSTR(media) (char *)[[(media) mountPoint] UTF8String]

/*!
@enum ExtFSMediaFlags
@abstract ExtFSMedia internal bit flags.
@discussion These flags should not be used by ExtFSMedia clients.
@constant kfsDiskArb Media is managed by Disk Arb.
@constant kfsMounted Media is mounted.
@constant kfsWritable Media or filesystem is writable.
@constant kfsEjectable Media is ejectable.
@constant kfsWholeDisk Media represents a whole disk.
@constant kfsLeafDisk Media contains no partititions.
@constant kfsCDROM Media is a CD.
@constant kfsDVDROM Media is a DVD.
@constant kfsGetAttrlist Filesystem supports getattrlist() sys call.
@constant kfsIconNotFound No icon found for the media.
@constant kfsNoMount Media cannot be mounted (partition map, driver partition, etc).
@constant kfsPermsEnabled Filesystem permissions are in effect.
*/
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
   kfsNoMount		= (1<<11),
   kfsPermsEnabled  = (1<<12)
};

/*!
@defined NSSTR
@abstract Convenience macro to convert a C string to an NSString.
*/
#define NSSTR(cstr) [NSString stringWithCString:(cstr)]
