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

#import <Foundation/Foundation.h>

@class ExtFSMedia;

@interface ExtFSMediaController : NSObject
{
@private
   id _media;
}

+ (ExtFSMediaController*)mediaController;

- (unsigned)mediaCount;
- (NSArray*)media; /* All media */
- (NSArray*)rootMedia; /* Gets all media that is not a child of some other media */
- (NSArray*)mediaWithFSType:(ExtFSType)fstype;
- (ExtFSMedia*)mediaWithBSDName:(NSString*)device;

- (int)mount:(ExtFSMedia*)media on:(NSString*)dir;
- (int)unmount:(ExtFSMedia*)media force:(BOOL)force eject:(BOOL)eject;

@end

/* ExtFSMedia object */
extern NSString *ExtFSMediaNotificationAppeared;
extern NSString *ExtFSMediaNotificationDisappeared;
extern NSString *ExtFSMediaNotificationMounted;
extern NSString *ExtFSMediaNotificationUnmounted;
extern NSString *ExtFSMediaNotificationOpFailure; // User info == NSDictionary
/* NSString object containing device name */
extern NSString *ExtFSMediaNotificationCreationFailed;

// Keys for OpFailure notification
extern NSString *ExtMediaKeyOpFailureType; // NSString
extern NSString *ExtMediaKeyOpFailureError; // NSNumber
extern NSString *ExtMediaKeyOpFailureErrorString; // NSString suitable for display to user

char* FSNameFromType(int type);
NSString* NSFSNameFromType(int type);

