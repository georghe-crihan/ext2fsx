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

#import "ExtFSManager.h"
#import "ExtFSMedia.h"
#import "ExtFSMediaController.h"

/* Note: For the "Options" button, the image/title
      is the reverse of the actual action. */

#define EXT_TOOLBAR_ACTION_COUNT 3
#define EXT_TOOLBAR_ACTION_MOUNT @"Mount"
#define EXT_TOOLBAR_ACTION_EJECT @"Eject"
#define EXT_TOOLBAR_ACTION_INFO @"Info"

#define EXT_TOOLBAR_ALT_ACTION_MOUNT @"Unmount"
#define EXT_TOOLBAR_ALT_ACTION_INFO @"Options"

#define EXT_TOOLBAR_ICON_TYPE @"tiff"	

static inline void ExtSwapButtonState(id button, BOOL swapImage)
{
   NSString *title;
   NSImage *image;
   
   title = [[button alternateTitle] retain];
   [button setAlternateTitle:[button title]];
   [button setTitle:title];
   [title release];
   
   if (swapImage) {
      image = [[button alternateImage] retain];
      [button setAlternateImage:[button image]];
      [button setImage:image];
      [image release];
   }
}

static inline id ExtMakeInfoTitle(NSString *title)
{
   NSMutableAttributedString *str;
   str = [[NSMutableAttributedString alloc] initWithString:
      [title stringByAppendingString:@":"]];
   [str applyFontTraits:NSBoldFontMask range:NSMakeRange(0, [str length])];
   return ([str autorelease]);
}

#import "extfsmgr.h"

static NSMutableDictionary *_prefRoot, *_prefGlobal, *_prefMedia, *_prefMgr = nil;
static NSString *_bundleid = nil;
static BOOL _prefsChanged = NO;

@implementation ExtFSManager : NSPreferencePane

/* Private */

- (int)probeDisks
{
   ExtFSMediaController *mc;
   mc = [ExtFSMediaController mediaController];
   
   _volData = [[NSMutableArray alloc] initWithArray:
      [[mc rootMedia] sortedArrayUsingSelector:@selector(compare:)]];
   
   // Preload the icon data (this can take a few seconds if loading from disk)
   [_volData makeObjectsPerformSelector:@selector(icon)];
   
   return (0);
}

- (void)updateMountState:(ExtFSMedia*)media
{
   NSString *tmp;
   
   tmp = [(NSButtonCell*)[_mountButton cell] representedObject];
   if ([media canMount] && NO == [[media mountPoint] isEqualToString:@"/"]) {
      [_mountButton setEnabled:YES];
      if ([media isMounted]) {
         if ([tmp isEqualTo:EXT_TOOLBAR_ACTION_MOUNT]) {
            ExtSwapButtonState(_mountButton, NO);
            [(NSButtonCell*)[_mountButton cell]
               setRepresentedObject:EXT_TOOLBAR_ALT_ACTION_MOUNT];
         }
      } else 
         goto unmount_state;
   } else {
      [_mountButton setEnabled:NO];
unmount_state:
      if ([tmp isEqualTo:EXT_TOOLBAR_ALT_ACTION_MOUNT]) {
         ExtSwapButtonState(_mountButton, NO);
         [(NSButtonCell*)[_mountButton cell]
            setRepresentedObject:EXT_TOOLBAR_ACTION_MOUNT];
      }
   }
}

- (void)mediaAdd:(NSNotification*)notification
{
   ExtFSMedia *media, *parent;
   
   media = [notification object];
#ifdef DIAGNOSTIC
   NSLog(@"ExtFS: **** Media '%s' appeared ***\n", BSDNAMESTR(media));
#endif

   if ((parent = [media parent])) {
      if (![_volData containsObject:parent]) {
         [_volData addObject:parent];
         goto exit;
      } else {
         if ([_vollist isItemExpanded:parent])
            [_vollist reloadItem:parent reloadChildren:YES];
         return;
      }
   } else {
      [_volData addObject:media];
   }

exit:
   [_vollist reloadData];
}

