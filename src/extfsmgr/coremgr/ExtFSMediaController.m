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

#import "ExtFSLock.h"
#import "ExtFSMedia.h"
#import "ExtFSMediaController.h"

NSString * const ExtFSMediaNotificationAppeared = @"ExtFSMediaNotificationAppeared";
NSString * const ExtFSMediaNotificationDisappeared = @"ExtFSMediaNotificationDisappeared";
NSString * const ExtFSMediaNotificationMounted = @"ExtFSMediaNotificationMounted";
NSString * const ExtFSMediaNotificationUnmounted = @"ExtFSMediaNotificationUnmounted";
NSString * const ExtFSMediaNotificationCreationFailed = @"ExtFSMediaNotificationCreationFailed";
NSString * const ExtFSMediaNotificationOpFailure = @"ExtFSMediaNotificationOpFailure";

static ExtFSMediaController *e_instance;
static void* e_instanceLock = nil; // Ptr to global controller internal lock
static pthread_mutex_t e_initMutex = PTHREAD_MUTEX_INITIALIZER;
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
#define CDAUDIO_NAME "cddafs"
#define UDF_NAME "udf"
#define MSDOS_NAME "msdos"
#define NTFS_NAME "ntfs"

static const char *e_fsNames[] = {
   EXT2FS_NAME,
   EXT3FS_NAME,
   HFS_NAME,
   HFS_NAME,
   HFS_NAME,
   HFS_NAME,
   UFS_NAME,
   CD9660_NAME,
   CDAUDIO_NAME,
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
static void DiskArbCallback_CallFailedNotification(DiskArbDiskIdentifier device,
   int type, int status);

@interface ExtFSMediaController (Private)
- (void)updateMountStatus;
- (ExtFSMedia*)createMediaWithIOService:(io_service_t)service properties:(NSDictionary*)props;
- (int)updateMedia:(io_iterator_t)iter remove:(BOOL)remove;
- (BOOL)volumeDidUnmount:(NSString*)name;
- (void)removePending:(ExtFSMedia*)media;
@end

@interface ExtFSMedia (ExtFSMediaControllerPrivate)
- (void)updateAttributesFromIOService:(io_service_t)service;
- (void)setIsMounted:(struct statfs*)stat;
- (NSDictionary*)iconDescription;
- (void)addChild:(ExtFSMedia*)media;
- (void)remChild:(ExtFSMedia*)media;
/* Implemented in ExtFSMedia.m -- this just gets rid of the compiler warnings. */
- (int)fsInfo;
@end

enum {
    kPendingMounts = 0,
    kPendingUMounts = 1,
    kPendingCount
};
enum {
    kNoteArgName = 0,
    kNoteArgObject = 1,
    kNoteArgInfo = 2
};

// This may end up conflicting with Disk Arb at some point,
// but as of Panther the largest DA Error is (1<<3).
#define EXT_DISK_ARB_MOUNT_FAILURE (1<<31)
#define EXT_MOUNT_ERROR_DELAY 4.0
#define EXTFS_DM_BNDL_ID @"net.sourceforge.ext2fsx.ExtFSDiskManager"

@implementation ExtFSMediaController : NSObject

/* Private */

- (void)postNotification:(NSArray*)args
{
    unsigned ct = [args count];
    id obj = (ct >= kNoteArgObject+1) ? [args objectAtIndex:kNoteArgObject] : nil;
    NSDictionary *info = (ct >= kNoteArgInfo+1) ? [args objectAtIndex:kNoteArgInfo] : nil;
    [[NSNotificationCenter defaultCenter]
         postNotificationName:[args objectAtIndex:kNoteArgName] object:obj userInfo:info];
}
#define EFSMCPostNotification(note, obj, info) do {\
NSArray *args = [[NSArray alloc] initWithObjects:note, obj, info, nil]; \
[[ExtFSMediaController mediaController] performSelectorOnMainThread:@selector(postNotification:) \
withObject:args waitUntilDone:NO]; \
[args release]; \
} while(0)

