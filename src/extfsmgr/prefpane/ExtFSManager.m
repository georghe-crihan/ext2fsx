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

#import <ExtFSDiskManager/ExtFSDiskManager.h>
#import "ExtFSManager.h"

/* Note: For the "Options" button, the image/title
      is the reverse of the actual action. */

#define EXT_TOOLBAR_ACTION_COUNT 3
#define EXT_TOOLBAR_ACTION_MOUNT @"Mount"
#define EXT_TOOLBAR_ACTION_EJECT @"Eject"
#define EXT_TOOLBAR_ACTION_INFO @"Info"

#define EXT_TOOLBAR_ALT_ACTION_MOUNT @"Unmount"
#define EXT_TOOLBAR_ALT_ACTION_INFO @"Options"

#define EXT_TOOLBAR_ICON_TYPE @"icns"	

static void ExtSwapButtonState(id button, BOOL swapImage)
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

static id ExtMakeInfoTitle(NSString *title)
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

/* Localized strings */
static NSString *_yes, *_no, *_bytes;
static NSString *_monikers[] =
      {@"bytes", @"KB", @"MB", @"GB", @"TB", @"PB", @"EB", @"ZB", @"YB", nil};
//     KiloByte, MegaByte, GigaByte, TeraByte, PetaByte, ExaByte, ZetaByte, YottaByte
//bytes  2^10,     2^20,     2^30,     2^40,     2^50,     2^60,    2^70,     2^80

static NSDictionary *_fsPrettyNames;

static void ExtSetPrefVal(ExtFSMedia *media, id key, id val)
{
    NSString *uuid;
    NSMutableDictionary *dict;
    
    uuid = [media uuidString];
    dict = [_prefMedia objectForKey:uuid];
    if (!dict) {
        dict = [NSMutableDictionary dictionary];
        [_prefMedia setObject:dict forKey:uuid];
    }
    [dict setObject:val forKey:key];
    _prefsChanged = YES;
}

static int _operations = 0;
#define BeginOp() do { \
if (0 == _operations) [_opProgress startAnimation:nil]; \
++_operations; \
} while(0)