#define ExtSetDefaultState() \
do { \
_curSelection = nil; \
[_mountButton setEnabled:NO]; \
[_ejectButton setEnabled:NO]; \
[_infoButton setEnabled:NO]; \
[_diskIconView setImage:nil]; \
[self doOptions:self]; \
[_tabs selectTabViewItemWithIdentifier:@"Startup"]; \
} while (0)

- (void)mediaRemove:(NSNotification*)notification
{
   ExtFSMedia *media, *parent;
   
   media = [notification object];
   
#ifdef DIAGNOSTIC
   NSLog(@"ExtFS: **** Media '%s' disappeared ***\n", BSDNAMESTR(media));
#endif

   if ((parent = [media parent])) {
      if ([_volData containsObject:parent] && [_vollist isItemExpanded:parent])
         [_vollist reloadItem:parent reloadChildren:YES];
      return;
   } else {
      [_volData removeObject:media];
   }
   
   [_vollist reloadData];
   
   if (-1 == [_vollist selectedRow])
      ExtSetDefaultState();
}

- (void)volMount:(NSNotification*)notification
{
   ExtFSMedia *media;
   
   media = [notification object];
#ifdef DIAGNOSTIC
   NSLog(@"ExtFS: **** Media '%s' mounted ***\n", BSDNAMESTR(media));
#endif
   if (media == _curSelection)
      [self doMediaSelection:media];
   [_vollist reloadItem:media];
}

- (void)volUnmount:(NSNotification*)notification
{
   ExtFSMedia *media;
   
   media = [notification object];
#ifdef DIAGNOSTIC
   NSLog(@"ExtFS: **** Media '%s' unmounted ***\n", BSDNAMESTR([notification object]));
#endif
   if (media == _curSelection)
      [self doMediaSelection:media];
   [_vollist reloadItem:media];
}

- (void)childChanged:(NSNotification*)notification
{

}

- (void)startup
{
   NSString *title;
   ExtFSMedia *media;
   NSEnumerator *en;
   
   [_vollist setEnabled:NO];
   
   [_tabs selectTabViewItemWithIdentifier:@"Startup"];
   title = ExtLocalizedString(@"Please wait, gathering disk informationÉ",
      "Startup Message");
   [_startupText setStringValue:title];
   
   [_startupProgress setStyle:NSProgressIndicatorSpinningStyle];
   [_startupProgress sizeToFit];
   [_startupProgress setDisplayedWhenStopped:NO];
   [_startupProgress setIndeterminate:YES];
   [_startupProgress setUsesThreadedAnimation:YES];
   [_startupProgress startAnimation:self];
   [_startupProgress displayIfNeeded];
   
   
   (void)[self probeDisks];
   
   // Startup complete
   [_vollist reloadData];
   
   // Register for notifications
   [[NSNotificationCenter defaultCenter] addObserver:self
            selector:@selector(mediaAdd:)
            name:ExtFSMediaNotificationAppeared
            object:nil];
   [[NSNotificationCenter defaultCenter] addObserver:self
            selector:@selector(mediaRemove:)
            name:ExtFSMediaNotificationDisappeared
            object:nil];
   [[NSNotificationCenter defaultCenter] addObserver:self
            selector:@selector(volMount:)
            name:ExtFSMediaNotificationMounted
            object:nil];
   [[NSNotificationCenter defaultCenter] addObserver:self
            selector:@selector(volUnmount:)
            name:ExtFSMediaNotificationUnmounted
            object:nil];
   [[NSNotificationCenter defaultCenter] addObserver:self
            selector:@selector(childChanged:)
            name:ExtFSMediaNotificationChildChange
            object:nil];
   
   // Exapnd the root items
   en = [_volData objectEnumerator];
   while ((media = [en nextObject])) {
      [_vollist expandItem:media];
   }
   
   [_startupProgress stopAnimation:self];
   [_startupProgress displayIfNeeded];
   title = ExtLocalizedString(@"Please select a disk or volume",
      "Startup Message");
   [_startupText setStringValue:title];
   
   [_vollist setEnabled:NO];
}

