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

#import <stdio.h>

#import "ExtFSMedia.h"
#import "ExtFSMediaController.h"

@interface ListDisks : NSObject
{
   ExtFSMediaController *_mc;
}
- (void)list:(ExtFSMediaController*)mc;
- (void)mediaAdd:(NSNotification*)notification;
- (void)mediaRemove:(NSNotification*)notification;
- (void)volMount:(NSNotification*)notification;
- (void)volUnmount:(NSNotification*)notification;
@end

@implementation ListDisks

- (void)list:(ExtFSMediaController*)mc
{
   NSArray *disks;
   NSEnumerator *en;
   ExtFSMedia *obj, *parent;
   NSString *tmp;
   BOOL mounted;
   
   if (!_mc) _mc = mc;
   mc = _mc;
   
   disks = [mc media];
   en = [disks objectEnumerator];
   while ((obj = [en nextObject])) {
      mounted = [obj isMounted];
      printf ("Device: %s\n", [[obj bsdName] cString]);
      tmp = [obj ioRegistryName];
      if (tmp)
         printf ("\tIOKit Name: %s\n", [tmp cString]);
      parent = [obj parent];
      if (parent)
         printf ("\tParent Device: %s\n", [[parent bsdName] cString]);
      printf ("\tEjectable: %s\n", ([obj isEjectable] ? "Yes" : "No"));
      printf ("\tDVD/CD ROM: %s\n", (([obj isDVDROM] || [obj isCDROM]) ? "Yes" : "No"));
      printf ("\tFS Type: %s\n", FSNameFromType([obj fsType]));
      printf ("\tMounted: %s\n", (mounted ? [[obj mountPoint] cString] : "Not mounted"));
      if (mounted) {
         tmp = [obj volName];
         if (tmp)
            printf ("\tVolume Name: %s\n", [tmp cString]);
         tmp = [obj uuidString];
         if (tmp)
            printf ("\tVolume UUID: %s\n", [tmp cString]);
         printf ("\tSupports Journaling: %s\n", ([obj hasJournal] ? "Yes" : "No"));
         printf ("\tJournaled: %s\n", ([obj isJournaled] ? "Yes" : "No"));
         printf ("\tIndexed Directories: %s\n", ([obj hasIndexedDirs] ? "Yes" : "No"));
         printf ("\tLarge Files: %s\n", ([obj hasLargeFiles] ? "Yes" : "No"));
         printf ("\tBlock size: %lu\n", [obj blockSize]);
         printf ("\tBlock count: %qu\n", [obj blockCount]);
         printf ("\tTotal bytes: %qu\n", [obj size]);
         printf ("\tAvailable bytes: %qu\n", [obj availableSize]);
         printf ("\tNumber of files: %qu\n", [obj fileCount]);
         printf ("\tNumber of directories: %qu\n", [obj dirCount]);
      }
   #ifdef EXT_MGR_GUI
      NSImage *icon = [obj icon];
   #endif
   }
}

- (void)mediaAdd:(NSNotification*)notification
{
   printf ("**** Media '%s' appeared ***\n", BSDNAMESTR([notification object]));
}

- (void)mediaRemove:(NSNotification*)notification
{
   printf ("**** Media '%s' disappeared ***\n", BSDNAMESTR([notification object]));
}

- (void)volMount:(NSNotification*)notification
{
   printf ("**** Media '%s' mounted ***\n", BSDNAMESTR([notification object]));
   [self list:nil];
}

- (void)volUnmount:(NSNotification*)notification
{
   printf ("**** Media '%s' unmounted ***\n", BSDNAMESTR([notification object]));
   [self list:nil];
}

@end

int main(int argc, const char *argv[])
{
   NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
   ExtFSMediaController *mc;
   ListDisks *ld;
   
   mc = [ExtFSMediaController mediaController];
   ld = [[ListDisks alloc] init];
   
#ifdef EXT_MGR_GUI
   [[NSNotificationCenter defaultCenter] addObserver:ld
            selector:@selector(mediaAdd:)
            name:ExtFSMediaNotificationAppeared
            object:nil];
   [[NSNotificationCenter defaultCenter] addObserver:ld
            selector:@selector(mediaRemove:)
            name:ExtFSMediaNotificationDisappeared
            object:nil];
   [[NSNotificationCenter defaultCenter] addObserver:ld
            selector:@selector(volMount:)
            name:ExtFSMediaNotificationMounted
            object:nil];
   [[NSNotificationCenter defaultCenter] addObserver:ld
            selector:@selector(volUnmount:)
            name:ExtFSMediaNotificationUnmounted
            object:nil];
   [ld performSelector:@selector(list:) withObject:mc afterDelay:2.0];
   NSApplicationMain(argc, argv);
#else
   [ld list:mc];
#endif

   [ld release];
   [pool release];
   return (0);
}