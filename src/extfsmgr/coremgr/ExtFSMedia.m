/*
* Copyright 2003-2006 Brian Bergstrand.
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

#import <stdlib.h>
#import <unistd.h>
#import <sys/param.h>
#import <sys/ucred.h>
#import <sys/mount.h>
#import <sys/attr.h>
#import <sys/ioctl.h>
#import <sys/syscall.h>
#import <pthread.h>

#import <ext2_byteorder.h>
#ifndef NOEXT2
#import <gnu/ext2fs/ext2_fs.h>
#endif

#import "ExtFSLock.h"
#import "ExtFSMedia.h"
#import "ExtFSMediaController.h"
#import "ExtFSMediaPrivate.h"

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

#ifndef NOEXT2
struct superblock {
   struct ext2_super_block *s_es;
};
#define e2super e_sb
#define e2sblock e2super->s_es

// Must be called with the write lock held
#define e2super_alloc() \
do { \
   e_sb = malloc(sizeof(struct superblock)); \
   if (e_sb) { \
      e2sblock = malloc(sizeof(struct ext2_super_block)); \
      if (!e2sblock) { free(e_sb); e_sb = nil; } \
   } \
} while(0)

// Must be called with the write lock held
#define e2super_free() \
do { \
   if (e_sb) { free(e2sblock); free(e_sb); e_sb = nil; } \
} while(0)
#else
#define e2super_free()
#endif

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
   
   path = MOUNTPOINTSTR(self);
   if (!path)
      return (EINVAL);
   
   gettimeofday(&now, nil);
   
   #ifndef NOEXT2
   NSString *bsdName = [self bsdName];
   /* Get the superblock if we don't have it */
   ewlock(e_lock);
   if ((fsTypeExt2 == e_fsType || fsTypeExt3 == e_fsType) && !e_sb) {
      e2super_alloc();
      if (e_sb)
         err = syscall(SYS_fsctl, path, EXT2_IOC_GETSBLOCK, e2sblock, 0);
      else
         err = ENOMEM;
      if (0 == err) {
         // Make sure our UUID copy is correct (ie a re-format since last mount)
         [e_uuid release];
         e_uuid = nil;
         eulock(e_lock);
         NSString *tmp = [self uuidString];
         ewlock(e_lock);
         e_uuid = [tmp retain];
      } else {
         NSLog(@"ExtFS: Failed to load superblock for device '%@' mounted on '%s' (%d).\n",
            bsdName, path, err);
         e2super_free();
      }
   }
   
   if (e_lastFSUpdate + VOL_INFO_CACHE_TIME > now.tv_sec) {
      eulock(e_lock);
      return (0);
   }
   e_lastFSUpdate = now.tv_sec;
   
   eulock(e_lock); // drop the lock while we do the I/O
   #endif
   
   bzero(&vinfo, sizeof(vinfo));
   bzero(&alist, sizeof(alist));
   alist.bitmapcount = ATTR_BIT_MAP_COUNT;
   alist.volattr = ATTR_VOL_INFO|ATTR_VOL_SIGNATURE|ATTR_VOL_SPACEAVAIL|
      ATTR_VOL_FILECOUNT|ATTR_VOL_DIRCOUNT;
   if (!e_volName)
      alist.volattr |= ATTR_VOL_CAPABILITIES|ATTR_VOL_NAME;
   
   err = getattrlist(path, &alist, &vinfo.vinfo, sizeof(vinfo.vinfo), 0);
   if (!err) {
      ewlock(e_lock);
      e_fileCount = vinfo.vinfo.v_filecount;
      e_dirCount = vinfo.vinfo.v_dircount;
      e_blockAvail = vinfo.vinfo.v_availspace / e_fsBlockSize;
      if (0 != vinfo.vinfo.v_name[VOL_CAPABILITIES_FORMAT])
         e_volName = [[NSString alloc] initWithUTF8String:vinfo.vinfo.v_name];
      if (alist.volattr & ATTR_VOL_CAPABILITIES)
         e_volCaps = vinfo.vinfo.v_caps.capabilities[0];
      if (fsTypeHFS == e_fsType && kHFSPlusSigWord == vinfo.vinfo.v_signature)
            e_fsType = fsTypeHFSPlus;
      else if (fsTypeHFS == e_fsType && kHFSXSigWord == vinfo.vinfo.v_signature)
         e_fsType = fsTypeHFSX;
      eulock(e_lock);
      goto eminfo_exit;
   }
   
   /* Fall back to statfs to get the info. */
   err = statfs(path, &vinfo.vstat);
   ewlock(e_lock);
   if (!err) {
      e_blockAvail = vinfo.vstat.f_bavail;
      e_fileCount = vinfo.vstat.f_files - vinfo.vstat.f_ffree;
   } else {
      e_lastFSUpdate = 0;
   }
   
   e_attributeFlags &= ~kfsGetAttrlist;
   eulock(e_lock);
   
