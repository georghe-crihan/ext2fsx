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

static const char whatid[] __attribute__ ((unused)) =
"@(#) $Id$";

#import <unistd.h>
#import <sys/param.h>
#import <sys/ucred.h>
#import <sys/mount.h>
#import <sys/attr.h>
#import <sys/ioctl.h>
#import <sys/syscall.h>
#import <pthread.h>

#import <ext2_byteorder.h>
#import <gnu/ext2fs/ext2_fs.h>

#import "ExtFSLock.h"
#import "ExtFSMedia.h"
#import "ExtFSMediaController.h"

#import <IOKit/storage/IOMedia.h>
#import <IOKit/IOBSD.h>

NSString * const ExtFSMediaNotificationUpdatedInfo = @"ExtFSMediaNotificationUpdatedInfo";
NSString * const ExtFSMediaNotificationChildChange = @"ExtFSMediaNotificationChildChange";

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
#define VOL_INFO_CACHE_TIME 15

static NSMutableDictionary *e_mediaIconCache = nil;
static void *e_mediaIconCacheLck = nil;

#ifndef SYS_fsctl
#define SYS_fsctl 242
#endif

struct superblock {
   struct ext2_super_block *s_es;
};
#define e2super _sb
#define e2sblock e2super->s_es

// Must be called with the write lock held
#define e2super_alloc() \
do { \
   _sb = malloc(sizeof(struct superblock)); \
   if (_sb) { \
      e2sblock = malloc(sizeof(struct ext2_super_block)); \
      if (!e2sblock) { free(_sb); _sb = nil; } \
   } \
} while(0)

// Must be called with the write lock held
#define e2super_free() \
do { \
   if (_sb) { free(e2sblock); free(_sb); _sb = nil; } \
} while(0)

#ifndef __HFS_FORMAT__
#define kHFSPlusSigWord 0x482B
#define kHFSXSigWord 0x4858
#endif

@implementation ExtFSMedia

/* Private */

- (void)postNotification:(NSArray*)args
{
    NSDictionary *info = ([args count] == 2) ? [args objectAtIndex:1] : nil;
    [[NSNotificationCenter defaultCenter]
         postNotificationName:[args objectAtIndex:0] object:self userInfo:info];
}
#define EFSMPostNotification(note, info) do {\
NSArray *args = [[NSArray alloc] initWithObjects:note, info, nil]; \
[self performSelectorOnMainThread:@selector(postNotification:) withObject:args waitUntilDone:NO]; \
[args release]; \
} while(0)