- (void)updateMountStatus
{
   int count, i;
   struct statfs *stats;
   NSString *device;
   ExtFSMedia *e2media;
   NSMutableArray *pMounts;
   
   count = getmntinfo(&stats, MNT_WAIT);
   if (!count)
      return;
   
   ewlock(e_lock);
   pMounts = [e_pending objectAtIndex:kPendingMounts];
   for (i = 0; i < count; ++i) {
      device = [NSSTR(stats[i].f_mntfromname) lastPathComponent];      
      e2media = [e_media objectForKey:device];
      if (!e2media || [e2media isMounted])
         continue;
      
      [pMounts removeObject:e2media];
      
      [e2media setIsMounted:&stats[i]];
   }
   eulock(e_lock);
}

- (BOOL)volumeDidUnmount:(NSString*)device
{
   ExtFSMedia *e2media;
   BOOL isMounted;
   
   ewlock(e_lock);
   e2media = [e_media objectForKey:device];
   isMounted = [e2media isMounted];
   if (e2media && isMounted) {
      NSMutableArray *pUMounts = [e_pending objectAtIndex:kPendingUMounts];
      [pUMounts removeObject:e2media];
      eulock(e_lock);
      [e2media setIsMounted:nil];
      return (YES);
   }
   eulock(e_lock);
#ifdef DIAGNOSTIC
   if (nil == e2media)
      NSLog(@"ExtFS: Oops! Received unmount for an unknown device: '%@'.\n", device);
   else if (NO == isMounted)
      NSLog(@"ExtFS: Oops! Received unmount for a device that is already unmounted: '%@'.\n", device);
#endif
   return (NO);
}

- (ExtFSMedia*)createMediaWithIOService:(io_service_t)service properties:(NSDictionary*)props
{
   ExtFSMedia *e2media, *parent;
   NSString *device;
   
   e2media = [[ExtFSMedia alloc] initWithIORegProperties:props];
   if (e2media) {
      device = [e2media bsdName];
      ewlock(e_lock);
      [e_media setObject:e2media forKey:device];
      eulock(e_lock);
      
      [e2media updateAttributesFromIOService:service];
      if ((parent = [e2media parent]))
         [parent addChild:e2media];
   #ifdef DIAGNOSTIC
      NSLog(@"ExtFS: Media %@ created with parent %@.\n", device, parent);
   #endif
      
      EFSMCPostNotification(ExtFSMediaNotificationAppeared, e2media, nil);
      [e2media release];
   } else {
      EFSMCPostNotification(ExtFSMediaNotificationCreationFailed,
        [props objectForKey:NSSTR(kIOBSDNameKey)], nil);
   }
   return (e2media);
}

- (void)removeMedia:(ExtFSMedia*)e2media device:(NSString *)device
{
   ExtFSMedia *parent;
   
   if ([e2media isMounted])
      NSLog(@"ExtFS: Oops! Media '%@' removed while still mounted!\n", device);
   
   if ((parent = [e2media parent]))
      [parent remChild:e2media];

   [e2media retain];
   ewlock(e_lock);
   [e_media removeObjectForKey:device];
   eulock(e_lock);
   [self removePending:e2media];

   EFSMCPostNotification(ExtFSMediaNotificationDisappeared, e2media, nil);

#ifdef DIAGNOSTIC
   NSLog(@"ExtFS: Media '%@' removed. Retain count = %u.\n",
      device, [e2media retainCount]);
#endif
   [e2media release];
}

