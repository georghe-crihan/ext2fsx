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

#import <string.h>
#import <sys/param.h>
#import <sys/ucred.h>
#import <sys/mount.h>

#import <mach/mach_port.h>
#import <mach/mach_interface.h>
#import <mach/mach_init.h>

#import "DiskArbitrationPrivate.h"

#import <IOKit/IOMessage.h>
#import <IOKit/IOBSD.h>
#import <IOKit/storage/IOMedia.h>
#import <IOKit/storage/IOCDMedia.h>
#import <IOKit/storage/IODVDMedia.h>

#import "ExtFSMedia.h"
#import "ExtFSMediaController.h"

static ExtFSMediaController *_instance;
NSString *ExtFSMediaNotificationAppeared = @"ExtFSMediaNotificationAppeared";
NSString *ExtFSMediaNotificationDisappeared = @"ExtFSMediaNotificationDisappeared";
NSString *ExtFSMediaNotificationMounted = @"ExtFSMediaNotificationMounted";
NSString *ExtFSMediaNotificationUnmounted = @"ExtFSMediaNotificationUnmounted";
NSString *ExtFSMediaNotificationCreationFailed = @"ExtFSMediaNotificationCreationFailed";


static IONotificationPortRef notify_port_ref=0;
static io_iterator_t notify_add_iter=0, notify_rem_iter=0;

#ifndef EXT2FS_NAME
#define EXT2FS_NAME "ext2"
#endif
#ifndef EXT3FS_NAME
#define EXT3FS_NAME "ext3"
#endif

#define HFS_NAME "hfs"
#define UFS_NAME "ufs"
#define CD9660_NAME "cd9660"
#define UDF_NAME "udf"
#define MSDOS_NAME "msdos"
#define NTFS_NAME "ntfs"

char *fsNameTable[] = {
   EXT2FS_NAME,
   EXT3FS_NAME,
   HFS_NAME,
   UFS_NAME,
   CD9660_NAME,
   UDF_NAME,
   MSDOS_NAME,
   NTFS_NAME,
   "Unknown",
   nil
};

static void iomatch_add_callback(void *refcon, io_iterator_t iter);
static void iomatch_rem_callback(void *refcon, io_iterator_t iter);
static void DiskArbCallback_DiskAppearedWithMountpoint(char *device,
   unsigned flags, char *mountpoint);
static void DiskArbCallback_UnmountPostNotification(DiskArbDiskIdentifier device,
   int errorCode, pid_t dissenter);

@interface ExtFSMediaController (Private)
- (void)updateMountStatus;
- (ExtFSMedia*)createMediaObjectWithProperties:(NSDictionary*)props;
- (int)updateMedia:(io_iterator_t)iter remove:(BOOL)remove;
- (BOOL)volumeDidUnmount:(NSString*)name;
@end

@interface ExtFSMedia (ExtFSMediaControllerPrivate)
- (void)updateAttributesFromIOService:(io_service_t)service;
- (void)setIsMounted:(struct statfs*)stat;
- (NSDictionary*)iconDescription;
/* Implemented in ExtFSMedia -- this just gets rid of the compiler warning. */
- (int)fsInfo;
@end

@implementation ExtFSMediaController : NSObject

/* Private */

- (void)updateMountStatus
{
   int count, i;
   struct statfs *stats;
   NSString *device;
   ExtFSMedia *e2media;
   
   count = getmntinfo(&stats, MNT_WAIT);
   if (!count)
      return;
   
   for (i = 0; i < count; ++i) {
      device = [NSSTR(stats[i].f_mntfromname) lastPathComponent];      
      e2media = [_media objectForKey:device];
      if (!e2media || [e2media isMounted])
         continue;
      
      [e2media setIsMounted:&stats[i]];
   }
}

- (BOOL)volumeDidUnmount:(NSString*)device
{
   ExtFSMedia *e2media;
   
   e2media = [_media objectForKey:device];
   if (e2media && [e2media isMounted]) {
      [e2media setIsMounted:nil];
      return (YES);
   }
   
   return (NO);
}

- (ExtFSMedia*)createMediaObjectWithProperties:(NSDictionary*)props
{
   ExtFSMedia *e2media;
   
   e2media = [[ExtFSMedia alloc] initWithIORegProperties:props];
   if (e2media) {
      [_media setObject:e2media forKey:[e2media bsdName]];
      [[NSNotificationCenter defaultCenter]
         postNotificationName:ExtFSMediaNotificationAppeared object:e2media
         userInfo:nil];
      [e2media release];
   } else {
      [[NSNotificationCenter defaultCenter]
         postNotificationName:ExtFSMediaNotificationCreationFailed
         object:[props objectForKey:NSSTR(kIOBSDNameKey)]
         userInfo:nil];
   }
   return (e2media);
}