#define EndOp() do { \
--_operations; \
if (0 == _operations) [_opProgress stopAnimation:nil]; \
if (_operations < 0) _operations = 0; \
} while(0)

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
            ExtSwapButtonState(_mountButton, YES);
            [(NSButtonCell*)[_mountButton cell]
               setRepresentedObject:EXT_TOOLBAR_ALT_ACTION_MOUNT];
         }
      } else 
         goto unmount_state;
   } else {
      [_mountButton setEnabled:NO];
unmount_state:
      if ([tmp isEqualTo:EXT_TOOLBAR_ALT_ACTION_MOUNT]) {
         ExtSwapButtonState(_mountButton, YES);
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
   NSLog(@"ExtFSM: **** Media '%s' appeared ***\n", BSDNAMESTR(media));
#endif

   if ((parent = [media parent])) {
      if (![_volData containsObject:parent] && nil == [parent parent]) {
         [_volData addObject:parent];
         goto exit;
      } else {
         if ([_vollist isItemExpanded:parent])
            [_vollist reloadItem:parent reloadChildren:YES];
         else
            [_vollist reloadItem:parent reloadChildren:NO];
         return;
      }
   } else {
      [_volData addObject:media];
   }

exit:
   [_volData sortUsingSelector:@selector(compare:)];
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
   
   EndOp();
   media = [notification object];
   
#ifdef DIAGNOSTIC
   NSLog(@"ExtFSM: **** Media '%s' disappeared ***\n", BSDNAMESTR(media));
#endif

   if ((parent = [media parent])) {
      [_vollist reloadItem:parent reloadChildren:YES];
      return;
   }
   
   [_volData removeObject:media];
   [_vollist reloadData];
   if (-1 == [_vollist selectedRow])
      ExtSetDefaultState();
}

- (void)volMount:(NSNotification*)notification
{
   ExtFSMedia *media;
   
   EndOp();
   media = [notification object];
#ifdef DIAGNOSTIC
   NSLog(@"ExtFSM: **** Media '%s' mounted ***\n", BSDNAMESTR(media));
#endif
   if (media == _curSelection)
      [self doMediaSelection:media];
   [_vollist reloadItem:media];
}

- (void)volUnmount:(NSNotification*)notification
{
   ExtFSMedia *media;
   
   EndOp();
   media = [notification object];
#ifdef DIAGNOSTIC
   NSLog(@"ExtFSM: **** Media '%s' unmounted ***\n", BSDNAMESTR([notification object]));
#endif
   if (media == _curSelection)
      [self doMediaSelection:media];
   [_vollist reloadItem:media];
}

- (void)volInfoUpdated:(NSNotification*)notification
{
#if 0
   ExtFSMedia *media;
   
   media = [notification object];
    if (media == _curSelection)
      [self generateInfo:media];
#endif
}

- (void)childChanged:(NSNotification*)notification
{

}

- (void)mediaError:(NSNotification*)notification
{
   NSString *op, *device, *errStr, *msg;
   NSNumber *err;
   NSDictionary *info = [notification userInfo];
   NSWindow *win;
   
   EndOp();
   device = [[notification object] bsdName];
   err = [info objectForKey:ExtMediaKeyOpFailureError];
   op = [info objectForKey:ExtMediaKeyOpFailureType];
   errStr = [info objectForKey:ExtMediaKeyOpFailureErrorString];
   msg = [info objectForKey:ExtMediaKeyOpFailureMsgString];
   
   msg = [NSString stringWithFormat:
            ExtLocalizedString(@"Command: %@\nDevice: %@\nMessage: %@\nError: 0x%X", ""),
            op, device, errStr, [err intValue]];
   
   win = [NSApp keyWindow];
   if (nil == [win attachedSheet]) {
        NSBeginCriticalAlertSheet(ExtLocalizedString(@"Disk Error", ""), @"OK",
            nil, nil, win, nil, nil, nil, nil, msg);
   } else {
        NSRunCriticalAlertPanel(ExtLocalizedString(@"Disk Error", ""),
            msg, @"OK", nil, nil);
   }
}

- (void)startup
{
   NSString *title;
   ExtFSMedia *media;
   NSEnumerator *en;
   
   [_vollist setEnabled:NO];
   
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
            selector:@selector(volInfoUpdated:)
            name:ExtFSMediaNotificationUpdatedInfo
            object:nil];
   [[NSNotificationCenter defaultCenter] addObserver:self
            selector:@selector(childChanged:)
            name:ExtFSMediaNotificationChildChange
            object:nil];
   [[NSNotificationCenter defaultCenter] addObserver:self
            selector:@selector(mediaError:)
            name:ExtFSMediaNotificationOpFailure
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
   
   [_vollist setEnabled:YES];
}

// args are NSString*
#define ExtInfoInsert(title, value) \
do { \
line = ExtMakeInfoTitle((title)); \
data = [@" " stringByAppendingString:(value)]; \
data = [data stringByAppendingString:@"\n"]; \
[line appendAttributedString: \
   [[[NSAttributedString alloc] initWithString:data] autorelease]]; \
[_infoText insertText:line]; \
} while(0)

- (void)generateInfo:(ExtFSMedia*)media
{
   NSString *data;
   NSMutableAttributedString *line;
   double size;
   short i;
   BOOL mounted;
   
   mounted = [media isMounted];
   
   [_infoText setEditable:YES];
   [_infoText setString:@""];
   
   ExtInfoInsert(ExtLocalizedString(@"IOKit Name", ""),
      ExtLocalizedString([media ioRegistryName], ""));
   
   ExtInfoInsert(ExtLocalizedString(@"Device", ""),
      [media bsdName]);
   
   ExtInfoInsert(ExtLocalizedString(@"Ejectable", ""),
      ([media isEjectable] ? _yes : _no));
   
   ExtInfoInsert(ExtLocalizedString(@"DVD/CD-ROM", ""),
      ([media isDVDROM] || [media isCDROM] ? _yes : _no));
   
   data = ExtLocalizedString(@"Not Mounted", "");
   ExtInfoInsert(ExtLocalizedString(@"Mount Point", ""),
      (mounted ? [media mountPoint] : data));
   
   ExtInfoInsert(ExtLocalizedString(@"Writable", ""),
      ([media isWritable] ? _yes : _no));
   
   if (mounted) {
      ExtInfoInsert(ExtLocalizedString(@"Filesystem", ""),
         [_fsPrettyNames objectForKey:[NSNumber numberWithInt:[media fsType]]]);
      
      data = [media volName];
      ExtInfoInsert(ExtLocalizedString(@"Volume Name", ""),
         (data ? data : @""));
      
      data = [media uuidString];
      ExtInfoInsert(ExtLocalizedString(@"Volume UUID", ""),
         (data ? data : @""));
      
      ExtInfoInsert(ExtLocalizedString(@"Permissions Enabled", ""),
         ([media hasPermissions] ? _yes : _no));
      
      ExtInfoInsert(ExtLocalizedString(@"Supports Journaling", ""),
         ([media hasJournal] ? _yes : _no));
      
      ExtInfoInsert(ExtLocalizedString(@"Journaled", ""),
         ([media isJournaled] ? _yes : _no));
      
      ExtInfoInsert(ExtLocalizedString(@"Supports Sparse Files", ""),
         ([media hasSparseFiles] ? _yes : _no));
      
      ExtInfoInsert(ExtLocalizedString(@"Case Sensitive Names", ""),
         ([media isCaseSensitive] ? _yes : _no));
      
      ExtInfoInsert(ExtLocalizedString(@"Case Preserved Names", ""),
         ([media isCasePreserving] ? _yes : _no));
      
      size = [media size];
      for (i=0; size > 1024.0; ++i)
         size /= 1024.0;
      data = _monikers[i];
      data = [NSString stringWithFormat:@"%.2lf %@ (%qu %@)",
         size, data, [media size], _bytes];
      ExtInfoInsert(ExtLocalizedString(@"Size", ""), data);
      
      size = [media availableSize];
      for (i=0; size > 1024.0; ++i)
         size /= 1024.0;
      data = _monikers[i];
      data = [NSString stringWithFormat:@"%.2lf %@ (%qu %@)",
         size, data, [media availableSize], _bytes];
      ExtInfoInsert(ExtLocalizedString(@"Available Space", ""), data);
      
      data = [NSString stringWithFormat:@"%lu %@", [media blockSize], _bytes];
      ExtInfoInsert(ExtLocalizedString(@"Block Size", ""), data);
      
      data = [NSString stringWithFormat:@"%qu", [media blockCount]];
      ExtInfoInsert(ExtLocalizedString(@"Number of Blocks", ""), data);
      
      data = [NSString stringWithFormat:@"%qu", [media fileCount]];
      ExtInfoInsert(ExtLocalizedString(@"Number of Files", ""), data);
      
      data = [NSString stringWithFormat:@"%qu", [media dirCount]];
      ExtInfoInsert(ExtLocalizedString(@"Number of Directories", ""), data);
      
      if ([media isExtFS]) {
         [_infoText insertText:@"\n"];
         line = ExtMakeInfoTitle(ExtLocalizedString(@"Ext2/3 Specific", ""));
         [line setAlignment:NSCenterTextAlignment range:NSMakeRange(0, [line length])];
         [_infoText insertText:line];
         [_infoText insertText:@"\n\n"];
         
         ExtInfoInsert(ExtLocalizedString(@"Indexed Directories", ""),
            ([media hasIndexedDirs] ? _yes : _no));
         
         ExtInfoInsert(ExtLocalizedString(@"Large Files", ""),
            ([media hasLargeFiles] ? _yes : _no));
      }
      
   } else {// mounted
      size = [media size];
      for (i=0; size > 1024.0; ++i)
         size /= 1024.0;
      data = _monikers[i];
      data = [NSString stringWithFormat:@"%.2lf %@ (%qu %@)",
         size, data, [media size], _bytes];
      ExtInfoInsert(ExtLocalizedString(@"Device Size", ""), data);
      
      data = [NSString stringWithFormat:@"%lu %@", [media blockSize], _bytes];
      ExtInfoInsert(ExtLocalizedString(@"Device Block Size", ""), data);
   }
   
   // Scroll back to the top
   [_infoText scrollRangeToVisible:NSMakeRange(0,0)]; 
   
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
   state = ([media hasIndexedDirs] || (boolVal && [boolVal boolValue]) ? NSOnState : NSOffState);
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
   
   boolVal = [dict objectForKey:EXT_PREF_KEY_NOPERMS];
   state = ([boolVal boolValue] ? NSOnState : NSOffState);
   [_ignorePermsBox setState:state];
   
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
#ifdef TRACE
   NSLog(@"ExtFSM: media %@ selected, canMount=%d",
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
         NSLog(@"ExtFSM: Options for '%@' disabled -- missing UUID.\n",
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
   
#ifdef TRACE
   NSLog(@"ExtFSM: readonly clicked.\n");
#endif
   
   boolVal = [NSNumber numberWithBool:
      (NSOnState == [_mountReadOnlyBox state] ? YES : NO)];
   ExtSetPrefVal(_curSelection, EXT_PREF_KEY_RDONLY, boolVal);
}

- (IBAction)click_automount:(id)sender
{
   NSNumber *boolVal;
   
#ifdef TRACE
   NSLog(@"ExtFSM: automount clicked.\n");
#endif

   boolVal = [NSNumber numberWithBool:
      (NSOnState == [_dontAutomountBox state] ? YES : NO)];
   ExtSetPrefVal(_curSelection, EXT_PREF_KEY_NOAUTO, boolVal);
}

- (IBAction)click_ignorePerms:(id)sender
{
    NSNumber *boolVal;
    
    boolVal = [NSNumber numberWithBool:
       (NSOnState == [_ignorePermsBox state] ? YES : NO)];
    ExtSetPrefVal(_curSelection, EXT_PREF_KEY_NOPERMS, boolVal);
}

- (IBAction)click_indexedDirs:(id)sender
{
   NSNumber *boolVal;
#ifdef TRACE
   NSLog(@"ExtFSM: indexed dirs clicked.\n");
#endif

   boolVal = [NSNumber numberWithBool:
      (NSOnState == [_indexedDirsBox state] ? YES : NO)];
   ExtSetPrefVal(_curSelection, EXT_PREF_KEY_DIRINDEX, boolVal);
}

- (void)doMount:(id)sender
{
   int err, mount;
   
   mount = ![_curSelection isMounted];
#ifdef TRACE
   NSLog(@"ExtFSM: %@ '%@'.\n", (mount ? @"Mounting" : @"Unmounting"),
      [_curSelection bsdName]);
#endif
   
   // Save the prefs so mount will behave correctly if an Ext2 disk was changed.
   [self savePrefs];
   
   if (mount)
      err = [[ExtFSMediaController mediaController] mount:_curSelection on:nil];
   else
      err = [[ExtFSMediaController mediaController] unmount:_curSelection
         force:NO eject:NO];
   if (0 == err) {
      BeginOp();
      [self updateMountState:_curSelection];
   } else {
      NSString *op = (mount ? @"mount" : @"unmount");
      op = ExtLocalizedString(op, "");
      NSBeginCriticalAlertSheet(ExtLocalizedString(@"Disk Error", ""),
         @"OK", nil, nil, [NSApp keyWindow], nil, nil, nil, nil,
         [NSString stringWithFormat:@"%@ %@ '%@'. Error = %d.",
            ExtLocalizedString(@"Could not", ""), op,
            [_curSelection bsdName]], err);
   }
}

- (void)doEject:(id)sender
{
   int err;
#ifdef TRACE
   NSLog(@"ExtFSM: Ejecting '%@'.\n", [_curSelection bsdName]);
#endif
   
   err = [[ExtFSMediaController mediaController] unmount:_curSelection
      force:NO eject:YES];
   if (!err) {
      BeginOp();
   } else {
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
   
#ifdef TRACE
   NSLog(@"ExtFSM: doOptions: start=%d, opts=%d, info=%d, enabled=%d, infoAlt=%d\n",
      startup, opts, info, enabled, _infoButtonAlt);
#endif
   
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
   NSString *alt_images[] = {EXT_TOOLBAR_ALT_ACTION_MOUNT, nil, EXT_TOOLBAR_ALT_ACTION_INFO, nil};
   int i;
   
   _infoButtonAlt = NO; // Used to deterimine state
   
   // Change controls for startup
   [_startupProgress setStyle:NSProgressIndicatorSpinningStyle];
   [_startupProgress sizeToFit];
   [_startupProgress setDisplayedWhenStopped:NO];
   [_startupProgress setIndeterminate:YES];
   [_startupProgress setUsesThreadedAnimation:YES];
   [_startupProgress startAnimation:self]; // Stopped in [startup]
   [_startupProgress display];
   [_tabs selectTabViewItemWithIdentifier:@"Startup"];
   title = [ExtLocalizedString(@"Please wait, gathering disk information",
      "Startup Message") stringByAppendingString:@"É"];
   [_startupText setStringValue:title];
   [_startupText display];
   
   /* Get our prefs */
   plist = [[self bundle] infoDictionary];
   _bundleid = [plist objectForKey:@"CFBundleIdentifier"];
   
   _prefRoot = [[NSMutableDictionary alloc] initWithDictionary:
      [[NSUserDefaults standardUserDefaults] persistentDomainForName:_bundleid]];
   if (![_prefRoot count]) {
   #ifdef TRACE
      NSLog(@"ExtFSM: Creating preferences\n");
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
   NSLog(@"ExtFSM: Prefs = %@\n", _prefRoot);
#endif
   
   /* Setup localized string globals */
   _yes = [ExtLocalizedString(@"Yes", "") retain];
   _no = [ExtLocalizedString(@"No", "") retain];
   _bytes = [ExtLocalizedString(@"bytes", "") retain];
   _monikers[0] = _bytes;
   
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
   
   [_opProgress setUsesThreadedAnimation:YES];
   [_opProgress setDisplayedWhenStopped:NO];
   [_opProgress displayIfNeeded];
   _operations = 0;
   
   [_mountReadOnlyBox setTitle:
      ExtLocalizedString(@"Mount Read Only", "")];
   [_dontAutomountBox setTitle:
      ExtLocalizedString(@"Don't Automount", "")];
   [_ignorePermsBox setTitle:
      ExtLocalizedString(@"Ignore Permissions", "")]; 
   [_indexedDirsBox setTitle:
      ExtLocalizedString(@"Enable Indexed Directories", "")];
   
   [_optionNoteText setStringValue:
      ExtLocalizedString(@"Changes to these options will take effect during the next mount.", "")];
   
   [_copyrightText setStringValue:
      [[[self bundle] localizedInfoDictionary] objectForKey:@"CFBundleGetInfoString"]];
   [_copyrightText setTextColor:[NSColor disabledControlTextColor]];
   
   /* The correct way to get these names is to enum the FS bundles and
      get the FSName value for each personality. This turns out to be more
      work than it's worth though. */
   _fsPrettyNames = [[NSDictionary alloc] initWithObjectsAndKeys:
      @"Ext2", [NSNumber numberWithInt:fsTypeExt2],
      @"Ext3", [NSNumber numberWithInt:fsTypeExt3],
      @"HFS", [NSNumber numberWithInt:fsTypeHFS],
      ExtLocalizedString(@"HFS Plus", ""), [NSNumber numberWithInt:fsTypeHFSPlus],
      ExtLocalizedString(@"HFS Plus Journaled", ""), [NSNumber numberWithInt:fsTypeHFSJ],
      ExtLocalizedString(@"HFS Plus Journaled/Case Sensitive", ""),
         [NSNumber numberWithInt:fsTypeHFSJCS],
      @"UFS", [NSNumber numberWithInt:fsTypeUFS],
      @"ISO 9660", [NSNumber numberWithInt:fsTypeCD9660],
      ExtLocalizedString(@"CD Audio", ""), [NSNumber numberWithInt:fsTypeCDAudio],
      @"UDF", [NSNumber numberWithInt:fsTypeUDF],
      @"FAT (MSDOS)", [NSNumber numberWithInt:fsTypeMSDOS],
      @"NTFS", [NSNumber numberWithInt:fsTypeNTFS],
      ExtLocalizedString(@"Unknown", ""), [NSNumber numberWithInt:fsTypeUnknown],
      nil];

   [self performSelector:@selector(startup) withObject:nil afterDelay:0.3];
}

- (void)didSelect
{
}

- (void)didUnselect
{
   [self savePrefs];
}

// Private
- (void)savePrefs
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
      NSLog(@"ExtFSM: Could not copy preferences to %@."
         @" This is most likely because you do not have write permissions."
         @" Changes made will have no effect when mounting volumes.", path);
      NSBeep();
   }
}

/* Delegate */

- (int)outlineView:(NSOutlineView *)outlineView numberOfChildrenOfItem:(id)item
{
#ifdef TRACE
   NSLog(@"ExtFSM: outline # children for %@", item);
#endif
   if (!item)
      return ([_volData count]); // Return # of children for top-level item
      
   return ([item childCount]);
}

- (BOOL)outlineView:(NSOutlineView *)outlineView isItemExpandable:(id)item
{
#ifdef TRACE
   NSLog(@"ExtFSM: outline expand for %@", item);
#endif
   if ([item childCount])
      return (YES);
   
   return (NO);
}

- (id)outlineView:(NSOutlineView *)outlineView child:(int)index ofItem:(id)item
{
#ifdef TRACE
   NSLog(@"ExtFSM: outline get child at index %d for %@", index, item);
#endif
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
#ifdef TRACE
   NSLog(@"ExtFSM: outline obj val for %@", item);
#endif
   
   NS_DURING
   name = [item volName];
   if (!name)
      name = [[item mountPoint] lastPathComponent];
   NS_HANDLER
#ifdef DIAGNOSTIC
   NSLog(@"ExtFSM: Item (0x%08X) is not a valid media object.\n", item);
#endif
   return (nil);
   NS_ENDHANDLER
   return (name ? name : [item ioRegistryName]);
}

- (void)outlineViewSelectionDidChange:(NSNotification *)notification
{
#ifdef TRACE
   NSLog(@"ExtFSM: outline selection row = %u", [_vollist selectedRow]);
#endif
   [self doMediaSelection:[_vollist itemAtRow:[_vollist selectedRow]]];
}

@end