- (int)updateMedia:(io_iterator_t)iter remove:(BOOL)remove
{
   io_service_t iomedia;
   kern_return_t kr = 0;
   ExtFSMedia *e2media;
   CFMutableDictionaryRef properties;
   NSString *device;
   
   /* Init all media devices */
   while ((iomedia = IOIteratorNext(iter))) {
      kr = IORegistryEntryCreateCFProperties(iomedia, &properties,
         kCFAllocatorDefault, 0);
      if (0 == kr) {
         device = [(NSMutableDictionary*)properties objectForKey:NSSTR(kIOBSDNameKey)];
         erlock(e_lock);
         e2media = [e_media objectForKey:device];
         eulock(e_lock);
         if (e2media) {
            if (remove)
               [self removeMedia:e2media device:device];
         #ifdef DIAGNOSTIC
            else {
               NSLog(@"ExtFS: Existing media %@ appeared again.\n", device);
            }
         #endif
            CFRelease(properties);
            continue;
         }
         
         (void)[self createMediaWithIOService:iomedia properties:(NSDictionary*)properties];
         
         CFRelease(properties);
      }
      IOObjectRelease(iomedia);
   }
   
   [self updateMountStatus];
   
   return (0);
}

- (void)mountError:(NSTimer*)timer
{
    NSMutableArray *pMounts;
    ExtFSMedia *media = [timer userInfo];
    
    erlock(e_lock);
    pMounts = [e_pending objectAtIndex:kPendingMounts];
    if ([pMounts containsObject:media]) {
        eulock(e_lock);
        // Send an error
        DiskArbCallback_CallFailedNotification(BSDNAMESTR(media),
            /* XXX - 0x1FFFFFFF will cause an 'Unknown error' since
                the value overflows the error table. */
            EXT_DISK_ARB_MOUNT_FAILURE, 0x1FFFFFFF);
        return;
    }
    eulock(e_lock);
}

- (void)removePending:(ExtFSMedia*)media
{
    NSMutableArray *pMounts;
    NSMutableArray *pUMounts;
    
    ewlock(e_lock);
    pMounts = [e_pending objectAtIndex:kPendingMounts];
    pUMounts = [e_pending objectAtIndex:kPendingUMounts];
   
    [pMounts removeObject:media];
    [pUMounts removeObject:media];
    eulock(e_lock);
}

/* Public */

+ (ExtFSMediaController*)mediaController
{
   if (!e_instance) {
      (void)[[ExtFSMediaController alloc] init];
   }
   
   return (e_instance);
} 

- (unsigned)mediaCount
{
   unsigned ct;
   erlock(e_lock);
   ct = [e_media count];
   eulock(e_lock);
   return (ct);
}

- (NSArray*)media
{
   NSArray *m;
   erlock(e_lock);
   m = [e_media allValues];
   eulock(e_lock);
   return (m);
}

- (NSArray*)rootMedia
{
   NSMutableArray *array;
   NSEnumerator *en;
   ExtFSMedia *e2media;
   
   array = [NSMutableArray array];
   erlock(e_lock);
   en = [e_media objectEnumerator];
   while ((e2media = [en nextObject])) {
      if (nil == [e2media parent]) {
         [array addObject:e2media];
      }
   }
   eulock(e_lock);
   
   return (array);
}

- (NSArray*)mediaWithFSType:(ExtFSType)fstype
{
   NSMutableArray *array;
   NSEnumerator *en;
   ExtFSMedia *e2media;
   
   array = [NSMutableArray array];
   erlock(e_lock);
   en = [e_media objectEnumerator];
   while ((e2media = [en nextObject])) {
      if (fstype == [e2media fsType]) {
         [array addObject:e2media];
      }
   }
   eulock(e_lock);
   
   return (array);
}

- (ExtFSMedia*)mediaWithBSDName:(NSString*)device
{
   ExtFSMedia *m;
   erlock(e_lock);
   m = [e_media objectForKey:device];
   eulock(e_lock);
   return ([[m retain] autorelease]);
}

