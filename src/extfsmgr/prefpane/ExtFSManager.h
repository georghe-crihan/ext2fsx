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

#import <PreferencePanes/PreferencePanes.h>

@class ExtFSMedia;

@interface ExtFSManager : NSPreferencePane
{
   IBOutlet NSButton *_mountButton;
   IBOutlet NSButton *_ejectButton;
   IBOutlet NSButton *_infoButton;
   IBOutlet NSImageView *_diskIconView;
   IBOutlet NSOutlineView *_vollist;
   IBOutlet NSTabView *_tabs;
   
   IBOutlet id _mountReadOnlyBox;
   IBOutlet id _dontAutomountBox;
   IBOutlet id _indexedDirsBox;
   IBOutlet id _optionNoteText;
   
   IBOutlet id _infoText;
   
   IBOutlet id _startupProgress;
   IBOutlet id _startupText;
   
   id _volData, _curSelection;
   BOOL _infoButtonAlt;
}

- (IBAction)click_readOnly:(id)sender;
- (IBAction)click_automount:(id)sender;
- (IBAction)click_indexedDirs:(id)sender;

- (void)doMount:(id)sender;
- (void)doEject:(id)sender;
- (void)doOptions:(id)sender;

- (void)savePrefs;

- (void)doMediaSelection:(ExtFSMedia*)media;

/* Delegate stuff */
- (int)outlineView:(NSOutlineView *)outlineView numberOfChildrenOfItem:(id)item;
- (BOOL)outlineView:(NSOutlineView *)outlineView isItemExpandable:(id)item;
- (id)outlineView:(NSOutlineView *)outlineView child:(int)index ofItem:(id)item;
- (id)outlineView:(NSOutlineView *)outlineView
   objectValueForTableColumn:(NSTableColumn *)tableColumn byItem:(id)item;

@end

#define EXTFS_MGR_PREF_ID @"net.sourceforge.ext2fs.mgr"

#define ExtLocalizedString(key, comment) \
NSLocalizedStringFromTableInBundle(key,nil, [self bundle], comment)