#define ExtInfoInsert() \
do { \
data = [data stringByAppendingString:@"\n"]; \
[line appendAttributedString: \
   [[[NSAttributedString alloc] initWithString:data] autorelease]]; \
[_infoText insertText:line]; \
} while(0)

- (void)generateInfo:(ExtFSMedia*)media
{
   NSMutableAttributedString *line;
   NSString *data, *yes, *no, *bytes;
   NSString *monikers[] =
      {nil, @"KB", @"MB", @"GB", @"TB", @"PB", @"EB", @"ZB", nil};
   double size;
   short i;
   BOOL mounted;
   
   mounted = [media isMounted];
   
   yes = ExtLocalizedString(@"Yes", "");
   no = ExtLocalizedString(@"No", "");
   bytes = ExtLocalizedString(@"bytes", "");
   monikers[0] = bytes; 
   
   [_infoText setEditable:YES];
   [_infoText setString:@""];
   
   line = ExtMakeInfoTitle(ExtLocalizedString(@"IOKit Name", ""));
   data = [@" " stringByAppendingString:[media ioRegistryName]];
   ExtInfoInsert();
   
   line = ExtMakeInfoTitle(ExtLocalizedString(@"Device", ""));
   data = [@" " stringByAppendingString:[media bsdName]];
   ExtInfoInsert();
   
   line = ExtMakeInfoTitle(ExtLocalizedString(@"Ejectable", ""));
   data = [@" " stringByAppendingString:
      ([media isEjectable] ? yes : no)];
   ExtInfoInsert();
   
   line = ExtMakeInfoTitle(ExtLocalizedString(@"DVD/CD-ROM", ""));
   data = [@" " stringByAppendingString:
      ([media isDVDROM] || [media isCDROM] ? yes : no)];
   ExtInfoInsert();
      
   line = ExtMakeInfoTitle(ExtLocalizedString(@"Mount Point", ""));
   data = ExtLocalizedString(@"Not Mounted", "");
   data = [@" " stringByAppendingString:
      ([media isMounted] ? [media mountPoint] : data)];
   ExtInfoInsert();
   
   line = ExtMakeInfoTitle(ExtLocalizedString(@"Writable", ""));
   data = [@" " stringByAppendingString:
      ([media isWritable] ? yes : no)];
   ExtInfoInsert();
   
   if (mounted) {
      line = ExtMakeInfoTitle(ExtLocalizedString(@"Filesystem", ""));
      data = [@" " stringByAppendingString:NSFSNameFromType([media fsType])];
      ExtInfoInsert();
      
      line = ExtMakeInfoTitle(ExtLocalizedString(@"Volume Name", ""));
      data = [media volName];
      data = [@" " stringByAppendingString:(data ? data : @"")];
      ExtInfoInsert();
      
      line = ExtMakeInfoTitle(ExtLocalizedString(@"Volume UUID", ""));
      data = [media uuidString];
      data = [@" " stringByAppendingString:(data ? data : @"")];
      ExtInfoInsert();
      
      line = ExtMakeInfoTitle(ExtLocalizedString(@"Supports Journaling", ""));
      data = [@" " stringByAppendingString:([media hasJournal] ? yes : no)];
      ExtInfoInsert();
      
      line = ExtMakeInfoTitle(ExtLocalizedString(@"Journaled", ""));
      data = [@" " stringByAppendingString:([media isJournaled] ? yes : no)];
      ExtInfoInsert();
      
      line = ExtMakeInfoTitle(ExtLocalizedString(@"Supports Sparse Files", ""));
      data = [@" " stringByAppendingString:([media hasSparseFiles] ? yes : no)];
      ExtInfoInsert();
      
      line = ExtMakeInfoTitle(ExtLocalizedString(@"Case Sensitive Names", ""));
      data = [@" " stringByAppendingString:([media isCaseSensitive] ? yes : no)];
      ExtInfoInsert();
      
      line = ExtMakeInfoTitle(ExtLocalizedString(@"Case Preserved Names", ""));
      data = [@" " stringByAppendingString:([media isCasePreserving] ? yes : no)];
      ExtInfoInsert();
      
      size = [media size];
      for (i=0; size > 1024.0; ++i)
         size /= 1024.0;
      data = monikers[i];
      line = ExtMakeInfoTitle(ExtLocalizedString(@"Size", ""));
      data = [@" " stringByAppendingFormat:@"%.2lf %@ (%qu %@)",
         size, data, [media size], bytes];
      ExtInfoInsert();
      
      size = [media availableSize];
      for (i=0; size > 1024.0; ++i)
         size /= 1024.0;
      data = monikers[i];
      line = ExtMakeInfoTitle(ExtLocalizedString(@"Available Space", ""));
      data = [@" " stringByAppendingFormat:@"%.2lf %@ (%qu %@)",
         size, data, [media availableSize], bytes];
      ExtInfoInsert();
      
      line = ExtMakeInfoTitle(ExtLocalizedString(@"Block Size", ""));
      data = [@" " stringByAppendingFormat:@"%lu %@", [media blockSize], bytes];
      ExtInfoInsert();
      
      line = ExtMakeInfoTitle(ExtLocalizedString(@"Number of Blocks", ""));
      data = [@" " stringByAppendingFormat:@"%qu", [media blockCount]];
      ExtInfoInsert();
      
      line = ExtMakeInfoTitle(ExtLocalizedString(@"Number of Files", ""));
      data = [@" " stringByAppendingFormat:@"%qu", [media fileCount]];
      ExtInfoInsert();
      
      line = ExtMakeInfoTitle(ExtLocalizedString(@"Number of Directories", ""));
      data = [@" " stringByAppendingFormat:@"%qu", [media dirCount]];
      ExtInfoInsert();
      
      if ([media isExtFS]) {
         [_infoText insertText:@"\n"];
         line = ExtMakeInfoTitle(ExtLocalizedString(@"Ext2/3 Specific", ""));
         [line setAlignment:NSCenterTextAlignment range:NSMakeRange(0, [line length])];
         [_infoText insertText:line];
         [_infoText insertText:@"\n\n"];
         
         line = ExtMakeInfoTitle(ExtLocalizedString(@"Indexed Directories", ""));
         data = [@" " stringByAppendingString:([media hasIndexedDirs] ? yes : no)];
         ExtInfoInsert();
         
         line = ExtMakeInfoTitle(ExtLocalizedString(@"Large Files", ""));
         data = [@" " stringByAppendingString:([media hasLargeFiles] ? yes : no)];
         ExtInfoInsert();
      }
      
   } else {// mounted
      size = [media size];
      for (i=0; size > 1024.0; ++i)
         size /= 1024.0;
      data = monikers[i];
      line = ExtMakeInfoTitle(ExtLocalizedString(@"Device Size", ""));
      data = [@" " stringByAppendingFormat:@"%.2lf %@ (%qu %@)",
         size, data, [media size], bytes];
      ExtInfoInsert();
      
      line = ExtMakeInfoTitle(ExtLocalizedString(@"Device Block Size", ""));
      data = [@" " stringByAppendingFormat:@"%lu %@", [media blockSize], bytes];
      ExtInfoInsert();
   }
   
   [_infoText setEditable:NO];
}

