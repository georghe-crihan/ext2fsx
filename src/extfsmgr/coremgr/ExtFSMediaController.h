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

#import <Foundation/Foundation.h>

@class ExtFSMedia;

/*!
@class ExtFSMediaController
@abstract Handles interaction with Disk Arbitration and IOKit
to determine available media.
@discussion This is the central management point for all media.
It queries the system for all media, and determines their properties and mount status.
There should only be one instance of this class.
*/
@interface ExtFSMediaController : NSObject
{
@private
   id _media;
}

/*!
@method mediaController
@abstract Class method to access the global controller instance.
@result Global instance.
*/
+ (ExtFSMediaController*)mediaController;

/*!
@method mediaCount
@abstract Determinet the number of media objects.
@discussion One media object is created for each
disk and each partition on a disk.
@result Count of media objects.
*/
- (unsigned)mediaCount;
/*!
@method media
@abstract Access all media objects.
@result An array of media objects.
*/
- (NSArray*)media;
/*!
@method rootMedia
@abstract Access all media that is not a child of some other media.
@result An array of media objects.
*/
- (NSArray*)rootMedia;
/*!
@method mediaWithFSType
@abstract Access all media with a specific filesystem type.
@discussion Only media that is mounted will be included.
@param fstype Filesystem type to match.
@result An array of media objects, or nil if no matches were found.
*/
- (NSArray*)mediaWithFSType:(ExtFSType)fstype;
/*!
@method mediaWithBSDName
@abstract Search for a media object by it's BSD kernel name.
@param device A string containing the device name to search for
(excluding any paths).
@result A media object matching the device name, or nil if a match was not found.
*/
- (ExtFSMedia*)mediaWithBSDName:(NSString*)device;

/*!
@method mount:on:
@abstract Mount media on specified directory.
@param media Media to mount.
@param on A string containing the absolute path to the directory the media should be mounted on.
@result 0 if successful or an error code (possibly from Disk Arbitration).
*/
- (int)mount:(ExtFSMedia*)media on:(NSString*)dir;
/*!
@method unmount:force:eject:
@abstract Unmount currently mounted media.
@discussion It is possible for this method to return success,
before the unmount has completed. If the unmount fails later, a
ExtFSMediaNotificationOpFailure notification will be sent.
@param media Media to unmount.
@param force Force the unmount even if the filesystem is in use?
@param eject Eject the media if possible?
@result 0 if successful or an error code (possibly from Disk Arbitration).
*/
- (int)unmount:(ExtFSMedia*)media force:(BOOL)force eject:(BOOL)eject;

@end

/*!
@const ExtFSMediaNotificationAppeared
@abstract This notification is sent to the default Notification center
when new media appears. The new media object is attached.
*/
extern NSString *ExtFSMediaNotificationAppeared;
/*!
@const ExtFSMediaNotificationDisappeared
@abstract This notification is sent to the default Notification center
when new media disappears (ie it's ejected). The vanishing 
media object is attached.
*/
extern NSString *ExtFSMediaNotificationDisappeared;
/*!
@const ExtFSMediaNotificationMounted
@abstract This notification is sent to the default Notification center
when a device has been mounted (either by explicit request of
the application or by some other means). The mounted media object
is attached.
*/
extern NSString *ExtFSMediaNotificationMounted;
/*!
@const ExtFSMediaNotificationUnmounted
@abstract This notification is sent to the default Notification center
when a device has been unmounted (either by explicit request of
the application or by some other means). The unmounted media object
is attached.
*/
extern NSString *ExtFSMediaNotificationUnmounted;
/*!
@const ExtFSMediaNotificationOpFailure
@abstract This notification is sent to the default Notification center
when an asynchronous operation fails. The media object that failed is
attached and the user info dictionary contains the error information.
*/
extern NSString *ExtFSMediaNotificationOpFailure;

/*!
@const ExtFSMediaNotificationCreationFailed
@abstract This notification is sent to the default Notification center
when ExtFSMediaController fails to create a media object for a device.
A string containing the device name is attached.
*/
extern NSString *ExtFSMediaNotificationCreationFailed;

/*!
@const ExtMediaKeyOpFailureType
@abstract Key for ExtFSMediaNotificationOpFailure to determine
the type of failure (as an NSString -- Unknown, Unmount, Eject, etc).
*/
extern NSString *ExtMediaKeyOpFailureType;
/*!
@const ExtMediaKeyOpFailureError
@abstract Key for ExtFSMediaNotificationOpFailure to determine
the error code (as an NSNumber).
*/
extern NSString *ExtMediaKeyOpFailureError;
/*!
@const ExtMediaKeyOpFailureErrorString
@abstract Key for ExtFSMediaNotificationOpFailure to access
a human-readable description of the error suitable for display
to a user.
*/
extern NSString *ExtMediaKeyOpFailureErrorString;

/*!
@function FSNameFromType
@abstract Converts a filesystem type id to a C string
containing the filesystem name.
@param type A valid ExtFSType id.
@result Filesystem name or nil, if the type is invalid.
*/
char* FSNameFromType(int type);
/*!
@function NSFSNameFromType
@abstract Converts a filesystem type id to a NSString
containing the filesystem name.
@param type A valid ExtFSType id.
@result Filesystem name or nil, if the type is invalid.
*/
NSString* NSFSNameFromType(int type);
