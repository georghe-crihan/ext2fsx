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

#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include <CoreFoundation/CoreFoundation.h>

#include "extfsmgr.h"

#include <gnu/ext2fs/ext2_fs.h>

static CFStringRef extmgr_uuid(const char *device)
{
   CFStringRef str;
   CFUUIDRef uuid;
   CFUUIDBytes *ubytes;
   char *buf;
   struct ext2_super_block *sbp;
   int fd, bytes;
   
   buf = malloc(4096);
   if (!buf) {
      fprintf(stderr, "%s: malloc failed, %s\n", progname,
			strerror(ENOMEM));
      return (NULL);
   }
   
   fd = open(device, O_RDONLY, 0);
   if (fd < 0) {
      free(buf);
      fprintf(stderr, "%s: open '%s' failed, %s\n", progname, device,
			strerror(errno));
      return (NULL);
   }
   
   bytes = read (fd, buf, 4096);
   if (4096 != bytes) {
      free(buf);
      fprintf(stderr, "%s: device read '%s' failed, %s\n", progname, device,
			strerror(errno));
      return (NULL);
   }
   
   close(fd);
   
    /* Superblock starts at offset 1024 (block 2). */
   sbp = (struct ext2_super_block*)(buf+1024);
   if (EXT2_SUPER_MAGIC != le16_to_cpu(sbp->s_magic)) {
      free(buf);
      fprintf(stderr, "%s: device '%s' does not contain a valid filesystem\n",
         progname, device);
      return (NULL);
   }
   
   ubytes = (CFUUIDBytes*)&sbp->s_uuid[0];
   uuid = CFUUIDCreateFromUUIDBytes(kCFAllocatorDefault, *ubytes);
   free(buf);
   if (uuid) {
      str = CFUUIDCreateString(kCFAllocatorDefault, uuid);
      CFRelease(uuid);
      return (str);
   }
   return (NULL);
}

void extmgr_mntopts (const char *device, int *mopts, int *eopts, int *nomount)
{
   CFPropertyListRef mediaRoot;
   CFDictionaryRef media;
   CFStringRef uuid;
   CFBooleanRef boolVal;
   
   *nomount = 0;
   
   mediaRoot = CFPreferencesCopyAppValue(EXT_PREF_KEY_MEDIA, EXT_PREF_ID);
   if (mediaRoot && CFDictionaryGetTypeID() == CFGetTypeID(mediaRoot)) {
      uuid = extmgr_uuid(device);
      if (uuid) {
         media = CFDictionaryGetValue(mediaRoot, uuid);
         if (media && CFDictionaryGetTypeID() == CFGetTypeID(media)) {
            boolVal = CFDictionaryGetValue(media, EXT_PREF_KEY_NOAUTO);
            if (boolVal && CFBooleanGetValue(boolVal)) {
               *nomount = 1;
               goto out;
            }
            
            boolVal = CFDictionaryGetValue(media, EXT_PREF_KEY_RDONLY);
            if (boolVal && CFBooleanGetValue(boolVal)) {
               *mopts |= MNT_RDONLY;
            }
            
            boolVal = CFDictionaryGetValue(media, EXT_PREF_KEY_DIRINDEX);
            if (boolVal && CFBooleanGetValue(boolVal)) {
               *eopts |= EXT2_MNT_INDEX;
            }
         }
out:
         CFRelease(uuid);
      }
   }
   if (mediaRoot)
      CFRelease(mediaRoot);
}