- (void)setOptionState:(ExtFSMedia*)media
{
   NSCellStateValue state;
   NSMutableDictionary *dict;
   NSNumber *boolVal;
   BOOL mediaRO;
   
   mediaRO = [media isDVDROM] || [media isCDROM];
   
   dict = [_prefMedia objectForKey:[media uuidString]];
   
   boolVal = [dict objectForKey:EXT_PREF_KEY_DIRINDEX];
   state = ([media hasIndexedDirs] || [boolVal boolValue] ? NSOnState : NSOffState);
   [_indexedDirsBox setState:state];
   if (NSOnState == state || mediaRO)
      [_indexedDirsBox setEnabled:NO];
   else
      [_indexedDirsBox setEnabled:YES];
      
   if (!dict)
      return;
   
   boolVal = [dict objectForKey:EXT_PREF_KEY_NOAUTO];
   state = ([boolVal boolValue] ? NSOnState : NSOffState);
   [_dontAutomountBox setState:state];
   
   boolVal = [dict objectForKey:EXT_PREF_KEY_RDONLY];
   state = ([boolVal boolValue] || mediaRO ? NSOnState : NSOffState);
   [_mountReadOnlyBox setState:state];
   if (!mediaRO)
      [_mountReadOnlyBox setEnabled:YES];
   else
      [_mountReadOnlyBox setEnabled:NO];
}