eminfo_exit:
   if (!err) {
      EFSMPostNotification(ExtFSMediaNotificationUpdatedInfo, nil);
   }
   return (err);
}

- (void)probe
{
    int type;
    NSString *uuid, *tmp;
     
    if (0 == (e_attributeFlags & kfsNoMount)) {
        // Query the raw disk for its filesystem type
        NSString *probePath;
        probePath = [[ExtFSMediaController mediaController] pathForResource:EFS_PROBE_RSRC];
        if (probePath) {
            NSTask *probe = [[NSTask alloc] init];
            NSPipe *output = [[NSPipe alloc] init];
            [probe setStandardOutput:output];
            [probe setArguments:[NSArray arrayWithObjects:[self bsdName], nil]];
            [probe setLaunchPath:probePath];
            NS_DURING
            [probe launch];
            [probe waitUntilExit];
            NS_HANDLER
            // Launch failed
            NS_ENDHANDLER
            type = [probe terminationStatus];
            if (type >= 0 && type < fsTypeNULL) {
                NSFileHandle *f;
                NSData *d;
                unsigned len;
                
                // See if there was a UUID output
                f = [output fileHandleForReading];
                uuid = nil;
                if ((d = [f availableData]) && (len = [d length]) > 0)
                    uuid = [[NSString alloc] initWithBytes:[d bytes]
                        length:len encoding:NSUTF8StringEncoding];
                
                tmp = nil;
                ewlock(e_lock);
                e_fsType = type;
                if (uuid) {
                    tmp = e_uuid;
                    e_uuid = uuid;
                }
                eulock(e_lock);
                [tmp release];
            }
        #ifdef DIAGNOSTIC
            else
                NSLog(@"ExtFS: efsprobe failed with '%d'.\n", type);
        #endif
            [output release];
            [probe release];
        }
    }
}

/* Public */

