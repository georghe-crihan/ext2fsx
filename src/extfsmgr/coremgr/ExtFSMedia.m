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

static const char whatid[] __attribute__ ((unused)) =
"@(#) $Id$";

#import <unistd.h>
#import <sys/param.h>
#import <sys/ucred.h>
#import <sys/mount.h>
#import <sys/attr.h>
#import <sys/ioctl.h>
#import <sys/syscall.h>

#import <ext2_byteorder.h>
#import <gnu/ext2fs/ext2_fs.h>

#import "ExtFSMedia.h"
#import "ExtFSMediaController.h"

#import <IOKit/storage/IOMedia.h>
#import <IOKit/IOBSD.h>

NSString *ExtFSMediaNotificationUpdatedInfo = @"ExtFSMediaNotificationUpdatedInfo";
NSString *ExtFSMediaNotificationChildChange = @"ExtFSMediaNotificationChildChange";

/* Extra VOL CAPS in Panther */
#ifndef VOL_CAP_FMT_JOURNAL
#define VOL_CAP_FMT_JOURNAL 0x00000008
#endif
#ifndef VOL_CAP_FMT_JOURNAL_ACTIVE
#define VOL_CAP_FMT_JOURNAL_ACTIVE 0x00000010
#endif
#ifndef VOL_CAP_FMT_NO_ROOT_TIMES
#define VOL_CAP_FMT_NO_ROOT_TIMES 0x00000020
#endif
#ifndef VOL_CAP_FMT_SPARSE_FILES
#define VOL_CAP_FMT_SPARSE_FILES 0x00000040
#endif
#ifndef VOL_CAP_FMT_ZERO_RUNS
#define VOL_CAP_FMT_ZERO_RUNS 0x00000080
#endif
#ifndef VOL_CAP_FMT_CASE_SENSITIVE
#define VOL_CAP_FMT_CASE_SENSITIVE 0x00000100
#endif
#ifndef VOL_CAP_FMT_CASE_PRESERVING
#define VOL_CAP_FMT_CASE_PRESERVING 0x00000200
#endif
#ifndef VOL_CAP_FMT_FAST_STATFS
#define VOL_CAP_FMT_FAST_STATFS 0x00000400
#endif

struct attr_volinfo {
   size_t v_size;
   /* Fixed storage */
   u_long v_signature;
   off_t v_availspace;
   u_long v_filecount;
   u_long v_dircount;
   attrreference_t v_volref;
   vol_capabilities_attr_t v_caps;
   /* Variable storage */
   char v_name[256];
};

union volinfo {
   struct attr_volinfo vinfo;
   struct statfs vstat;
};
#define VOL_INFO_CACHE_TIME 60

#ifdef EXT_MGR_GUI
static NSMutableDictionary *_mediaIconCache = nil;
#endif

#ifndef SYS_fsctl
#define SYS_fsctl 242
#endif

struct superblock {
   struct ext2_super_block *s_es;
};
#define e2super _sb
#define e2sblock e2super->s_es

#define e2super_alloc \
do { \
   _sb = malloc(sizeof(struct superblock)); \
   if (_sb) { \
      e2sblock = malloc(sizeof(struct ext2_super_block)); \
      if (!e2sblock) { free(_sb); _sb = nil; } \
   } \
} while(0)

#define e2super_free \
do { \
   if (_sb) { free(e2sblock); free(_sb); _sb = nil; } \
} while(0)

@implementation ExtFSMedia

/* Private */
#define kHFSPlusSigWord 0x482B