- (void)removeMedia:(ExtFSMedia*)e2media device:(NSString *)device
{
   ExtFSMedia *parent;
   
   if ([e2media isMounted])
      NSLog(@"ExtFS: Oops! Device %@ disappeared while still marked as mounted\n", device);
   
   [e2media retain];
   [_media removeObjectForKey:device];
   [[NSNotificationCenter defaultCenter]
      postNotificationName:ExtFSMediaNotificationDisappeared
      object:e2media userInfo:nil];
   
   if ((parent = [e2media parent]))
      [parent remChild:e2media];
#ifdef DIAGNOSTIC
   NSLog(@"ExtFS: Media %@ disappeared. Retain count = %u.\n",
      device, [e2media retainCount]);
#endif
   [e2media release];
}

- (int)updateMedia:(io_iterator_t)iter remove:(BOOL)remove
{
   io_service_t iomedia;
   kern_return_t kr = 0;
   CFMutableDictionaryRef properties;
   ExtFSMedia *e2media, *parent;
   NSString *device;
   
   /* Init all media devices */
   while ((iomedia = IOIteratorNext(iter))) {
      kr = IORegistryEntryCreateCFProperties(iomedia, &properties,
         kCFAllocatorDefault, 0);
      if (0 == kr) {
         device = [(NSMutableDictionary*)properties objectForKey:NSSTR(kIOBSDNameKey)];
         e2media = [_media objectForKey:device];
         if (e2media) {
            if (remove)
               [self removeMedia:e2media device:device];
         #ifdef DIAGNOSTIC
            else {
               NSLog(@"ExtFS: Existing media %@ appeared again.\n", device);
            }
         #endif
            continue;
         }
         
         e2media = [self createMediaObjectWithProperties:(NSDictionary*)properties];
         [e2media updateAttributesFromIOService:iomedia];
         if ((parent = [e2media parent]))
            [parent addChild:e2media];
         
         CFRelease(properties);
      }
      IOObjectRelease(iomedia);
   }
   
   [self updateMountStatus];
   
   return (0);
}

/* Public */

+ (ExtFSMediaController*)mediaController
{
   if (!_instance) {
      (void)[[ExtFSMediaController alloc] init];
   }
   
   return (_instance);
} 

- (unsigned)mediaCount
{
   return ([_media count]);
}

- (NSArray*)media
{
   return ([_media allValues]);
}

- (NSArray*)rootMedia
{
   NSMutableArray *array;
   NSEnumerator *en;
   ExtFSMedia *e2media;
   
   array = [NSMutableArray array];
   en = [_media objectEnumerator];
   while ((e2media = [en nextObject])) {
      if (nil == [e2media parent]) {
         [array addObject:e2media];
      }
   }
   
   return (array);
}

- (NSArray*)mediaWithFSType:(ExtFSType)fstype
{
   NSMutableArray *array;
   NSEnumerator *en;
   ExtFSMedia *e2media;
   
   array = [NSMutableArray array];
   en = [_media objectEnumerator];
   while ((e2media = [en nextObject])) {
      if (fstype == [e2media fsType]) {
         [array addObject:e2media];
      }
   }
   
   return (array);
}

- (ExtFSMedia*)mediaWithBSDName:(NSString*)device
{
   return ([_media objectForKey:device]);
}

- (int)mount:(ExtFSMedia*)media on:(NSString*)dir
{
   return (DiskArbRequestMount_auto(BSDNAMESTR(media)));
}

- (int)unmount:(ExtFSMedia*)media force:(BOOL)force eject:(BOOL)eject
{
   int flags = 0;
   
   if (force)
      flags |= kDiskArbForceUnmountFlag;
   
   if ([media isEjectable] && eject)
      flags |= kDiskArbUnmountAndEjectFlag;
   
   return (DiskArbUnmountRequest_async_auto(BSDNAMESTR(media), flags));
}

/* Super */