- (int)fsInfo
{
   struct attrlist alist;
   union volinfo vinfo;
   struct timeval now;
   int err;
   char *path;
   NSString *bsdName = [self bsdName];
   
   path = MOUNTPOINTSTR(self);
   if (!path)
      return (EINVAL);
   
   /* Get the superblock if we don't have it */
   ewlock(e_lock);
   if ((fsTypeExt2 == _fsType || fsTypeExt3 == _fsType) && !_sb) {
      e2super_alloc();
      if (_sb)
         err = syscall(SYS_fsctl, path, EXT2_IOC_GETSBLOCK, e2sblock, 0);
      else
         err = ENOMEM;
      if (err) {
         NSLog(@"ExtFS: Failed to load superblock for device '%@' mounted on '%s' (%d).\n",
            bsdName, path, err);
         e2super_free();
      }
   }
   
   gettimeofday(&now, nil);
   if (_lastFSUpdate + VOL_INFO_CACHE_TIME > now.tv_sec) {
      eulock(e_lock);
      return (0);
   }
   _lastFSUpdate = now.tv_sec;
   
   eulock(e_lock); // drop the lock while we do the I/O
   
   bzero(&vinfo, sizeof(vinfo));
   bzero(&alist, sizeof(alist));
   alist.bitmapcount = ATTR_BIT_MAP_COUNT;
   alist.volattr = ATTR_VOL_INFO|ATTR_VOL_SIGNATURE|ATTR_VOL_SPACEAVAIL|
      ATTR_VOL_FILECOUNT|ATTR_VOL_DIRCOUNT;
   if (!_volName)
      alist.volattr |= ATTR_VOL_CAPABILITIES|ATTR_VOL_NAME;
   
   err = getattrlist(path, &alist, &vinfo.vinfo, sizeof(vinfo.vinfo), 0);
   if (!err) {
      ewlock(e_lock);
      _fileCount = vinfo.vinfo.v_filecount;
      _dirCount = vinfo.vinfo.v_dircount;
      _blockAvail = vinfo.vinfo.v_availspace / _fsBlockSize;
      if (0 != vinfo.vinfo.v_name[VOL_CAPABILITIES_FORMAT])
         _volName = [NSSTR(vinfo.vinfo.v_name) retain];
      if (alist.volattr & ATTR_VOL_CAPABILITIES)
         _volCaps = vinfo.vinfo.v_caps.capabilities[0];
      if (fsTypeHFS == _fsType && kHFSPlusSigWord == vinfo.vinfo.v_signature)
            _fsType = fsTypeHFSPlus;
      else if (fsTypeHFS == _fsType && kHFSXSigWord == vinfo.vinfo.v_signature)
         _fsType = fsTypeHFSX;
      eulock(e_lock);
      goto eminfo_exit;
   }
   
   /* Fall back to statfs to get the info. */
   err = statfs(path, &vinfo.vstat);
   ewlock(e_lock);
   if (!err) {
      _blockAvail = vinfo.vstat.f_bavail;
      _fileCount = vinfo.vstat.f_files - vinfo.vstat.f_ffree;
   }
   
   _attributeFlags &= ~kfsGetAttrlist;
   eulock(e_lock);
   
eminfo_exit:
   if (!err) {
      EFSMPostNotification(ExtFSMediaNotificationUpdatedInfo, nil);
   } else {
      _lastFSUpdate = 0;
   }
   return (err);
}

/* Public */

- (ExtFSMedia*)initWithIORegProperties:(NSDictionary*)properties
{
   NSString *hint;
   NSRange r;
   
   if ((self = [super init])) {
      if (nil == e_mediaIconCacheLck) {
         // Just to make sure someone else doesn't get in during creation...
         e_mediaIconCacheLck = (void*)0xDEADBEEF;
         if (0 != eilock(&e_mediaIconCacheLck)) {// This is never released
             NSLog(@"ExtFS: Failed to allocate media icon cache lock!\n");
            e_mediaIconCacheLck = nil;
init_err:
            [super release];
            return (nil);
         }
      }
      if (0 != eilock(&e_lock)) {
        NSLog(@"ExtFS: Failed to allocate media object lock!\n");
        goto init_err;
      }
      
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
      
      ewlock(e_mediaIconCacheLck);
      if (nil != e_mediaIconCache)
         (void)[e_mediaIconCache retain];
      else
         e_mediaIconCache = [[NSMutableDictionary alloc] init];
      eulock(e_mediaIconCacheLck);
   }
   return (self);
}

- (id)representedObject
{
   return ([[_object retain] autorelease]);
}

- (void)setRepresentedObject:(id)object
{
   id tmp;
   (void)[object retain];
   tmp = _object;
   ewlock(e_lock);
   _object = object;
   eulock(e_lock);
   [tmp release];
}

- (ExtFSMedia*)parent
{
   return ([[_parent retain] autorelease]);
}

- (NSArray*)children
{
   NSArray *c;
   erlock(e_lock);
   /* We have to return a copy, so the caller
      can use the array in a thread safe context.
    */
   c = [_children copy];
   eulock(e_lock);
   return ([c autorelease]);
}

- (unsigned)childCount
{
   unsigned ct;
   erlock(e_lock);
   ct  = [_children count];
   eulock(e_lock);
   return (ct);
}