- (int)mount:(ExtFSMedia*)media on:(NSString*)dir
{
   NSMutableArray *pMounts;
   NSMutableArray *pUMounts;
   kern_return_t ke;
   
   ewlock(e_lock);
   pMounts = [e_pending objectAtIndex:kPendingMounts];
   pUMounts = [e_pending objectAtIndex:kPendingUMounts];
   if ([pMounts containsObject:media] || [pUMounts containsObject:media]) {
        eulock(e_lock);
    #ifdef DIAGNOSTIC
        NSLog(@"ExtFS: Can't mount '%@'. Operation is already in progress.",
            [media bsdName]);
    #endif
        return (EINPROGRESS);
   }
   
   [pMounts addObject:media];// Add it while we are under the lock.
   eulock(e_lock);
   
   ke = DiskArbRequestMount_auto(BSDNAMESTR(media));
   if (0 == ke) {
        /* XXX -- This is a hack to detect a failed mount. It
           seems DA does notify clients of failed mounts. (Why???)
         */
        (void)[NSTimer scheduledTimerWithTimeInterval:EXT_MOUNT_ERROR_DELAY
            target:self selector:@selector(mountError:) userInfo:media
            repeats:NO];
   } else {
      // Re-acquire lock and remove it from pending
      ewlock(e_lock);
      if ([pMounts containsObject:media])
         [pMounts removeObject:media];
      eulock(e_lock);
   }
   return (ke);
}

- (int)unmount:(ExtFSMedia*)media force:(BOOL)force eject:(BOOL)eject
{
   int flags = kDiskArbUnmountOneFlag;
   NSMutableArray *pMounts;
   NSMutableArray *pUMounts;
   kern_return_t ke;
   
   erlock(e_lock);
   pMounts = [e_pending objectAtIndex:kPendingMounts];
   pUMounts = [e_pending objectAtIndex:kPendingUMounts];
   if ([pMounts containsObject:media] || [pUMounts containsObject:media]) {
        eulock(e_lock);
    #ifdef DIAGNOSTIC
        NSLog(@"ExtFS: Can't unmount '%@'. Operation is already in progress.",
            [media bsdName]);
    #endif
        return (EINPROGRESS);
   }
   eulock(e_lock);
   
   if (force)
      flags |= kDiskArbForceUnmountFlag;
   
   if ([media isEjectable] && eject) {
      flags |= kDiskArbUnmountAndEjectFlag;
      flags &= ~kDiskArbUnmountOneFlag;
   }
   
   ke = EINVAL;
   if ([media isMounted] || (nil != [media children]))
      ke = DiskArbUnmountRequest_async_auto(BSDNAMESTR(media), flags);
   else if (flags & kDiskArbUnmountAndEjectFlag)
      ke = DiskArbEjectRequest_async_auto(BSDNAMESTR(media), 0);
   if (0 == ke) {
      ewlock(e_lock);
      if (NO == [pMounts containsObject:media])
        [pUMounts addObject:media];
      eulock(e_lock);
   }
   return (ke);
}

/* Super */

- (id)init
{
   kern_return_t kr = 0;
   CFRunLoopSourceRef rlSource;
   id obj;
   int i;
   
   pthread_mutex_lock(&e_initMutex);
   
   if (e_instance) {
      pthread_mutex_unlock(&e_initMutex);
      [super release];
      return (e_instance);
   }
   
   if (!(self = [super init])) {
      pthread_mutex_unlock(&e_initMutex);
      return (nil);
   }
      
   if (0 != eilock(&e_lock)) {
      pthread_mutex_unlock(&e_initMutex);
#ifdef DIAGNOSTIC
      NSLog(@"ExtFS: Failed to allocate lock for media controller!\n");
#endif
      [super release];
      return (nil);
   }
   
   e_instance = self;
   
   e_pending = [[NSMutableArray alloc] initWithCapacity:kPendingCount];
   for (i=0; i < kPendingCount; ++i) {
        obj = [[NSMutableArray alloc] init];
        [e_pending addObject:obj];
        [obj release];
   }
   
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
   
   e_media = [[NSMutableDictionary alloc] init];
   /* Process the initial registry entrires */
   iomatch_add_callback(nil, notify_add_iter);
   iomatch_rem_callback(nil, notify_rem_iter);
   
   /* Init Disk Arb */
   kr = DiskArbInit();
   DiskArbAddCallbackHandler(kDA_DISK_UNMOUNT_POST_NOTIFY,
      (void *)&DiskArbCallback_UnmountPostNotification, 0);
   DiskArbAddCallbackHandler(kDA_DISK_APPEARED_WITH_MT,
      (void *)&DiskArbCallback_DiskAppearedWithMountpoint, 0);
   DiskArbAddCallbackHandler(kDA_CALL_FAILED,
      (void *)&DiskArbCallback_CallFailedNotification, 0);
   DiskArbUpdateClientFlags();
   
   e_instanceLock = e_lock;
   pthread_mutex_unlock(&e_initMutex);
   return (self);

exit:
   e_instance = nil;
   if (e_lock) edlock(e_lock);
   [e_media release];
   [e_pending release];
   if (notify_add_iter) IOObjectRelease(notify_add_iter);
   if (notify_rem_iter) IOObjectRelease(notify_rem_iter);
   if (notify_port_ref) IONotificationPortDestroy(notify_port_ref);
   pthread_mutex_unlock(&e_initMutex);
   [super release];
   return (nil);
}