- (void)doMediaSelection:(ExtFSMedia*)media
{
   if (!media) {
      ExtSetDefaultState();
      return;
   }
   
   _curSelection = media;
#ifdef DIAGNOSTIC
   NSLog(@"ExtFS: media %@ selected, canMount=%d",
      [media bsdName], [media canMount]);
#endif
   
   /* Change state of buttons, options, info etc */
   [self generateInfo:media];
   
   [_diskIconView setImage:[media icon]];
   
   if ([media isExtFS] && [media isMounted]) {
      if (nil != [media uuidString]) {
         [self setOptionState:media];
         [_infoButton setEnabled:YES];
      } else {
         NSLog(@"ExtFS: Options for '%@' disabled -- missing UUID.\n",
            [media bsdName]);
         [_infoButton setEnabled:NO];
      }
   } else
      [_infoButton setEnabled:NO];

   [self doOptions:self];
   
   [self updateMountState:media];
   
   if ([media isEjectable])
      [_ejectButton setEnabled:YES];
   else
      [_ejectButton setEnabled:NO];
}

/* Public */

- (IBAction)click_readOnly:(id)sender
{
   NSNumber *boolVal;
   NSString *uuid;
   NSMutableDictionary *dict;
   
#ifdef DIAGNOSTIC
   NSLog(@"ExtFS: readonly clicked.\n");
#endif
   
   uuid = [_curSelection uuidString];
   dict = [_prefMedia objectForKey:uuid];
   if (!dict) {
      dict = [NSMutableDictionary dictionary];
      [_prefMedia setObject:dict forKey:uuid];
   }
   
   boolVal = [NSNumber numberWithBool:
      (NSOnState == [_mountReadOnlyBox state] ? YES : NO)];
   [dict setObject:boolVal forKey:EXT_PREF_KEY_RDONLY];
   _prefsChanged = YES;
}

- (IBAction)click_automount:(id)sender
{
   NSNumber *boolVal;
   NSString *uuid;
   NSMutableDictionary *dict;
   
#ifdef DIAGNOSTIC
   NSLog(@"ExtFS: automount clicked.\n");
#endif
   
   uuid = [_curSelection uuidString];
   dict = [_prefMedia objectForKey:uuid];
   if (!dict) {
      dict = [NSMutableDictionary dictionary];
      [_prefMedia setObject:dict forKey:uuid];
   }
   
   boolVal = [NSNumber numberWithBool:
      (NSOnState == [_dontAutomountBox state] ? YES : NO)];
   [dict setObject:boolVal forKey:EXT_PREF_KEY_NOAUTO];
   _prefsChanged = YES;
}

