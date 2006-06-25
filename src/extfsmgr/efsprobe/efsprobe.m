/*
* Copyright 2004,2006 Brian Bergstrand.
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
#import <sys/types.h>
#import <sys/mount.h>
#import <sys/mman.h>
#import <unistd.h>
#import <fcntl.h>

#import <ufs/ufs/dinode.h>
#import <ufs/ffs/fs.h>

#import <ext2_byteorder.h>
#import <gnu/ext2fs/ext2_fs.h>

// We don't want HFSVolumes.h from CarbonCore
#define __HFSVOLUMES__
#import "ExtFSMedia.h"
#undef __HFSVOLUMES__
#import <hfs/hfs_format.h>

char *progname;

struct superblock {
    struct ext2_super_block *s_es;
};

#define EXT_SUPER_SIZE 32768
#define EXT_SUPER_OFF 1024
#define HFS_SUPER_OFF 1024
static ExtFSType efs_getdevicefs (const char *device, NSString **uuidStr)
{
    int fd, bytes;
    char *buf;
    struct superblock sb;
    HFSPlusVolumeHeader *hpsuper;
    HFSMasterDirectoryBlock *hsuper;
    struct fs *usuper;
    ExtFSType type = fsTypeUnknown; 
    char path[PATH_MAX] = "/dev/r";
    BOOL slocked = NO;
    
    if (uuidStr)
        *uuidStr = nil;
    
    buf = malloc(EXT_SUPER_SIZE);
    if (!buf) {
        fprintf(stderr, "%s: malloc failed, %s\n", progname,
            strerror(ENOMEM));
        return (-ENOMEM);
    }
    
    bytes = strlen(path);
    snprintf((path+bytes), PATH_MAX-1-bytes, "%s", device);
    path[PATH_MAX-1] = 0;
    
    fd = open(path, O_RDONLY, 0);
    if (fd < 0) {
        fprintf(stderr, "%s: open '%s' failed, %s\n", progname, path,
            strerror(errno));
        free(buf);
        return (-errno);
    }
    
    if (0 == geteuid()) {
        mprotect(buf, EXT_SUPER_SIZE, PROT_EXEC | PROT_WRITE | PROT_READ);
        slocked = (0 == mlock(buf, EXT_SUPER_SIZE));
    }
    
    bytes = read (fd, buf, EXT_SUPER_SIZE);
    close(fd);

    if (EXT_SUPER_SIZE != bytes) {
        fprintf(stderr, "%s: device read '%s' failed, %s\n", progname, path,
            strerror(errno));
        bzero(buf, EXT_SUPER_SIZE);
        if (slocked)
            (void)munlock(buf, EXT_SUPER_SIZE);
        free(buf);
        return (-errno);
    }

    unsigned char *uuidbytes = NULL;
    unsigned char *hfsuidbytes = NULL;
    
    /* Ext2 and HFS(+) Superblocks start at offset 1024 (block 2). */
    sb.s_es = (struct ext2_super_block*)(buf+EXT_SUPER_OFF);
    if (EXT2_SUPER_MAGIC == le16_to_cpu(sb.s_es->s_magic)) {
        type = fsTypeExt2;
        if (0 != EXT2_HAS_COMPAT_FEATURE(&sb, EXT3_FEATURE_COMPAT_HAS_JOURNAL))
            type = fsTypeExt3;
        uuidbytes = sb.s_es->s_uuid;
    } else {
        hpsuper = (HFSPlusVolumeHeader*)(buf+HFS_SUPER_OFF);
        if (kHFSPlusSigWord == be16_to_cpu(hpsuper->signature)) {
            type = fsTypeHFSPlus;
            if (be32_to_cpu(hpsuper->attributes) & kHFSVolumeJournaledBit)
                type = fsTypeHFSJ;
            // unique id is in the last 8 bytes of the finder info storage
            // this is obviously not a UUID
            hfsuidbytes = &hpsuper->finderInfo[24];
        }
        else if (kHFSXSigWord == be16_to_cpu(hpsuper->signature)) {
            type = fsTypeHFSX;
            hfsuidbytes = &hpsuper->finderInfo[24];
        } else if (kHFSSigWord == be16_to_cpu(hpsuper->signature)) {
            type = fsTypeHFS;
            hsuper = (HFSMasterDirectoryBlock*)(buf+HFS_SUPER_OFF);
            if (kHFSPlusSigWord == be16_to_cpu(hsuper->drEmbedSigWord))
                type = fsTypeHFSPlus; // XXX - we'd have to read the embedded volume header to get the UUID
        } else {
            usuper = (struct fs*)(buf+SBOFF);
            if (FS_MAGIC == be32_to_cpu(usuper->fs_magic)) {
                struct ufslabel *ulabel = (struct ufslabel*)(buf+UFS_LABEL_OFFSET);
                const u_char lmagic[] = UFS_LABEL_MAGIC;
                type = fsTypeUFS;
                if (uuidStr && *((u_int32_t*)&lmagic[0]) == be32_to_cpu(ulabel->ul_magic))
                    *uuidStr = [[NSString alloc] initWithFormat:@"%qX", be64_to_cpu(ulabel->ul_uuid)];
            }
        }
    }
    
    if (uuidStr && uuidbytes) {
        CFUUIDRef cuuid = CFUUIDCreateFromUUIDBytes(kCFAllocatorDefault,
            *((CFUUIDBytes*)uuidbytes));
        if (cuuid) {
            *uuidStr = (NSString*)CFUUIDCreateString(kCFAllocatorDefault, cuuid);
            CFRelease(cuuid);
        }
    } else if (uuidStr && hfsuidbytes) {
        *uuidStr = [[NSString alloc] initWithFormat:@"%qX", be64_to_cpu(*((u_int64_t*)hfsuidbytes))];
    }

    bzero(buf, EXT_SUPER_SIZE);
    if (slocked)
        (void)munlock(buf, EXT_SUPER_SIZE);
    free(buf);
    return (type);
}

int main (int argc, char *argv[])
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    NSString *uuid = nil;
    ExtFSType type;
    
    if (2 != argc)
        return (-1);
    
    progname = strrchr(argv[0], '/');
    if (!progname)
        progname = argv[0];
    
    type = efs_getdevicefs(argv[1], &uuid);
    if (uuid) {
        printf ("%s", [uuid UTF8String]);
        [uuid release];
    }
    [pool release];
    return (type);
}