- (id)init
{
   kern_return_t kr = 0;
   CFRunLoopSourceRef rlSource;
   
   if (_instance)
      return (_instance);
   
   if (!(self = [super init]))
      return (nil);
      
   _instance = self;
   
   notify_port_ref = IONotificationPortCreate(kIOMasterPortDefault);
	if (0 == notify_port_ref) {
		kr = 1;
      goto exit;
	}
   rlSource = IONotificationPortGetRunLoopSource(notify_port_ref);
   CFRunLoopAddSource([[NSRunLoop currentRunLoop] getCFRunLoop], rlSource,
      kCFRunLoopDefaultMode);
   
   /* Setup callbacks to get notified of additions/removals. */
   kr = IOServiceAddMatchingNotification (notify_port_ref,
      kIOMatchedNotification, IOServiceMatching(kIOMediaClass),
      iomatch_add_callback, nil, &notify_add_iter);
   if (kr || 0 == notify_add_iter) {
      goto exit;
   }
   kr = IOServiceAddMatchingNotification (notify_port_ref,
      kIOTerminatedNotification, IOServiceMatching(kIOMediaClass),
      iomatch_rem_callback, nil, &notify_rem_iter);
   if (kr || 0 == notify_rem_iter) {
      goto exit;
   }
   
   _media = [[NSMutableDictionary alloc] init];
   /* Process the initial registry entrires */
   iomatch_add_callback(nil, notify_add_iter);
   iomatch_rem_callback(nil, notify_rem_iter);
   
   /* Init Disk Arb */
   kr = DiskArbInit();
   DiskArbAddCallbackHandler(kDA_DISK_UNMOUNT_POST_NOTIFY,
      (void *)&DiskArbCallback_UnmountPostNotification, 0);
   DiskArbAddCallbackHandler(kDA_DISK_APPEARED_WITH_MT,
      (void *)&DiskArbCallback_DiskAppearedWithMountpoint, 0);
   DiskArbUpdateClientFlags();
   
   return (self);

exit:
   _instance = nil;
   [_media release];
   if (notify_add_iter) IOObjectRelease(notify_add_iter);
   if (notify_rem_iter) IOObjectRelease(notify_rem_iter);
   if (notify_port_ref) IONotificationPortDestroy(notify_port_ref);
   [super dealloc];
   return (nil);
}

/* This singleton lives forever. */
#if 0
- (void)dealloc
{
   DiskArbRemoveCallbackHandler(kDA_DISK_UNMOUNT_POST_NOTIFY,
      (void *)&DiskArbCallback_UnmountPostNotification);
   [_media release];
   [super dealloc];
}
#endif

@end

@implementation ExtFSMedia (ExtFSMediaController)