- (int)fsInfo
{
   struct attrlist alist;
   union volinfo vinfo;
   struct timeval now;
   int err;
   char *path;
   
   path = MOUNTPOINTSTR(self);
   if (!path)
      return (EINVAL);
   
   /* Get the superblock if we don't have it */
   if ((fsTypeExt2 == _fsType || fsTypeExt3 == _fsType) && !_sb) {
      e2super_alloc;
      if (_sb)
         err = syscall(SYS_fsctl, path, EXT2_IOC_GETSBLOCK, e2sblock, 0);
      else
         err = ENOMEM;
      if (err)
         NSLog(@"extfs: Failed to load superblock for device %@ mounted on %s (%d).\n",
            [self bsdName], path, err);
   }
   
   gettimeofday(&now, nil);
   if (_lastFSUpdate + VOL_INFO_CACHE_TIME > now.tv_sec)
      return (0);
      
   _lastFSUpdate = now.tv_sec;
   
   bzero(&vinfo, sizeof(vinfo));
   bzero(&alist, sizeof(alist));
   alist.bitmapcount = ATTR_BIT_MAP_COUNT;
   alist.volattr = ATTR_VOL_INFO|ATTR_VOL_SIGNATURE|ATTR_VOL_SPACEAVAIL|
      ATTR_VOL_FILECOUNT|ATTR_VOL_DIRCOUNT;
   if (!_volName)
      alist.volattr |= ATTR_VOL_CAPABILITIES|ATTR_VOL_NAME;
   
   err = getattrlist(path, &alist, &vinfo.vinfo, sizeof(vinfo.vinfo), 0);
   if (!err) {
      _fileCount = vinfo.vinfo.v_filecount;
      _dirCount = vinfo.vinfo.v_dircount;
      _blockAvail = vinfo.vinfo.v_availspace / _fsBlockSize;
      if (0 != vinfo.vinfo.v_name[VOL_CAPABILITIES_FORMAT])
         _volName = [NSSTR(vinfo.vinfo.v_name) retain];
      if (alist.volattr & ATTR_VOL_CAPABILITIES)
         _volCaps = vinfo.vinfo.v_caps.capabilities[0];
      if (fsTypeHFS == _fsType && kHFSPlusSigWord == vinfo.vinfo.v_signature)
            _fsType = fsTypeHFSPlus;
      goto exit;
   }
   
   /* Fall back to statfs to get the available blocks */
   err = statfs(path, &vinfo.vstat);
   if (!err)
      _blockAvail = vinfo.vstat.f_bavail;
   
   _attributeFlags &= ~kfsGetAttrlist;

exit:
   if (!err)
      [[NSNotificationCenter defaultCenter]
         postNotificationName:ExtFSMediaNotificationUpdatedInfo object:self
         userInfo:nil];
   return (err);
}

/* Public */

- (ExtFSMedia*)initWithIORegProperties:(NSDictionary*)properties
{
   NSString *hint;
   NSRange r;
   
   if ((self = [super init])) {
      _media = [properties retain];
      _size = [[_media objectForKey:NSSTR(kIOMediaSizeKey)] unsignedLongLongValue];
      _devBlockSize = [[_media objectForKey:NSSTR(kIOMediaPreferredBlockSizeKey)] unsignedLongValue];
      _fsType = fsTypeUnknown;
      
      _attributeFlags = kfsDiskArb | kfsGetAttrlist;
      if ([[_media objectForKey:NSSTR(kIOMediaEjectableKey)] boolValue])
         _attributeFlags |= kfsEjectable;
      if ([[_media objectForKey:NSSTR(kIOMediaWritableKey)] boolValue])
         _attributeFlags |= kfsWritable;
      if ([[_media objectForKey:NSSTR(kIOMediaWholeKey)] boolValue])
         _attributeFlags |= kfsWholeDisk;
      if ([[_media objectForKey:NSSTR(kIOMediaLeafKey)] boolValue])
         _attributeFlags |= kfsLeafDisk;
      
      hint = [_media objectForKey:NSSTR(kIOMediaContentHintKey)];
      r = [hint rangeOfString:@"partition"];
      if (hint && NSNotFound != r.location)
         _attributeFlags |= kfsNoMount;
         
      r = [hint rangeOfString:@"Driver"];
      if (hint && NSNotFound != r.location)
         _attributeFlags |= kfsNoMount;
         
      r = [hint rangeOfString:@"Patches"];
      if (hint && NSNotFound != r.location)
         _attributeFlags |= kfsNoMount;
      
      r = [hint rangeOfString:@"CD_DA"]; /* Digital Audio tracks */
      if (hint && NSNotFound != r.location)
         _attributeFlags |= kfsNoMount;
         
      hint = [_media objectForKey:NSSTR(kIOMediaContentKey)];
      r = [hint rangeOfString:@"partition"];
      if (hint && NSNotFound != r.location)
         _attributeFlags |= kfsNoMount;
      
   #ifdef EXT_MGR_GUI
      if (_mediaIconCache)
         [_mediaIconCache retain];
      else
         _mediaIconCache = [[NSMutableDictionary alloc] init];
   #endif
   }
   return (self);
}

- (id)representedObject
{
   return (_object);
}

- (void)setRepresentedObject:(id)object
{
   _object = object;
}

- (ExtFSMedia*)parent
{
   return (_parent);
}