- (IBAction)click_indexedDirs:(id)sender
{
   NSNumber *boolVal;
   NSString *uuid;
   NSMutableDictionary *dict;
   
#ifdef DIAGNOSTIC
   NSLog(@"ExtFS: indexed dirs clicked.\n");
#endif
   
   uuid = [_curSelection uuidString];
   dict = [_prefMedia objectForKey:uuid];
   if (!dict) {
      dict = [NSMutableDictionary dictionary];
      [_prefMedia setObject:dict forKey:uuid];
   }
   
   boolVal = [NSNumber numberWithBool:
      (NSOnState == [_dontAutomountBox state] ? YES : NO)];
   [dict setObject:boolVal forKey:EXT_PREF_KEY_DIRINDEX];
   _prefsChanged = YES;
}

- (void)doMount:(id)sender
{
   int err, mount;
   
   mount = ![_curSelection isMounted];
#ifdef DIAGNOSTIC
   NSLog(@"ExtFS: %@ '%@'.\n", (mount ? @"Mounting" : @"Unmounting"),
      [_curSelection bsdName]);
#endif
   
   if (mount)
      err = [[ExtFSMediaController mediaController] mount:_curSelection on:nil];
   else
      err = [[ExtFSMediaController mediaController] unmount:_curSelection
         force:NO eject:NO];
   if (!err)
      [self updateMountState:_curSelection];
   else {
      NSString *op = (mount ? @"mount" : @"unmount");
      op = ExtLocalizedString(op, "");
      NSBeginCriticalAlertSheet(ExtLocalizedString(@"Disk Error", ""),
         @"OK", nil, nil, [NSApp keyWindow], nil, nil, nil, nil,
         [NSString stringWithFormat:@"%@ %@ '%@'",
            ExtLocalizedString(@"Could not", ""), op, [_curSelection bsdName]]);
   }
}

- (void)doEject:(id)sender
{
   int err;
#ifdef DIAGNOSTIC
   NSLog(@"ExtFS: Ejecting '%@'.\n", [_curSelection bsdName]);
#endif
   
   err = [[ExtFSMediaController mediaController] unmount:_curSelection
      force:NO eject:YES];
   if (err) {
      NSBeginCriticalAlertSheet(ExtLocalizedString(@"Disk Error", ""),
         @"OK", nil, nil, [NSApp keyWindow], nil, nil, nil, nil,
         [NSString stringWithFormat:@"%@ '%@'",
            ExtLocalizedString(@"Could not eject", ""), [_curSelection bsdName]]);
   }
}

- (void)doOptions:(id)sender
{
   BOOL opts, info, startup, enabled;;
   
   startup = [[[_tabs selectedTabViewItem] identifier] isEqualTo:@"Startup"];
   opts = [[[_tabs selectedTabViewItem] identifier]
      isEqualTo:EXT_TOOLBAR_ALT_ACTION_INFO];
   info = !opts && !startup;
   enabled = [_infoButton isEnabled];
   
NSLog(@"start=%d, opts=%d, info=%d, enabled=%d, infoAlt=%d\n",
   startup, opts, info, enabled, _infoButtonAlt);
   
   if (!info || !enabled) {
      if (!info)
         [_tabs selectTabViewItemWithIdentifier:EXT_TOOLBAR_ACTION_INFO];
      if (enabled)
         goto info_alt_switch;
      else 
         goto info_alt_switch_back;
      return;
   }
   
   if (info && enabled) {
      if (self != sender) {
         // User action
         [_tabs selectTabViewItemWithIdentifier:EXT_TOOLBAR_ALT_ACTION_INFO];
info_alt_switch_back:
         if (_infoButtonAlt)
            ExtSwapButtonState(_infoButton, YES);
         _infoButtonAlt = NO;
         return;
      }
info_alt_switch:
      if (!_infoButtonAlt)
         ExtSwapButtonState(_infoButton, YES);
      _infoButtonAlt = YES;
   }
}