- (void)updateAttributesFromIOService:(io_service_t)service
{
   ExtFSMediaController *mc;
   CFTypeRef iconDesc;
   io_name_t ioname;
   io_service_t ioparent, ioparentold;
#ifdef notyet
   io_iterator_t piter;
#endif
   int iterations;
   #define MAX_PARENT_ITERS 10
   kern_return_t kr;
   
   mc = [ExtFSMediaController mediaController];
   
   /* Get IOKit name */
   if (0 == IORegistryEntryGetNameInPlane(service, kIOServicePlane, ioname))
      _ioregName = [NSSTR(ioname) retain];
   
   /* Get Parent */
#ifdef notyet
   /* This seems to only return the first parent, don't know what's going on */
   if (0 == IORegistryEntryGetParentIterator(service, kIOServicePlane, &piter)) {
      while ((ioparent = IOIteratorNext(piter))) {
#endif
   if (!(_attributeFlags & kfsWholeDisk)) {
      ioparent = nil;
      iterations = 0;
      kr = IORegistryEntryGetParentEntry(service, kIOServicePlane, &ioparent);
      while (!kr && ioparent && iterations < MAX_PARENT_ITERS) {
         /* Break on the first IOMedia parent */
         if (IOObjectConformsTo(ioparent, kIOMediaClass))
            break;
         /* Otherwise release it */
         ioparentold = ioparent;
         ioparent = nil;
         kr = IORegistryEntryGetParentEntry(ioparentold, kIOServicePlane, &ioparent);
         IOObjectRelease(ioparentold);
         iterations++;
      }
#ifdef notyet
      IOObjectRelease(piter);
#endif
      
      if (ioparent) {
         CFMutableDictionaryRef props;
         kr = IORegistryEntryCreateCFProperties(ioparent, &props,
            kCFAllocatorDefault, 0);
         if (!kr) {
            ExtFSMedia *parent;
            NSString *pdevice;
            
            /* See if the parent object exists */
            pdevice = [(NSDictionary*)props objectForKey:NSSTR(kIOBSDNameKey)];
            parent = [mc mediaWithBSDName:pdevice];
            if (!parent) {
               /* Parent does not exist */
               parent = 
                  [mc createMediaObjectWithProperties:(NSDictionary*)props];
               [parent updateAttributesFromIOService:ioparent];
            }
            _parent = [parent retain];
            
            CFRelease(props);
         }
         
         IOObjectRelease(ioparent);
      }
   }
   
   /* Find the icon description */
   if (!_parent || !(iconDesc = [_parent iconDescription]))
      iconDesc = IORegistryEntrySearchCFProperty(service, kIOServicePlane,
         CFSTR(kIOMediaIconKey), kCFAllocatorDefault,
         kIORegistryIterateParents | kIORegistryIterateRecursively);
   
   [_iconDesc release];
   _iconDesc = [(NSDictionary*)iconDesc retain];
   
   if (IOObjectConformsTo(service, kIODVDMediaClass))
      _attributeFlags |= kfsDVDROM;
   
   if (IOObjectConformsTo(service, kIOCDMediaClass))
      _attributeFlags |= kfsCDROM;
}

- (void)setIsMounted:(struct statfs*)stat
{
   if (!stat) {
      _attributeFlags &= ~kfsMounted;
      _fsBlockSize = 0;
      _blockCount = 0;
      _blockAvail = 0;
      _lastFSUpdate = 0;
      [_where release]; _where = nil;
      
      /* Reset the write flag in case the device is writable,
         but the mounted filesystem was not */
      if ([[_media objectForKey:NSSTR(kIOMediaWritableKey)] boolValue])
         _attributeFlags |= kfsWritable;
      
      [[NSNotificationCenter defaultCenter]
         postNotificationName:ExtFSMediaNotificationUnmounted object:self
         userInfo:nil];
      return;
   }
   
   if (!strncmp(stat->f_fstypename, EXT2FS_NAME, sizeof(EXT2FS_NAME)))
      _fsType = fsTypeExt2;
   else
   if (!strncmp(stat->f_fstypename, EXT3FS_NAME, sizeof(EXT3FS_NAME)))
      _fsType = fsTypeExt3;
   else
   if (!strncmp(stat->f_fstypename, HFS_NAME, sizeof(HFS_NAME)))
      _fsType = fsTypeHFS;
   else
   if (!strncmp(stat->f_fstypename, UFS_NAME, sizeof(UFS_NAME)))
      _fsType = fsTypeUFS;
   else
   if (!strncmp(stat->f_fstypename, CD9660_NAME, sizeof(CD9660_NAME)))
      _fsType = fsTypeCD9660;
   else
   if (!strncmp(stat->f_fstypename, UDF_NAME, sizeof(UDF_NAME)))
      _fsType = fsTypeUDF;
   else
   if (!strncmp(stat->f_fstypename, MSDOS_NAME, sizeof(MSDOS_NAME)))
      _fsType = fsTypeMSDOS;
   else
   if (!strncmp(stat->f_fstypename, NTFS_NAME, sizeof(NTFS_NAME)))
      _fsType = fsTypeNTFS;
   else
      _fsType = fsTypeUnknown;
   
   _attributeFlags |= kfsMounted;
   if (stat->f_flags & MNT_RDONLY)
      _attributeFlags &= ~kfsWritable;
   
   _fsBlockSize = stat->f_bsize;
   _blockCount = stat->f_blocks;
   _blockAvail = stat->f_bavail;
   [_where release];
   _where = [[NSString alloc] initWithCString:stat->f_mntonname];
   (void)[self fsInfo];
   [[NSNotificationCenter defaultCenter]
         postNotificationName:ExtFSMediaNotificationMounted object:self
         userInfo:nil];
}

- (NSDictionary*)iconDescription
{
   return (_iconDesc);
}

@end

/* Callbacks */

static void iomatch_add_callback(void *refcon, io_iterator_t iter)
{
   [[ExtFSMediaController mediaController] updateMedia:iter remove:NO];
}

static void iomatch_rem_callback(void *refcon, io_iterator_t iter)
{
   [[ExtFSMediaController mediaController] updateMedia:iter remove:YES];
}

static void DiskArbCallback_DiskAppearedWithMountpoint(char *device,
   unsigned flags, char *mountpoint)
{
   (void)[[ExtFSMediaController mediaController] updateMountStatus];
}

static void DiskArbCallback_UnmountPostNotification(DiskArbDiskIdentifier device,
   int errorCode, pid_t dissenter)
{
   (void)[[ExtFSMediaController mediaController] volumeDidUnmount:NSSTR(device)];
}