- (NSArray*)children
{
   return (_children);
}

- (unsigned)childCount
{
   return ([_children count]);
}

- (void)addChild:(ExtFSMedia*)media
{
   if (!_children)
      _children = [[NSMutableArray alloc] init];

   if ([_children containsObject:media]) {
#ifdef DIAGNOSTIC
      NSLog(@"ExtFS: Oops! Parent %@ already contains child %@.\n",
         [self bsdName], [media bsdName]);
#endif
      return;
   };

   [_children addObject:media];
   [[NSNotificationCenter defaultCenter]
      postNotificationName:ExtFSMediaNotificationChildChange
      object:self userInfo:nil];
}

- (void)remChild:(ExtFSMedia*)media
{
   if (NO == [_children containsObject:media]) {
#ifdef DIAGNOSTIC
      NSLog(@"ExtFS: Oops! Parent %@ does not contain child %@.\n",
         [self bsdName], [media bsdName]);
#endif
      return;
   };

   [_children removeObject:media];
   [[NSNotificationCenter defaultCenter]
      postNotificationName:ExtFSMediaNotificationChildChange
      object:self userInfo:nil];
}

- (NSString*)ioRegistryName
{
   return (_ioregName);
}

- (NSString*)bsdName
{
   return ([_media objectForKey:NSSTR(kIOBSDNameKey)]);
}

#ifdef EXT_MGR_GUI
- (NSImage*)icon
{
   NSString *bundleid, *iconName;
   FSRef ref;
   int err;
   
   if (_icon)
      return (_icon);
   
   if (_parent && (_icon = [_parent icon]))
      return ([_icon retain]);
   
   bundleid = [_iconDesc objectForKey:(NSString*)kCFBundleIdentifierKey];
   iconName = [_iconDesc objectForKey:NSSTR(kIOBundleResourceFileKey)];
   if (!bundleid || !iconName)
      return (nil);
   
   /* Try the cache */
   if ((_icon = [_mediaIconCache objectForKey:bundleid]))
      return ([_icon retain]);
   
   /* Load the icon from disk */
   if (_attributeFlags & kfsIconNotFound)
      return (nil);
   
   if (noErr == (err = FSFindFolder (kSystemDomain,
      kKernelExtensionsFolderType, NO, &ref))) {
      CFURLRef url;
      
      url = CFURLCreateFromFSRef (nil, &ref);
      if (url) {
         CFArrayRef kexts;
         
         kexts = CFBundleCreateBundlesFromDirectory(nil, url, nil);
         if (kexts) {
            CFBundleRef iconBundle;
            
            iconBundle = CFBundleGetBundleWithIdentifier((CFStringRef)bundleid);
            if (iconBundle) {
               CFURLRef iconurl;
               
               iconurl = CFBundleCopyResourceURL(iconBundle,
                  (CFStringRef)[iconName stringByDeletingPathExtension],
                  (CFStringRef)[iconName pathExtension],
                  nil);
               if (iconurl) {
                  _icon = [[NSImage alloc] initWithContentsOfURL:(NSURL*)iconurl];
                  CFRelease(iconurl);
               }
            }
            CFRelease(kexts);
         } /* kexts */
         CFRelease(url);
      } /* url */
   } /* FSFindFolder */
   
   if (_icon)
      [_mediaIconCache setObject:_icon forKey:bundleid];
   else
      _attributeFlags |= kfsIconNotFound;
   
   return (_icon);
}
#endif

- (BOOL)isEjectable
{
   return (0 != (_attributeFlags & kfsEjectable));
}

- (BOOL)canMount
{
   if ((_attributeFlags & kfsNoMount))
      return (NO);
   
   return (YES);
}

- (BOOL)isMounted
{
   return (0 != (_attributeFlags & kfsMounted));
}

- (BOOL)isWritable
{
   return (0 != (_attributeFlags & kfsWritable));
}

- (BOOL)isWholeDisk
{
   return (0 != (_attributeFlags & kfsWholeDisk));
}

- (BOOL)isLeafDisk
{
   return (0 != (_attributeFlags & kfsLeafDisk));
}

- (BOOL)isDVDROM
{
   return (0 != (_attributeFlags & kfsDVDROM));
}

- (BOOL)isCDROM
{
   return (0 != (_attributeFlags & kfsCDROM));
}

- (BOOL)usesDiskArb
{
   return (0 != (_attributeFlags & kfsDiskArb));
}