- (ExtFSMedia*)initWithIORegProperties:(NSDictionary*)properties
{
   NSString *hint;
   NSRange r;
   
   if ((self = [super init])) {
      if (nil == e_mediaIconCacheLck) {
         // Just to make sure someone else doesn't get in during creation...
         e_mediaIconCacheLck = (void*)E2_BAD_ADDR;
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
      
      e_media = [properties retain];
      e_size = [[e_media objectForKey:NSSTR(kIOMediaSizeKey)] unsignedLongLongValue];
      e_devBlockSize = [[e_media objectForKey:NSSTR(kIOMediaPreferredBlockSizeKey)] unsignedLongValue];
      e_fsType = fsTypeUnknown;
      e_opticalType = efsOpticalTypeUnknown;
      
      e_attributeFlags = kfsDiskArb | kfsGetAttrlist;
      if ([[e_media objectForKey:NSSTR(kIOMediaEjectableKey)] boolValue])
         e_attributeFlags |= kfsEjectable;
      if ([[e_media objectForKey:NSSTR(kIOMediaWritableKey)] boolValue])
         e_attributeFlags |= kfsWritable;
      if ([[e_media objectForKey:NSSTR(kIOMediaWholeKey)] boolValue])
         e_attributeFlags |= kfsWholeDisk;
      if ([[e_media objectForKey:NSSTR(kIOMediaLeafKey)] boolValue])
         e_attributeFlags |= kfsLeafDisk;
      
      hint = [e_media objectForKey:NSSTR(kIOMediaContentHintKey)];
      r = [hint rangeOfString:@"partition"];
      if (hint && NSNotFound != r.location)
         e_attributeFlags |= kfsNoMount;
         
      r = [hint rangeOfString:@"Driver"];
      if (hint && NSNotFound != r.location)
         e_attributeFlags |= kfsNoMount;
         
      r = [hint rangeOfString:@"Patches"];
      if (hint && NSNotFound != r.location)
         e_attributeFlags |= kfsNoMount;
      
      r = [hint rangeOfString:@"CD_DA"]; /* Digital Audio tracks */
      if (hint && NSNotFound != r.location)
         e_attributeFlags |= kfsNoMount;
         
      hint = [e_media objectForKey:NSSTR(kIOMediaContentKey)];
      r = [hint rangeOfString:@"partition"];
      if (hint && NSNotFound != r.location)
         e_attributeFlags |= kfsNoMount;
      
      [self probe];
      
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
   id tmp;
   erlock(e_lock);
   tmp = [e_object retain];
   eulock(e_lock);
   return ([tmp autorelease]);
}

- (void)setRepresentedObject:(id)object
{
   id tmp;
   (void)[object retain];
   ewlock(e_lock);
   tmp = e_object;
   e_object = object;
   eulock(e_lock);
   [tmp release];
}

- (ExtFSMedia*)parent
{
   return ([[e_parent retain] autorelease]);
}

- (NSArray*)children
{
   NSArray *c;
   erlock(e_lock);
   /* We have to return a copy, so the caller
      can use the array in a thread safe context.
    */
   c = [e_children copy];
   eulock(e_lock);
   return ([c autorelease]);
}

- (unsigned)childCount
{
   unsigned ct;
   erlock(e_lock);
   ct  = [e_children count];
   eulock(e_lock);
   return (ct);
}

- (void)addChild:(ExtFSMedia*)media
{
#ifdef DIAGNOSTIC
   NSString *myname = [self bsdName], *oname = [media bsdName];
#endif   
   ewlock(e_lock);
   if (!e_children)
      e_children = [[NSMutableArray alloc] init];

   if ([e_children containsObject:media]) {
#ifdef DIAGNOSTIC
      NSLog(@"ExtFS: Oops! Parent '%@' already contains child '%@'.\n",
         myname, oname);
#endif
      eulock(e_lock);
      return;
   };

   [e_children addObject:media];
   eulock(e_lock);
   EFSMPostNotification(ExtFSMediaNotificationChildChange, nil);
}

- (void)remChild:(ExtFSMedia*)media
{
#ifdef DIAGNOSTIC
   NSString *myname = [self bsdName], *oname = [media bsdName];
#endif
   ewlock(e_lock);
   if (nil == e_children || NO == [e_children containsObject:media]) {
#ifdef DIAGNOSTIC
      NSLog(@"ExtFS: Oops! Parent '%@' does not contain child '%@'.\n",
         myname, oname);
#endif
      eulock(e_lock);
      return;
   };

   [e_children removeObject:media];
   eulock(e_lock);
   EFSMPostNotification(ExtFSMediaNotificationChildChange, nil);
}

- (NSString*)ioRegistryName
{
   return ([[e_ioregName retain] autorelease]);
}

- (NSString*)bsdName
{
   NSString *name;
   erlock(e_lock);
   name = [[e_media objectForKey:NSSTR(kIOBSDNameKey)] retain];
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
   if (e_icon) {
      eulock(e_lock);
      return ([[e_icon retain] autorelease]);
   }
   euplock(e_lock); // Upgrade to write lock
   
   if (e_parent && (e_icon = [e_parent icon])) {
      (void)[e_icon retain]; // For ourself
      eulock(e_lock);
#ifdef DIAGNOSTIC
      NSLog(@"ExtFS: Retrieved icon %@ from %@ (%u).\n", [e_icon name], e_parent, [e_icon retainCount]);
#endif
      return ([[e_icon retain] autorelease]);
   }
   
   if (e_attributeFlags & kfsIconNotFound) {
      eulock(e_lock);
      return (nil);
   }
   
   bundleid = [e_iconDesc objectForKey:(NSString*)kCFBundleIdentifierKey];
   iconName = [e_iconDesc objectForKey:NSSTR(kIOBundleResourceFileKey)];
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
   if ((e_icon = [e_mediaIconCache objectForKey:cacheKey])) {
      (void)[e_icon retain]; // For ourself
      eulock(e_mediaIconCacheLck);
      eulock(e_lock);
#ifdef DIAGNOSTIC
      NSLog(@"ExtFS: Retrieved icon %@ from icon cache (%u).\n", cacheKey, [e_icon retainCount]);
#endif
      return ([[e_icon retain] autorelease]);
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
   if (nil != e_icon) {
      eulock(e_lock);
      [ico release];
      goto emicon_exit;
   }
   
   if (ico) {
      ewlock(e_mediaIconCacheLck);
      // Somebody else could have added us to the cache
      if (nil == (e_icon = [e_mediaIconCache objectForKey:cacheKey])) {
         e_icon = ico;
         [e_icon setName:cacheKey];
         // This supposedly allows images to be cached safely across threads
         [e_icon setCachedSeparately:YES];
         [e_mediaIconCache setObject:e_icon forKey:cacheKey];
#ifdef DIAGNOSTIC
         NSLog(@"ExtFS: Added icon %@ to icon cache (%u).\n", cacheKey, [e_icon retainCount]);
#endif
      } else {
         // Use the cached icon instead of the one we found
         [ico release];
         (void)[e_icon retain];
      }
      eulock(e_mediaIconCacheLck);
   } else {
      e_attributeFlags |= kfsIconNotFound;
   }
   eulock(e_lock);

emicon_exit:
   return ([[e_icon retain] autorelease]);
}

- (BOOL)isEjectable
{
   BOOL test;
   erlock(e_lock);
   test = (0 != (e_attributeFlags & kfsEjectable));
   eulock(e_lock);
   return (test);
}

- (BOOL)canMount
{
   erlock(e_lock);
   if ((e_attributeFlags & kfsNoMount)) {
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
   test = (0 != (e_attributeFlags & kfsMounted));
   eulock(e_lock);
   return (test);
}

- (BOOL)isWritable
{
   BOOL test;
   erlock(e_lock);
   test = (0 != (e_attributeFlags & kfsWritable));
   eulock(e_lock);
   return (test);
}

- (BOOL)isWholeDisk
{
   BOOL test;
   erlock(e_lock);
   test = (0 != (e_attributeFlags & kfsWholeDisk));
   eulock(e_lock);
   return (test);
}

- (BOOL)isLeafDisk
{
   BOOL test;
   erlock(e_lock);
   test = (0 != (e_attributeFlags & kfsLeafDisk));
   eulock(e_lock);
   return (test);
}

- (BOOL)isOptical
{
   BOOL test;
   erlock(e_lock);
   test = (0 != (e_attributeFlags & kfsDVDROM)) || (0 != (e_attributeFlags & kfsCDROM));
   eulock(e_lock);
   return (test);
}

- (ExtFSOpticalMediaType)opticalMediaType
{
    return (e_opticalType);
}

- (BOOL)usesDiskArb
{
   BOOL test;
   erlock(e_lock);
   test = (0 != (e_attributeFlags & kfsDiskArb));
   eulock(e_lock);
   return (test);
}

#ifdef notyet
- (void)setUsesDiskArb:(BOOL)diskarb
{
   ewlock(e_lock);
   if (diskarb)
      e_attributeFlags |= kfsDiskArb;
   else
      e_attributeFlags &= ~kfsDiskArb;
   eulock(e_lock);
}
#endif

- (u_int64_t)size
{
   return (e_size);
}

- (u_int32_t)blockSize
{
   u_int32_t sz;
   erlock(e_lock);
   sz = (e_attributeFlags & kfsMounted) ? e_fsBlockSize : e_devBlockSize;
   eulock(e_lock);
   return (sz);
}

- (ExtFSType)fsType
{
   return (e_fsType);
}

- (NSString*)fsName
{
    return (EFSNSPrettyNameFromType(e_fsType));
}

- (NSString*)mountPoint
{
   return ([[e_where retain] autorelease]);
}

- (u_int64_t)availableSize
{
   u_int64_t sz;
   
   (void)[self fsInfo];
   
   erlock(e_lock);
   sz = e_blockAvail * e_fsBlockSize;
   eulock(e_lock);
   
   return (sz);
}

- (u_int64_t)blockCount
{
   return (e_blockCount);
}

- (u_int64_t)fileCount
{
   (void)[self fsInfo];
   
   return (e_fileCount);
}

- (u_int64_t)dirCount
{
   erlock(e_lock);
   if (e_attributeFlags & kfsGetAttrlist) {
      eulock(e_lock);
      (void)[self fsInfo];
   } else {
      eulock(e_lock);
   }
      
   return (e_dirCount);
}

- (NSString*)volName
{
   return ([[e_volName retain] autorelease]);
}

- (BOOL)hasPermissions
{
   BOOL test;
   erlock(e_lock);
   test = (0 != (e_attributeFlags & kfsPermsEnabled));
   eulock(e_lock);
   return (test);
}

- (BOOL)hasJournal
{
   BOOL test;
   erlock(e_lock);
   test = (0 != (e_volCaps & VOL_CAP_FMT_JOURNAL));
   eulock(e_lock);
   return (test);
}

- (BOOL)isJournaled
{
   BOOL test;
   erlock(e_lock);
   test = (0 != (e_volCaps & VOL_CAP_FMT_JOURNAL_ACTIVE));
   eulock(e_lock);
   return (test);
}

- (BOOL)isCaseSensitive
{
   BOOL test;
   erlock(e_lock);
   test = (0 != (e_volCaps & VOL_CAP_FMT_CASE_SENSITIVE));
   eulock(e_lock);
   return (test);
}

- (BOOL)isCasePreserving
{
   BOOL test;
   erlock(e_lock);
   test = (0 != (e_volCaps & VOL_CAP_FMT_CASE_PRESERVING));
   eulock(e_lock);
   return (test);
}

- (BOOL)hasSparseFiles
{
   BOOL test;
   erlock(e_lock);
   test = (0 != (e_volCaps & VOL_CAP_FMT_SPARSE_FILES));
   eulock(e_lock);
   return (test);
}

#ifdef notyet
- (BOOL)hasSuper
{
   BOOL test;
   erlock(e_lock);
   test = (nil != e_sb);
   eulock(e_lock);
   return (test);
}
#endif

/* Private (for now) -- caller must release returned object */
- (CFUUIDRef)uuid
{
   CFUUIDRef uuid = nil;
   #ifndef NOEXT2
   CFUUIDBytes *bytes;
   
   erlock(e_lock);
   if (e_sb) {
      bytes = (CFUUIDBytes*)&e2sblock->s_uuid[0];
      uuid = CFUUIDCreateFromUUIDBytes(kCFAllocatorDefault, *bytes);
   }
   eulock(e_lock);
   #endif
   
   return (uuid);
}

- (BOOL)isExtFS
{
   BOOL test;
   erlock(e_lock);
   test = (e_fsType == fsTypeExt2 || e_fsType == fsTypeExt3);
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
   } else if (e_uuid) {
      return ([[e_uuid retain] autorelease]);
   }
   
   return (nil);
}

- (BOOL)hasIndexedDirs
{
   BOOL test = NO;
   #ifndef NOEXT2
   erlock(e_lock);
   if (e_sb)
      test = (0 != EXT3_HAS_COMPAT_FEATURE(e2super, EXT3_FEATURE_COMPAT_DIR_INDEX));
   eulock(e_lock);
   #endif
   return (test);
}

- (BOOL)hasLargeFiles
{
   BOOL test = NO;
   #ifndef NOEXT2
   erlock(e_lock);
   if (e_sb)
      test = (0 != EXT2_HAS_RO_COMPAT_FEATURE(e2super, EXT2_FEATURE_RO_COMPAT_LARGE_FILE));
   eulock(e_lock);
   #endif
   return (test);
}

- (u_int64_t)maxFileSize
{
    return (0);
}

- (ExtFSIOTransportType)transportType
{
    return (e_ioTransport & efsIOTransportTypeMask);
}

- (ExtFSIOTransportType)transportBus
{
    return (e_ioTransport & efsIOTransportBusMask);
}

- (NSString*)transportName
{
    return (EFSIOTransportNameFromType((e_ioTransport & efsIOTransportBusMask)));
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
      [e_media objectForKey:NSSTR(kIOBSDNameKey)]);
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
   [e_object release];
   [e_icon release];
   [e_iconDesc release];
   [e_ioregName release];
   [e_volName release];
   [e_where release];
   [e_media release];
   [e_uuid release];
   [e_children release];
   [e_parent release];
   
   edlock(e_lock);
   
   [super dealloc];
}

@end

@implementation ExtFSMedia (ExtFSMediaLockMgr)

- (void)lock:(BOOL)exclusive
{
    exclusive ? ewlock(e_lock) : erlock(e_lock);
}

- (void)unlock
{
    eulock(e_lock);
}

@end