/* This singleton lives forever. */
- (void)dealloc
{
   NSLog(@"ExtFS: Oops! Somebody released the global media controller!\n");
#if 0
   DiskArbRemoveCallbackHandler(kDA_DISK_UNMOUNT_POST_NOTIFY,
      (void *)&DiskArbCallback_UnmountPostNotification);
   [e_media release];
   [e_pending release];
   pthread_mutex_lock(&e_initMutex);
   e_instanceLock = nil;
   pthread_mutex_unlock(&e_initMutex);
   edlock(e_lock);
   [super dealloc];
#endif
}

@end

#define MAX_PARENT_ITERS 10
@implementation ExtFSMedia (ExtFSMediaController)

- (void)updateAttributesFromIOService:(io_service_t)service
{
   ExtFSMediaController *mc;
   NSString *regName = nil;
   ExtFSMedia *parent = nil;
   CFTypeRef iconDesc;
   io_name_t ioname;
   io_service_t ioparent, ioparentold;
#ifdef notyet
   io_iterator_t piter;
#endif
   int iterations;
   kern_return_t kr;
   BOOL dvd, cd, wholeDisk;
   
   mc = [ExtFSMediaController mediaController];
   
   erlock(e_lock);
   wholeDisk = (e_attributeFlags & kfsWholeDisk);
   eulock(e_lock);
   
   /* Get IOKit name */
   if (0 == IORegistryEntryGetNameInPlane(service, kIOServicePlane, ioname))
      regName = NSSTR(ioname);
   
   /* Get Parent */
#ifdef notyet
   /* This seems to only return the first parent, don't know what's going on */
   if (0 == IORegistryEntryGetParentIterator(service, kIOServicePlane, &piter)) {
      while ((ioparent = IOIteratorNext(piter))) {
#endif
   if (NO == wholeDisk) {
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
            NSString *pdevice;
            
            /* See if the parent object exists */
            pdevice = [(NSDictionary*)props objectForKey:NSSTR(kIOBSDNameKey)];
            parent = [mc mediaWithBSDName:pdevice];
            if (!parent) {
               /* Parent does not exist */
               parent = [mc createMediaWithIOService:ioparent
                  properties:(NSDictionary*)props];
            }
            CFRelease(props);
         }
         
         IOObjectRelease(ioparent);
      }
   }
   
   /* Find the icon description */
   if (!parent || !(iconDesc = [parent iconDescription]))
      iconDesc = IORegistryEntrySearchCFProperty(service, kIOServicePlane,
         CFSTR(kIOMediaIconKey), kCFAllocatorDefault,
         kIORegistryIterateParents | kIORegistryIterateRecursively);
   
   dvd = IOObjectConformsTo(service, kIODVDMediaClass);
   cd = IOObjectConformsTo(service, kIOCDMediaClass);
   
   ewlock(e_lock);
   if (regName && nil == e_ioregName)
      e_ioregName = [regName retain];
   if (parent && nil == e_parent)
      e_parent = [parent retain];
   if (iconDesc) {
      [e_iconDesc release];
      e_iconDesc = [(NSDictionary*)iconDesc retain];
   }
   if (dvd)
      e_attributeFlags |= kfsDVDROM;
   if (cd)
      e_attributeFlags |= kfsCDROM;
   eulock(e_lock);
}