- (void)addChild:(ExtFSMedia*)media
{
#ifdef DIAGNOSTIC
   NSString *myname = [self bsdName], *oname = [media bsdName];
#endif   
   ewlock(e_lock);
   if (!_children)
      _children = [[NSMutableArray alloc] init];

   if ([_children containsObject:media]) {
#ifdef DIAGNOSTIC
      NSLog(@"ExtFS: Oops! Parent '%@' already contains child '%@'.\n",
         myname, oname);
#endif
      eulock(e_lock);
      return;
   };

   [_children addObject:media];
   eulock(e_lock);
   EFSMPostNotification(ExtFSMediaNotificationChildChange, nil);
}

- (void)remChild:(ExtFSMedia*)media
{
#ifdef DIAGNOSTIC
   NSString *myname = [self bsdName], *oname = [media bsdName];
#endif
   ewlock(e_lock);
   if (nil == _children || NO == [_children containsObject:media]) {
#ifdef DIAGNOSTIC
      NSLog(@"ExtFS: Oops! Parent '%@' does not contain child '%@'.\n",
         myname, oname);
#endif
      eulock(e_lock);
      return;
   };

   [_children removeObject:media];
   eulock(e_lock);
   EFSMPostNotification(ExtFSMediaNotificationChildChange, nil);
}

- (NSString*)ioRegistryName
{
   return ([[_ioregName retain] autorelease]);
}

- (NSString*)bsdName
{
   NSString *name;
   erlock(e_lock);
   name = [[_media objectForKey:NSSTR(kIOBSDNameKey)] retain];
   eulock(e_lock);
   return ([name autorelease]);
}

- (NSImage*)icon
{
   NSString *bundleid, *iconName, *cacheKey;
   NSImage *ico;
   FSRef ref;
   int err;
   
   erlock(e_lock);
   if (_icon) {
      eulock(e_lock);
      return ([[_icon retain] autorelease]);
   }
   euplock(e_lock); // Upgrade to write lock
   
   if (_parent && (_icon = [_parent icon])) {
      (void)[_icon retain]; // For ourself
      eulock(e_lock);
#ifdef DIAGNOSTIC
      NSLog(@"ExtFS: Retrieved icon %@ from %@ (%u).\n", [_icon name], _parent, [_icon retainCount]);
#endif
      return ([[_icon retain] autorelease]);
   }
   
   if (_attributeFlags & kfsIconNotFound) {
      eulock(e_lock);
      return (nil);
   }
   
   bundleid = [_iconDesc objectForKey:(NSString*)kCFBundleIdentifierKey];
   iconName = [_iconDesc objectForKey:NSSTR(kIOBundleResourceFileKey)];
   cacheKey = [NSString stringWithFormat:@"%@.%@", bundleid, [iconName lastPathComponent]];
   if (!bundleid || !iconName) {
      eulock(e_lock);
#ifdef DIAGNOSTIC
      NSLog(@"ExtFS: Could not find icon path for %@.\n", [self bsdName]);
#endif
      return (nil);
   }
   
   /* Try the global cache */
   erlock(e_mediaIconCacheLck);
   if ((_icon = [e_mediaIconCache objectForKey:cacheKey])) {
      (void)[_icon retain]; // For ourself
      eulock(e_mediaIconCacheLck);
      eulock(e_lock);
#ifdef DIAGNOSTIC
      NSLog(@"ExtFS: Retrieved icon %@ from icon cache (%u).\n", cacheKey, [_icon retainCount]);
#endif
      return ([[_icon retain] autorelease]);
   }
   eulock(e_mediaIconCacheLck);
   
   /* Load the icon from disk */
   eulock(e_lock); // Drop the lock, as this can take many seconds
   ico = nil;
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
                  ico = [[NSImage alloc] initWithContentsOfURL:(NSURL*)iconurl];
                  CFRelease(iconurl);
               }
            }
            CFRelease(kexts);
         } /* kexts */
         CFRelease(url);
      } /* url */
   } /* FSFindFolder */
   
   ewlock(e_lock);
   // Now that we are under the lock again,
   // check to make sure no one beat us to the punch.
   if (nil != _icon) {
      eulock(e_lock);
      [ico release];
      goto emicon_exit;
   }
   
   if (ico) {
      ewlock(e_mediaIconCacheLck);
      // Somebody else could have added us to the cache
      if (nil == (_icon = [e_mediaIconCache objectForKey:cacheKey])) {
         _icon = ico;
         [_icon setName:cacheKey];
         // This supposedly allows images to be cached safely across threads
         [_icon setCachedSeparately:YES];
         [e_mediaIconCache setObject:_icon forKey:cacheKey];
#ifdef DIAGNOSTIC
         NSLog(@"ExtFS: Added icon %@ to icon cache (%u).\n", cacheKey, [_icon retainCount]);
#endif
      } else {
         // Use the cached icon instead of the one we found
         [ico release];
         (void)[_icon retain];
      }
      eulock(e_mediaIconCacheLck);
   } else {
      _attributeFlags |= kfsIconNotFound;
   }
   eulock(e_lock);