- (void)setUsesDiskArb:(BOOL)diskarb
{
   if (diskarb)
      _attributeFlags |= kfsDiskArb;
   else
      _attributeFlags &= ~kfsDiskArb;
}

- (u_int64_t)size
{
   return (_size);
}

- (u_int32_t)blockSize
{
   return ((_attributeFlags & kfsMounted) ? _fsBlockSize : _devBlockSize);
}

- (ExtFSType)fsType
{
   return (_fsType);
}

- (NSString*)mountPoint
{
   return (_where);
}

- (u_int64_t)availableSize
{
   (void)[self fsInfo];
   return (_blockAvail * _fsBlockSize);
}

- (u_int64_t)blockCount
{
   return (_blockCount);
}

- (u_int64_t)fileCount
{
   if (_attributeFlags & kfsGetAttrlist)
      (void)[self fsInfo];
   
   return (_fileCount);
}

- (u_int64_t)dirCount
{
   if (_attributeFlags & kfsGetAttrlist)
      (void)[self fsInfo];
      
   return (_dirCount);
}

- (NSString*)volName
{
   return (_volName);
}

- (BOOL)hasJournal
{
   return (0 != (_volCaps & VOL_CAP_FMT_JOURNAL));
}

- (BOOL)isJournaled
{
   return (0 != (_volCaps & VOL_CAP_FMT_JOURNAL_ACTIVE));
}

- (BOOL)isCaseSensitive
{
   return (0 != (_volCaps & VOL_CAP_FMT_CASE_SENSITIVE));
}

- (BOOL)isCasePreserving
{
   return (0 != (_volCaps & VOL_CAP_FMT_CASE_PRESERVING));
}

- (BOOL)hasSparseFiles
{
   return (0 != (_volCaps & VOL_CAP_FMT_SPARSE_FILES));
}

- (BOOL)hasSuper
{
   return (nil != _sb);
}

- (CFUUIDRef)uuid /* Private (for now) */
{
   CFUUIDRef uuid = nil;
   CFUUIDBytes *bytes;
   
   if (_sb) {
      bytes = (CFUUIDBytes*)&e2sblock->s_uuid[0];
      uuid = CFUUIDCreateFromUUIDBytes(kCFAllocatorDefault, *bytes);
   }
   
   return (uuid);
}

- (BOOL)isExtFS
{
   return (_fsType == fsTypeExt2 || _fsType == fsTypeExt3);
}

- (NSString*)uuidString
{
   CFUUIDRef uuid;
   NSString *str;
   
   uuid = [self uuid];
   if (uuid) {
      str = (NSString*)CFUUIDCreateString(kCFAllocatorDefault, uuid);
      CFRelease(uuid);
      return ([str autorelease]);
   }
   
   return (nil);
}

- (BOOL)hasIndexedDirs
{
   if (_sb)
      return (0 != EXT3_HAS_COMPAT_FEATURE(e2super, EXT3_FEATURE_COMPAT_DIR_INDEX));
      
   return (NO);
}

- (BOOL)hasLargeFiles
{
   if (_sb)
      return (0 != EXT2_HAS_RO_COMPAT_FEATURE(e2super, EXT2_FEATURE_RO_COMPAT_LARGE_FILE));
   
   return (NO);
}

- (NSComparisonResult)compare:(ExtFSMedia *)media
{
   return ([[self bsdName] compare:[media bsdName]]);
}

/* Super */

- (NSString*)description
{
   return ([self bsdName]);
}

- (unsigned)hash
{
   return ([[self bsdName] hash]);
}

- (BOOL)isEqual:(id)obj
{
   if ([obj respondsToSelector:@selector(bsdName)])
      return ([[self bsdName] isEqualTo:[obj bsdName]]);
   
   return (NO);
}

- (void)dealloc
{
#ifdef DIAGNOSTIC
   NSLog(@"ExtFS: Object '%@' dealloc\n",
      [_media objectForKey:NSSTR(kIOBSDNameKey)]);
#endif
   e2super_free;
#ifdef EXT_MGR_GUI
   unsigned count;
   [_icon release];
   count = [_mediaIconCache retainCount];
   [_mediaIconCache release];
   if (1 == count)
      _mediaIconCache = nil;
#endif
   [_iconDesc release];
   [_ioregName release];
   [_volName release];
   [_where release];
   [_media release];
   [_children release];
   [_parent release];
   [super dealloc];
}

@end