- (void)setIsMounted:(struct statfs*)stat
{
   NSDictionary *fsTypes;
   NSNumber *fstype;
   BOOL hasJournal, isJournaled;
   
   if (!stat) {
      ewlock(e_lock);
      e_attributeFlags &= ~(kfsMounted|kfsPermsEnabled);
      e_fsBlockSize = 0;
      e_blockCount = 0;
      e_blockAvail = 0;
      e_lastFSUpdate = 0;
      //e_fsType = fsTypeUnknown;
      [e_where release]; e_where = nil;
      [e_volName release]; e_volName = nil;
      
      /* Reset the write flag in case the device is writable,
         but the mounted filesystem was not */
      if ([[e_media objectForKey:NSSTR(kIOMediaWritableKey)] boolValue])
         e_attributeFlags |= kfsWritable;
      eulock(e_lock);
      
      EFSMCPostNotification(ExtFSMediaNotificationUnmounted, self, nil);
      return;
   }
   
   fsTypes = [[NSDictionary alloc] initWithObjectsAndKeys:
      [NSNumber numberWithInt:fsTypeExt2], NSSTR(EXT2FS_NAME),
      [NSNumber numberWithInt:fsTypeExt3], NSSTR(EXT3FS_NAME),
      [NSNumber numberWithInt:fsTypeHFS], NSSTR(HFS_NAME),
      [NSNumber numberWithInt:fsTypeUFS], NSSTR(UFS_NAME),
      [NSNumber numberWithInt:fsTypeCD9660], NSSTR(CD9660_NAME),
      [NSNumber numberWithInt:fsTypeCDAudio], NSSTR(CDAUDIO_NAME),
      [NSNumber numberWithInt:fsTypeUDF], NSSTR(UDF_NAME),
      [NSNumber numberWithInt:fsTypeMSDOS], NSSTR(MSDOS_NAME),
      [NSNumber numberWithInt:fsTypeNTFS], NSSTR(NTFS_NAME),
      [NSNumber numberWithInt:fsTypeUnknown], NSSTR("Unknown"),
      nil];
   
   fstype = [fsTypes objectForKey:NSSTR(stat->f_fstypename)];
   ewlock(e_lock);
   if (fstype)
      e_fsType = [fstype intValue];
   else
      e_fsType = fsTypeUnknown;
   
   [fsTypes release];
   
   e_attributeFlags |= kfsMounted;
   if (stat->f_flags & MNT_RDONLY)
      e_attributeFlags &= ~kfsWritable;
   
   if (0 == (stat->f_flags & MNT_UNKNOWNPERMISSIONS))
      e_attributeFlags |= kfsPermsEnabled;
   
   e_fsBlockSize = stat->f_bsize;
   e_blockCount = stat->f_blocks;
   e_blockAvail = stat->f_bavail;
   [e_where release];
   e_where = [[NSString alloc] initWithCString:stat->f_mntonname];
   eulock(e_lock);
   
   (void)[self fsInfo];
   hasJournal = [self hasJournal];
   isJournaled = [self isJournaled];
   
   ewlock(e_lock);
   if (fsTypeExt2 == e_fsType && hasJournal)
      e_fsType = fsTypeExt3;
   
   if (fsTypeHFSPlus == e_fsType) {
      if (isJournaled) {
         e_fsType = fsTypeHFSJ;
      }
   }
   eulock(e_lock);
   
   EFSMCPostNotification(ExtFSMediaNotificationMounted, self, nil);
}

- (NSDictionary*)iconDescription
{
   return ([[e_iconDesc retain] autorelease]);
}