emicon_exit:
   return ([[_icon retain] autorelease]);
}

- (BOOL)isEjectable
{
   BOOL test;
   erlock(e_lock);
   test = (0 != (_attributeFlags & kfsEjectable));
   eulock(e_lock);
   return (test);
}

- (BOOL)canMount
{
   erlock(e_lock);
   if ((_attributeFlags & kfsNoMount)) {
      eulock(e_lock);
      return (NO);
   }
   eulock(e_lock);
   
   return (YES);
}

- (BOOL)isMounted
{
   BOOL test;
   erlock(e_lock);
   test = (0 != (_attributeFlags & kfsMounted));
   eulock(e_lock);
   return (test);
}

- (BOOL)isWritable
{
   BOOL test;
   erlock(e_lock);
   test = (0 != (_attributeFlags & kfsWritable));
   eulock(e_lock);
   return (test);
}

- (BOOL)isWholeDisk
{
   BOOL test;
   erlock(e_lock);
   test = (0 != (_attributeFlags & kfsWholeDisk));
   eulock(e_lock);
   return (test);
}

- (BOOL)isLeafDisk
{
   BOOL test;
   erlock(e_lock);
   test = (0 != (_attributeFlags & kfsLeafDisk));
   eulock(e_lock);
   return (test);
}

- (BOOL)isDVDROM
{
   BOOL test;
   erlock(e_lock);
   test = (0 != (_attributeFlags & kfsDVDROM));
   eulock(e_lock);
   return (test);
}

- (BOOL)isCDROM
{
   BOOL test;
   erlock(e_lock);
   test = (0 != (_attributeFlags & kfsCDROM));
   eulock(e_lock);
   return (test);
}

- (BOOL)usesDiskArb
{
   BOOL test;
   erlock(e_lock);
   test = (0 != (_attributeFlags & kfsDiskArb));
   eulock(e_lock);
   return (test);
}

#ifdef notyet
- (void)setUsesDiskArb:(BOOL)diskarb
{
   ewlock(e_lock);
   if (diskarb)
      _attributeFlags |= kfsDiskArb;
   else
      _attributeFlags &= ~kfsDiskArb;
   eulock(e_lock);
}
#endif

- (u_int64_t)size
{
   return (_size);
}

- (u_int32_t)blockSize
{
   u_int32_t sz;
   erlock(e_lock);
   sz = (_attributeFlags & kfsMounted) ? _fsBlockSize : _devBlockSize;
   eulock(e_lock);
   return (sz);
}

- (ExtFSType)fsType
{
   return (_fsType);
}

- (NSString*)fsName
{
    return (NSFSPrettyNameFromType(_fsType));
}

- (NSString*)mountPoint
{
   return ([[_where retain] autorelease]);
}

- (u_int64_t)availableSize
{
   u_int64_t sz;
   
   (void)[self fsInfo];
   
   erlock(e_lock);
   sz = _blockAvail * _fsBlockSize;
   eulock(e_lock);
   
   return (sz);
}

- (u_int64_t)blockCount
{
   return (_blockCount);
}

- (u_int64_t)fileCount
{
   (void)[self fsInfo];
   
   return (_fileCount);
}

- (u_int64_t)dirCount
{
   erlock(e_lock);
   if (_attributeFlags & kfsGetAttrlist) {
      eulock(e_lock);
      (void)[self fsInfo];
   } else {
      eulock(e_lock);
   }
      
   return (_dirCount);
}