- (void)mainViewDidLoad
{
   NSDictionary *plist;
   NSString *title, *tmp;
   NSButton *button;
   NSButtonCell *buttonCell;
   NSImage *image;
   NSButton *buttons[] = {_mountButton, _ejectButton, _infoButton, nil};
   SEL tbactions[] = {@selector(doMount:), @selector(doEject:),
      @selector(doOptions:)};
   NSString *titles[] = {EXT_TOOLBAR_ACTION_MOUNT, EXT_TOOLBAR_ACTION_EJECT,
      EXT_TOOLBAR_ACTION_INFO, nil};
   NSString *alt_titles[] = {EXT_TOOLBAR_ALT_ACTION_MOUNT, nil,
      EXT_TOOLBAR_ALT_ACTION_INFO, nil};
   NSString *alt_images[] = {nil, nil, EXT_TOOLBAR_ALT_ACTION_INFO, nil};
   int i;
   
   _infoButtonAlt = NO; // Used to deterimine state
   
   /* Get our prefs */
   plist = [[self bundle] infoDictionary];
   _bundleid = [plist objectForKey:@"CFBundleIdentifier"];
   
   _prefRoot = [[NSMutableDictionary alloc] initWithDictionary:
      [[NSUserDefaults standardUserDefaults] persistentDomainForName:_bundleid]];
   if (![_prefRoot count]) {
   #ifdef DIAGNOSTIC
      NSLog(@"ExtFS: Creating preferences\n");
   #endif
      // Create the defaults
      [_prefRoot setObject:[NSMutableDictionary dictionary] forKey:EXT_PREF_KEY_GLOBAL];
      [_prefRoot setObject:[NSMutableDictionary dictionary] forKey:EXT_PREF_KEY_MEDIA];
      [_prefRoot setObject:[NSMutableDictionary dictionary] forKey:EXT_PREF_KEY_MGR];
      // Save them
      [self didUnselect];
   }
   _prefGlobal = [[NSMutableDictionary alloc] initWithDictionary:
      [_prefRoot objectForKey:EXT_PREF_KEY_GLOBAL]];
   _prefMedia = [[NSMutableDictionary alloc] initWithDictionary:
      [_prefRoot objectForKey:EXT_PREF_KEY_MEDIA]];
   _prefMgr = [[NSMutableDictionary alloc] initWithDictionary:
      [_prefRoot objectForKey:EXT_PREF_KEY_MGR]];
   [_prefRoot setObject:_prefGlobal forKey:EXT_PREF_KEY_GLOBAL];
   [_prefRoot setObject:_prefMedia forKey:EXT_PREF_KEY_MEDIA];
   [_prefRoot setObject:_prefMgr forKey:EXT_PREF_KEY_MGR];
#ifdef notnow
   NSLog(@"ExtFS: Prefs = %@\n", _prefRoot);
#endif
   
   [_tabs setTabViewType:NSNoTabsNoBorder];
   
   button = buttons[0];
   for (i=0; nil != button; ++i, button = buttons[i]) {
      buttonCell = [button cell];
      
      [button setImagePosition:NSImageAbove];
      [button setBezelStyle:NSShadowlessSquareBezelStyle];
      [button setButtonType:NSMomentaryPushInButton];
      // [button setShowsBorderOnlyWhileMouseInside:YES];
      [buttonCell setAlignment:NSCenterTextAlignment];
      [buttonCell setTarget:self];
      [buttonCell setAction:tbactions[i]];
      [buttonCell sendActionOn:NSLeftMouseUp];
      [buttonCell setImageDimsWhenDisabled:YES];
      
      tmp = titles[i];
      [buttonCell setRepresentedObject:tmp]; //Used for state
      
      /* Setup primaries */
      title = ExtLocalizedString(tmp, "Toolbar Item Title");
      [button setTitle:title];
      
      tmp = [[self bundle] pathForResource:tmp ofType:EXT_TOOLBAR_ICON_TYPE];
      image = [[NSImage alloc] initWithContentsOfFile:tmp];
      [button setImage:image];
      
      [image release];
      
      /* Setup alternates */
      tmp = alt_titles[i];
      if (tmp) {
         title = ExtLocalizedString(tmp, "Toolbar Item Title (Alternate)");
         [button setAlternateTitle:title];
      }
      tmp = alt_images[i];
      if (tmp) {
         tmp = [[self bundle] pathForResource:tmp ofType:EXT_TOOLBAR_ICON_TYPE];
         image = [[NSImage alloc] initWithContentsOfFile:tmp];
         [button setAlternateImage:image];
         [image release];
      }
      
      [button setEnabled:NO];
   }
   
   [_mountReadOnlyBox setTitle:ExtLocalizedString(
      @"Mount Read Only", "")];
   [_dontAutomountBox setTitle:ExtLocalizedString(
      @"Don't Automount", "")];
   [_indexedDirsBox setTitle:ExtLocalizedString(
      @"Enable Indexed Directories", "")];
   
   [_optionNoteText setStringValue:ExtLocalizedString(
      @"Changes to these options will take effect during the next mount.", "")];

   [self performSelector:@selector(startup) withObject:nil afterDelay:0.3];
}