@end

/* Helpers */

// e_fsNames is read-only, so there is no need for a lock
const char* EFSNameFromType(int type)
{
   if (type > fsTypeUnknown)
      type = fsTypeUnknown;
   return (e_fsNames[(type)]);
}

NSString* EFSNSNameFromType(int type)
{
   if (type > fsTypeUnknown)
      type = fsTypeUnknown;
   return ([NSString stringWithCString:e_fsNames[(type)]]);
}

static NSDictionary *e_fsPrettyNames = nil;
static pthread_mutex_t e_fsPrettyMutex = PTHREAD_MUTEX_INITIALIZER;

NSString* EFSNSPrettyNameFromType(int type)
{
    if (nil == e_fsPrettyNames) {
        NSBundle *me;
        pthread_mutex_lock(&e_fsPrettyMutex);
        // Make sure the global is still nil once we hold the lock
        if (nil != e_fsPrettyNames) {
            pthread_mutex_unlock(&e_fsPrettyMutex);
            goto fspretty_lookup;
        }
        
        me = [NSBundle bundleWithIdentifier:EXTFS_DM_BNDL_ID];
        if (nil == me) {
            pthread_mutex_unlock(&e_fsPrettyMutex);
            NSLog(@"ExtFS: Could not find bundle!\n");
            return (nil);
        }

        /* The correct way to get these names is to enum the FS bundles and
          get the FSName value for each personality. This turns out to be more
          work than it's worth though. */
        e_fsPrettyNames = [[NSDictionary alloc] initWithObjectsAndKeys:
            @"Linux Ext2", [NSNumber numberWithInt:fsTypeExt2],
            [me localizedStringForKey:@"Linux Ext3 (Journaled)" value:nil table:nil],
                [NSNumber numberWithInt:fsTypeExt3],
            @"Mac OS Standard", [NSNumber numberWithInt:fsTypeHFS],
            [me localizedStringForKey:@"Mac OS Extended" value:nil table:nil],
                [NSNumber numberWithInt:fsTypeHFSPlus],
            [me localizedStringForKey:@"Mac OS Extended (Journaled)" value:nil table:nil],
                [NSNumber numberWithInt:fsTypeHFSJ],
            [me localizedStringForKey:@"Mac OS Extended (Journaled/Case Sensitive)" value:nil table:nil],
                [NSNumber numberWithInt:fsTypeHFSX],
            @"UNIX", [NSNumber numberWithInt:fsTypeUFS],
            @"ISO 9660", [NSNumber numberWithInt:fsTypeCD9660],
            [me localizedStringForKey:@"CD Audio" value:nil table:nil],
                [NSNumber numberWithInt:fsTypeCDAudio],
            @"Universal Disk Format (UDF)", [NSNumber numberWithInt:fsTypeUDF],
            @"MSDOS (FAT)", [NSNumber numberWithInt:fsTypeMSDOS],
            @"Windows NTFS", [NSNumber numberWithInt:fsTypeNTFS],
            [me localizedStringForKey:@"Unknown" value:nil table:nil],
                [NSNumber numberWithInt:fsTypeUnknown],
          nil];
        pthread_mutex_unlock(&e_fsPrettyMutex);
    }

fspretty_lookup:
    // Once allocated, the name table is considered read-only
    if (type > fsTypeUnknown)
      type = fsTypeUnknown;
    return ([e_fsPrettyNames objectForKey:[NSNumber numberWithInt:type]]);
}

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
    if (0 == errorCode)
        (void)[[ExtFSMediaController mediaController] volumeDidUnmount:NSSTR(device)];
#ifdef DIAGNOSTIC
    else
         NSLog(@"ExtFS: Device '%s' failed unmount with error: %d.\n", device, errorCode);
#endif
}