- (NSString*)volName
{
   return ([[_volName retain] autorelease]);
}

- (BOOL)hasPermissions
{
   BOOL test;
   erlock(e_lock);
   test = (0 != (_attributeFlags & kfsPermsEnabled));
   eulock(e_lock);
   return (test);
}

- (BOOL)hasJournal
{
   BOOL test;
   erlock(e_lock);
   test = (0 != (_volCaps & VOL_CAP_FMT_JOURNAL));
   eulock(e_lock);
   return (test);
}

- (BOOL)isJournaled
{
   BOOL test;
   erlock(e_lock);
   test = (0 != (_volCaps & VOL_CAP_FMT_JOURNAL_ACTIVE));
   eulock(e_lock);
   return (test);
}

- (BOOL)isCaseSensitive
{
   BOOL test;
   erlock(e_lock);
   test = (0 != (_volCaps & VOL_CAP_FMT_CASE_SENSITIVE));
   eulock(e_lock);
   return (test);
}

- (BOOL)isCasePreserving
{
   BOOL test;
   erlock(e_lock);
   test = (0 != (_volCaps & VOL_CAP_FMT_CASE_PRESERVING));
   eulock(e_lock);
   return (test);
}

- (BOOL)hasSparseFiles
{
   BOOL test;
   erlock(e_lock);
   test = (0 != (_volCaps & VOL_CAP_FMT_SPARSE_FILES));
   eulock(e_lock);
   return (test);
}

#ifdef notyet
- (BOOL)hasSuper
{
   BOOL test;
   erlock(e_lock);
   test = (nil != _sb);
   eulock(e_lock);
   return (test);
}
#endif

/* Private (for now) -- caller must release returned object */
- (CFUUIDRef)uuid
{
   CFUUIDRef uuid = nil;
   CFUUIDBytes *bytes;
   
   erlock(e_lock);
   if (_sb) {
      bytes = (CFUUIDBytes*)&e2sblock->s_uuid[0];
      uuid = CFUUIDCreateFromUUIDBytes(kCFAllocatorDefault, *bytes);
   }
   eulock(e_lock);
   
   return (uuid);
}

- (BOOL)isExtFS
{
   BOOL test;
   erlock(e_lock);
   test = (_fsType == fsTypeExt2 || _fsType == fsTypeExt3);
   eulock(e_lock);
   return (test);
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
   BOOL test = NO;
   erlock(e_lock);
   if (_sb)
      test = (0 != EXT3_HAS_COMPAT_FEATURE(e2super, EXT3_FEATURE_COMPAT_DIR_INDEX));
   eulock(e_lock);
   return (test);
}

- (BOOL)hasLargeFiles
{
   BOOL test = NO;
   erlock(e_lock);
   if (_sb)
      test = (0 != EXT2_HAS_RO_COMPAT_FEATURE(e2super, EXT2_FEATURE_RO_COMPAT_LARGE_FILE));
   eulock(e_lock);
   return (test);
}

- (u_int64_t)maxFileSize
{
    return (0);
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
      return ([[self bsdName] isEqualToString:[obj bsdName]]);
   
   return (NO);
}

- (void)dealloc
{
   unsigned count;
#ifdef DIAGNOSTIC
   NSLog(@"ExtFS: Media '%@' dealloc.\n",
      [_media objectForKey:NSSTR(kIOBSDNameKey)]);
#endif
   
   // Icon cache
   ewlock(e_mediaIconCacheLck);
   count = [e_mediaIconCache retainCount];
   [e_mediaIconCache release];
   if (1 == count)
      e_mediaIconCache = nil;
   eulock(e_mediaIconCacheLck);
   // The mediaIconCache lock is never released.
   
   e2super_free();
   [_object release];
   [_icon release];
   [_iconDesc release];
   [_ioregName release];
   [_volName release];
   [_where release];
   [_media release];
   [_children release];
   [_parent release];
   
   edlock(e_lock);
   
   [super dealloc];
}

@end