- (void)didSelect
{
}

- (void)didUnselect
{
   NSArray *paths;
   NSString *path;
   
   if (!_prefsChanged)
      return;
   
   // Save the prefs
   [[NSUserDefaults standardUserDefaults]
      removePersistentDomainForName:_bundleid];
   [[NSUserDefaults standardUserDefaults] setPersistentDomain:_prefRoot
      forName:_bundleid];
   
   _prefsChanged = NO;
   
   /* Copy the plist to the /Library/Preferences dir. This is needed,
      because mount will run as root 99% of the time, and therefore won't
      have access to the user specific prefs. */
   paths = NSSearchPathForDirectoriesInDomains(NSLibraryDirectory, NSLocalDomainMask, NO);
   path = [[paths objectAtIndex:0] stringByAppendingPathComponent:
      @"Preferences/" EXT_PREF_ID @".plist"];
   if ([[NSFileManager defaultManager] fileExistsAtPath:path]) {
      // Remove it since we may not have write access to the file itself
      (void)[[NSFileManager defaultManager] removeFileAtPath:path handler:nil];
   }
   if (![_prefRoot writeToFile:path atomically:YES]) {
      NSLog(@"ExtFS: Could not copy preferences to %@."
         @" This is most likely because you do not have write permissions."
         @" Changes made will have no effect when mounting volumes.", path);
      NSBeep();
   }
}

/* Delegate */

- (int)outlineView:(NSOutlineView *)outlineView numberOfChildrenOfItem:(id)item
{
   //NSLog(@"extfs: outline # children for %@", item);
   if (!item)
      return ([_volData count]); // Return # of children for top-level item
      
   return ([item childCount]);
}

- (BOOL)outlineView:(NSOutlineView *)outlineView isItemExpandable:(id)item
{
   //NSLog(@"extfs: outline expand for %@", item);
   if ([item childCount])
      return (YES);
   
   return (NO);
}

- (id)outlineView:(NSOutlineView *)outlineView child:(int)index ofItem:(id)item
{
   //NSLog(@"extfs: outline get child at index %d for %@", index, item);
   if (!item) {
      return ([_volData objectAtIndex:index]);
   }
   
   if (index < [item childCount])
      return ([[item children] objectAtIndex:index]);
      
   return (nil);
}

- (id)outlineView:(NSOutlineView *)outlineView
   objectValueForTableColumn:(NSTableColumn *)tableColumn byItem:(id)item
{
   NSString *name;
   //NSLog(@"extfs: outline obj val for %@", item);
   
   name = [item volName];
   return (name ? name : [item ioRegistryName]);
}

- (void)outlineViewSelectionDidChange:(NSNotification *)notification
{
   //NSLog(@"extfs: outline selection row = %u", [_vollist selectedRow]);
   [self doMediaSelection:[_vollist itemAtRow:[_vollist selectedRow]]];
}

@end