NSString * const ExtMediaKeyOpFailureType = @"ExtMediaKeyOpFailureType";
NSString * const ExtMediaKeyOpFailureDevice = @"ExtMediaKeyOpFailureBSDName";
NSString * const ExtMediaKeyOpFailureError = @"ExtMediaKeyOpFailureError";
NSString * const ExtMediaKeyOpFailureErrorString = @"ExtMediaKeyOpFailureErrorString";
NSString * const ExtMediaKeyOpFailureMsgString = @"ExtMediaKeyOpFailureMsgString";

// Error codes are defined in DiskArbitration/DADissenter.h
static NSString * const e_DiskArbErrorTable[] = {
   @"", // Empty
   @"Unknown Error", // kDAReturnError
   @"Device is busy", // kDAReturnBusy
   @"Bad argument", // kDAReturnBadArgument
   @"Device is locked", // kDAReturnExclusiveAccess
   @"Resource shortage", // kDAReturnNoResources
   @"Device not found", // kDAReturnNotFound
   @"Device not mounted", // kDAReturnNotMounted
   @"Operation not permitted", // kDAReturnNotPermitted
   @"Not authorized", // kDAReturnNotPrivileged
   @"Device not ready", // kDAReturnNotReady
   @"Device is read only", // kDAReturnNotWritable
   @"Unsupported request", // kDAReturnUnsupported
   nil
};
#define DA_ERR_TABLE_LAST 0x0C

static void DiskArbCallback_CallFailedNotification(DiskArbDiskIdentifier device,
   int type, int status)
{
   NSString *bsd = NSSTR(device), *err, *op, *msg;
   ExtFSMedia *emedia;
   NSBundle *me;
   ExtFSMediaController *mc = [ExtFSMediaController mediaController];
   
   if ((emedia = [mc mediaWithBSDName:bsd])) {
      short idx = (status & 0xFF);
      NSDictionary *dict;
      
      [mc removePending:emedia];
      
      if (idx > DA_ERR_TABLE_LAST) {
         if (EBUSY == idx) // This can be returned instead of kDAReturnBusy
            idx = 2;
         else
            idx = 1;
      }
      // Table is read-only, no need to protect it.
      err = e_DiskArbErrorTable[idx];
      
      msg = nil;
      switch (type) {
        /* XXX - No mount errors from Disk Arb??? */
        case EXT_DISK_ARB_MOUNT_FAILURE:
            op = @"Mount";
            msg = @"The filesystem may need repair. Please use Disk Utility to check the filesystem.";
            break;
        case kDiskArbUnmountAndEjectRequestFailed:
        case kDiskArbUnmountRequestFailed:
            op = @"Unmount";
            break;
        case kDiskArbEjectRequestFailed:
            op = @"Eject";
            break;
        case kDiskArbDiskChangeRequestFailed:
            op = @"Change Request";
            break;
        default:
            op = @"Unknown";
            break;
      }
      
      // Get a ref to ourself so we can load our localized strings.
      erlock(e_instanceLock);
      me = [NSBundle bundleWithIdentifier:EXTFS_DM_BNDL_ID];
      if (me) {
        NSString *errl, *opl, *msgl;
        
        errl = [me localizedStringForKey:err value:nil table:nil];
        if (errl)
            err = errl;
        
        opl = [me localizedStringForKey:op value:nil table:nil];
        if (opl)
            op = opl;
        
        msgl = [me localizedStringForKey:msg value:nil table:nil];
        if (msgl)
            msg = msgl;
      }
      eulock(e_instanceLock);
      
      dict = [NSDictionary dictionaryWithObjectsAndKeys:
         op, ExtMediaKeyOpFailureType,
         bsd, ExtMediaKeyOpFailureDevice,
         [NSNumber numberWithInt:status], ExtMediaKeyOpFailureError,
         err, ExtMediaKeyOpFailureErrorString,
         msg, (msg ? ExtMediaKeyOpFailureMsgString : nil),
         nil];

      EFSMCPostNotification(ExtFSMediaNotificationOpFailure, emedia, dict);
      
      NSLog(@"ExtFS: DiskArb failure for device '%s', with type %d and status 0x%X\n",
         device, type, status);
   }
}
