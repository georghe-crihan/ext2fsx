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

#import <string.h>
#import <sys/param.h>
#import <sys/ucred.h>
#import <sys/mount.h>

#import <mach/mach_port.h>
#import <mach/mach_interface.h>
#import <mach/mach_init.h>

#import <IOKit/IOMessage.h>
#import <IOKit/IOBSD.h>
#import <IOKit/storage/IOMedia.h>
#import <IOKit/storage/IOCDMedia.h>
#import <IOKit/storage/IODVDMedia.h>

#import "ExtFSMediaPrivate.h"
#import "ExtFSLock.h"

enum {
    kNoteArgName = 0,
    kNoteArgObject = 1,
    kNoteArgInfo = 2
};

@implementation ExtFSMediaController (Private)

- (void)postNotification:(NSArray*)args
{
    unsigned ct = [args count];
    id obj = (ct >= kNoteArgObject+1) ? [args objectAtIndex:kNoteArgObject] : nil;
    NSDictionary *info = (ct >= kNoteArgInfo+1) ? [args objectAtIndex:kNoteArgInfo] : nil;
    [[NSNotificationCenter defaultCenter]
         postNotificationName:[args objectAtIndex:kNoteArgName] object:obj userInfo:info];
}

- (NSString*)pathForResource:(NSString*)resource
{
    NSString *path = nil;
    
    ewlock(e_lock);
    NSBundle *me = [NSBundle bundleWithIdentifier:EXTFS_DM_BNDL_ID];
    if (me) {
        path = [me pathForResource:resource ofType:nil];
    } else {
        NSLog(@"ExtFS: Could not find bundle!\n");
    }
    eulock(e_lock);
    return (path);
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
   ExtFSIOTransportType transType;
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
      while (kIOReturnSuccess == kr && ioparent && iterations < MAX_PARENT_ITERS) {
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
   
   // Get transport properties
    transType = efsIOTransportTypeUnknown | efsIOTransportTypeInternal;
    if (nil != parent) {
        transType = [parent transportType] | [parent transportBus];
        dvd = [parent isDVDROM];
        cd = [parent isCDROM];
    } else {
        io_name_t parentClass;
        ioparent = nil;
        kr = IORegistryEntryGetParentEntry(service, kIOServicePlane, &ioparent);
        while (kIOReturnSuccess == kr && ioparent &&
             (efsIOTransportTypeUnknown == (transType & efsIOTransportBusMask) ||
             efsIOTransportTypeExternal != (transType & efsIOTransportTypeMask))) {
            
            if (kIOReturnSuccess == IOObjectGetClass(ioparent, parentClass)) {
                if (0 == strncmp(parentClass, "IOATABlockStorageDevice", sizeof(parentClass)))
                    transType = efsIOTransportTypeATA | (transType & efsIOTransportTypeMask);
                else if (0 == strncmp(parentClass, "IOATAPIProtocolTransport", sizeof(parentClass)))
                    transType = efsIOTransportTypeATAPI | (transType & efsIOTransportTypeMask); 
                else if (0 == strncmp(parentClass, "IOFireWireDevice", sizeof(parentClass)))
                    transType = efsIOTransportTypeFirewire | (transType & efsIOTransportTypeMask);
                else if (0 == strncmp(parentClass, "IOUSBMassStorageClass", sizeof(parentClass)))
                    transType = efsIOTransportTypeUSB | (transType & efsIOTransportTypeMask);
                else if (0 == strncmp(parentClass, "IOSCSIBlockCommandsDevice", sizeof(parentClass) /*"IOSCSIProtocolServices"*/))
                    transType = efsIOTransportTypeSCSI | (transType & efsIOTransportTypeMask);
                else if (0 == strncmp(parentClass, "KDIDiskImageNub", sizeof(parentClass)) ||
                     0 == strncmp(parentClass, "IOHDIXController", sizeof(parentClass)) ||
                     0 == strncmp(parentClass, "PGPdiskController", sizeof(parentClass))) {
                    transType = efsIOTransportTypeImage | efsIOTransportTypeVirtual;
                    break;
                }

                if (0 == strncmp(parentClass, "IOSCSIPeripheralDeviceNub", sizeof(parentClass))) {
                    transType |= efsIOTransportTypeExternal;
                    transType &= ~efsIOTransportTypeInternal;
                }
            }
            ioparentold = ioparent;
            ioparent = nil;
            kr = IORegistryEntryGetParentEntry(ioparentold, kIOServicePlane, &ioparent);
            IOObjectRelease(ioparentold);
        }
        if (ioparent)
            IOObjectRelease(ioparent);
        dvd = IOObjectConformsTo(service, kIODVDMediaClass);
        cd = IOObjectConformsTo(service, kIOCDMediaClass);
    }
   
   /* Find the icon description */
   if (!parent || !(iconDesc = [parent iconDescription]))
      iconDesc = IORegistryEntrySearchCFProperty(service, kIOServicePlane,
         CFSTR(kIOMediaIconKey), kCFAllocatorDefault,
         kIORegistryIterateParents | kIORegistryIterateRecursively);
   
   ewlock(e_lock);
   e_ioTransport = transType;
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
   NSString *tmp;
   ExtFSType ftype;
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
   ftype = fsTypeUnknown;
   if (fstype)
      ftype = [fstype intValue];
   else
      NSLog(@"ExtFS: Unknown filesystem '%@'.\n", fstype);
   [fsTypes release];
   tmp = [[NSString alloc] initWithCString:stat->f_mntonname];
   
   ewlock(e_lock);
   e_fsType = ftype;
   
   e_attributeFlags |= kfsMounted;
   // CD-Audio needs the following
   e_attributeFlags &= ~kfsNoMount;
   if (stat->f_flags & MNT_RDONLY)
      e_attributeFlags &= ~kfsWritable;
   
   if (0 == (stat->f_flags & MNT_UNKNOWNPERMISSIONS))
      e_attributeFlags |= kfsPermsEnabled;
   
   e_fsBlockSize = stat->f_bsize;
   e_blockCount = stat->f_blocks;
   e_blockAvail = stat->f_bavail;
   [e_where release];
   e_where = tmp;
   eulock(e_lock);
   
   tmp = nil;
   
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

- (io_service_t)copyIOService
{
    const char *device = BSDNAMESTR(self);
    CFMutableDictionaryRef dict;
    
    dict = IOBSDNameMatching(kIOMasterPortDefault, 0, device);
    if (dict)
        return (IOServiceGetMatchingService(kIOMasterPortDefault, dict));
    
    return (nil);
}

- (io_service_t)copyATAIOService
{
    io_service_t me = nil, tmp, parent = nil;
    
    if (efsIOTransportTypeATA == [self transportBus])
        me = [self copyIOService];
    if (me) {
        kern_return_t kr;
        io_name_t parentClass;
        kr = IORegistryEntryGetParentEntry(me, kIOServicePlane, &parent);
        while (kIOReturnSuccess == kr && parent) {
            if (kIOReturnSuccess == IOObjectGetClass(parent, parentClass)) {
                if (0 == strncmp(parentClass, "IOATABlockStorageDevice", sizeof(parentClass)))
                    break;
            }
            tmp = parent;
            parent = nil;
            kr = IORegistryEntryGetParentEntry(tmp, kIOServicePlane, &parent);
            IOObjectRelease(tmp);
        }
        IOObjectRelease(me);
    }
    
    return (parent);
}

@end
