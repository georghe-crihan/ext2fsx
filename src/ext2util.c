/*
 * Copyright (c) 2000 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * The contents of this file constitute Original Code as defined in and
 * are subject to the Apple Public Source License Version 1.1 (the
 * "License").  You may not use this file except in compliance with the
 * License.  Please obtain a copy of the License at
 * http://www.apple.com/publicsource and read it before using this file.
 * 
 * This Original Code and all software distributed under the License are
 * distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE OR NON-INFRINGEMENT.  Please see the
 * License for the specific language governing rights and limitations
 * under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */
/*
 * Copyright (c) 1998 Robert Nordier
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/wait.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/sysctl.h>
#include <sys/loadable_fs.h>

#include <dev/disk.h>

#include <machine/byte_order.h>

#include <err.h>
#include <sysexits.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "ext2_apple.h"
#include <ext2_fs.h>
#include <fs.h>

#define FS_TYPE			EXT2FS_NAME
#define FS_NAME_FILE		"EXT2"
#define FS_BUNDLE_NAME		"ext2fs.kext"
#define FS_KEXT_DIR			"/System/Library/Extensions/ext2fs.kext"
#define FS_KMOD_DIR			"/System/Library/Extensions/ext2fs.kext/Contents/MacOS/ext2fs"
#define RAWDEV_PREFIX		"/dev/r"
#define BLOCKDEV_PREFIX		"/dev/"
#define MOUNT_COMMAND		"/sbin/mount"
#define UMOUNT_COMMAND		"/sbin/umount"
#define KEXTLOAD_COMMAND	"/sbin/kextload"
#define KMODLOAD_COMMAND	"/sbin/kmodload"
#define READWRITE_OPT		"-w"
#define READONLY_OPT		"-r"
#define SUID_OPT			"suid"
#define NOSUID_OPT			"nosuid"
#define DEV_OPT				"dev"
#define NODEV_OPT			"nodev"
#define LABEL_LENGTH		11
#define MAX_DOS_BLOCKSIZE	2048

#define FSUC_LABEL		'n'

#define UNKNOWN_LABEL		"Unlabeled"

#define	DEVICE_SUID			"suid"
#define	DEVICE_NOSUID		"nosuid"

#define	DEVICE_DEV			"dev"
#define	DEVICE_NODEV		"nodev"

/* globals */
const char	*progname;	/* our program name, from argv[0] */
int		debug;	/* use -D to enable debug printfs */

/*
 * The following code is re-usable for all FS_util programs
 */
void usage(void);

static int fs_probe(char *devpath, int removable, int writable);
static int fs_mount(char *devpath, char *mount_point, int removable, 
	int writable, int suid, int dev);
static int fs_unmount(char *devpath);
#if 0
static int fs_label(char *devpath, char *volName);
static void fs_set_label_file(char *labelPtr);
#endif

static int safe_open(char *path, int flags, mode_t mode);
static void safe_read(int fd, char *buf, int nbytes, off_t off);
static void safe_close(int fd);
#if 0
static void safe_write(int fd, char *data, int len, off_t off);
#endif
static void safe_execv(char *args[]);
#if 0
static void safe_unlink(char *path);
#endif

#ifdef DEBUG
static void report_exit_code(int ret);
#endif

extern int checkLoadable();
#if 0
static int oklabel(const char *src);
static void mklabel(u_int8_t *dest, const char *src);
#endif

int ret = 0;
char	diskLabel[LABEL_LENGTH + 1];


void usage()
{
        fprintf(stderr, "usage: %s action_arg device_arg [mount_point_arg] [Flags]\n", progname);
        fprintf(stderr, "action_arg:\n");
        fprintf(stderr, "       -%c (Probe)\n", FSUC_PROBE);
        fprintf(stderr, "       -%c (Mount)\n", FSUC_MOUNT);
        fprintf(stderr, "       -%c (Unmount)\n", FSUC_UNMOUNT);
        fprintf(stderr, "       -%c name\n", 'n');
    fprintf(stderr, "device_arg:\n");
    fprintf(stderr, "       device we are acting upon (for example, 'disk0s2')\n");
    fprintf(stderr, "mount_point_arg:\n");
    fprintf(stderr, "       required for Mount and Force Mount \n");
    fprintf(stderr, "Flags:\n");
    fprintf(stderr, "       required for Mount, Force Mount and Probe\n");
    fprintf(stderr, "       indicates removable or fixed (for example 'fixed')\n");
    fprintf(stderr, "       indicates readonly or writable (for example 'readonly')\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "		%s -p disk0s2 fixed writable\n", progname);
    fprintf(stderr, "		%s -m disk0s2 /my/hfs removable readonly\n", progname);
        exit(FSUR_INVAL);
}

int main(int argc, char **argv)
{
    char		rawdevpath[MAXPATHLEN];
    char		blockdevpath[MAXPATHLEN];
    char		opt;
    struct stat	sb;
    int			ret = FSUR_INVAL;


    /* save & strip off program name */
    progname = argv[0];
    argc--;
    argv++;

    /* secret debug flag - must be 1st flag */
    debug = (argc > 0 && !strcmp(argv[0], "-D"));
    if (debug) { /* strip off debug flag argument */
        argc--;
        argv++;
    }

    if (argc < 2 || argv[0][0] != '-')
        usage();
    opt = argv[0][1];
    if (opt != FSUC_PROBE && opt != FSUC_MOUNT && opt != FSUC_UNMOUNT && opt != FSUC_LABEL)
        usage(); /* Not supported action */
    if ((opt == FSUC_MOUNT || opt == FSUC_UNMOUNT || opt == FSUC_LABEL) && argc < 3)
        usage(); /* mountpoint arg missing! */

    sprintf(rawdevpath, "%s%s", RAWDEV_PREFIX, argv[1]);
    if (stat(rawdevpath, &sb) != 0) {
        fprintf(stderr, "%s: stat %s failed, %s\n", progname, rawdevpath,
                strerror(errno));
        exit(FSUR_INVAL);
    }

    sprintf(blockdevpath, "%s%s", BLOCKDEV_PREFIX, argv[1]);
    if (stat(blockdevpath, &sb) != 0) {
        fprintf(stderr, "%s: stat %s failed, %s\n", progname, blockdevpath,
                strerror(errno));
        exit(FSUR_INVAL);
    }

    switch (opt) {
        case FSUC_PROBE: {
            if (argc != 4)
                usage();
            ret = fs_probe(rawdevpath,
                        strcmp(argv[2], DEVICE_FIXED),
                        strcmp(argv[3], DEVICE_READONLY));
            break;
        }

        case FSUC_MOUNT:
        case FSUC_MOUNT_FORCE:
            if (argc != 7)
                usage();
            if (strcmp(argv[3], DEVICE_FIXED) && strcmp(argv[3], DEVICE_REMOVABLE)) {
                printf("ext2fs.util: ERROR: unrecognized flag (removable/fixed) argv[%d]='%s'\n",3,argv[3]);
                usage();
            }
                if (strcmp(argv[4], DEVICE_READONLY) && strcmp(argv[4], DEVICE_WRITABLE)) {
                    printf("ext2fs.util: ERROR: unrecognized flag (readonly/writable) argv[%d]='%s'\n",4,argv[4]);
                    usage();
                }
                if (strcmp(argv[5], DEVICE_SUID) && strcmp(argv[5], DEVICE_NOSUID)) {
                    printf("ext2fs.util: ERROR: unrecognized flag (suid/nosuid) argv[%d]='%s'\n",5,argv[5]);
                    usage();
                }
                if (strcmp(argv[6], DEVICE_DEV) && strcmp(argv[6], DEVICE_NODEV)) {
                    printf("ext2fs.util: ERROR: unrecognized flag (dev/nodev) argv[%d]='%s'\n",6,argv[6]);
                    usage();
                }
                ret = fs_mount(blockdevpath,
                            argv[2],
                            strcmp(argv[3], DEVICE_FIXED),
                            strcmp(argv[4], DEVICE_READONLY),
                            strcmp(argv[5], DEVICE_NOSUID),
                            strcmp(argv[6], DEVICE_NODEV));
            break;
        case FSUC_UNMOUNT:
            ret = fs_unmount(rawdevpath);
            break;
        case FSUC_LABEL:
            #if 0
            ret = fs_label(rawdevpath, argv[2]);
            #else
            ret = ENOTSUP;
            #endif
            break;
        default:
            usage();
    }

    #ifdef DEBUG
    report_exit_code(ret);
    #endif
    exit(ret);

    return(ret);
}

static int fs_mount(char *devpath, char *mount_point, int removable, int writable, int suid, int dev) {
    const char *kextargs[] = {KEXTLOAD_COMMAND, FS_KEXT_DIR, NULL};
    const char *mountargs[] = {MOUNT_COMMAND, READWRITE_OPT, "-o", SUID_OPT, "-o",
        DEV_OPT, "-t", FS_TYPE, devpath, mount_point, NULL};

    if (! writable)
        mountargs[1] = READONLY_OPT;

    if (! suid)
        mountargs[3] = NOSUID_OPT;

    if (! dev)
        mountargs[5] = NODEV_OPT;

    if (checkLoadable())
        safe_execv(kextargs); /* better here than in mount_udf */
    safe_execv(mountargs);
    ret = FSUR_IO_SUCCESS;

    return ret;
}

static int fs_unmount(char *devpath) {
        const char *umountargs[] = {UMOUNT_COMMAND, devpath, NULL};

        safe_execv(umountargs);
        return(FSUR_IO_SUCCESS);
}

#if 0
/*
 * Check a volume label.
 */
static int
oklabel(const char *src)
{
    int c, i;

    for (i = 0, c = 0; i <= 11; i++) {
        c = (u_char)*src++;
        if (c < ' ' + !i || strchr("\"*+,./:;<=>?[\\]|", c))
            break;
    }
    return i && !c;
}

/*
 * Make a volume label.
 */
static void
mklabel(u_int8_t *dest, const char *src)
{
    int c, i;

    for (i = 0; i < 11; i++) {
	c = *src ? toupper(*src++) : ' ';
	*dest++ = !i && c == '\xe5' ? 5 : c;
    }
}
#endif

static int
safe_open(char *path, int flags, mode_t mode)
{
	int fd = open(path, flags, mode);

	if (fd < 0) {
		fprintf(stderr, "%s: open %s failed, %s\n", progname, path,
			strerror(errno));
		exit(FSUR_IO_FAIL);
	}
	return(fd);
}


static void
safe_close(int fd)
{
	if (close(fd)) {
		fprintf(stderr, "%s: safe_close failed, %s\n", progname,
			strerror(errno));
		exit(FSUR_IO_FAIL);
	}
}

void
safe_execv(char *args[])
{
	int		pid;
	union wait	status;

	pid = fork();
	if (pid == 0) {
		(void)execv(args[0], args);
		fprintf(stderr, "%s: execv %s failed, %s\n", progname, args[0],
			strerror(errno));
		exit(FSUR_IO_FAIL);
	}
	if (pid == -1) {
		fprintf(stderr, "%s: fork failed, %s\n", progname,
			strerror(errno));
		exit(FSUR_IO_FAIL);
	}
	if (wait4(pid, (int *)&status, 0, NULL) != pid) {
		fprintf(stderr, "%s: BUG executing %s command\n", progname,
			args[0]);
		exit(FSUR_IO_FAIL);
	} else if (!WIFEXITED(status)) {
		fprintf(stderr, "%s: %s command aborted by signal %d\n",
			progname, args[0], WTERMSIG(status));
		exit(FSUR_IO_FAIL);
	} else if (WEXITSTATUS(status)) {
		fprintf(stderr, "%s: %s command failed, exit status %d: %s\n",
			progname, args[0], WEXITSTATUS(status),
			strerror(WEXITSTATUS(status)));
		exit(FSUR_IO_FAIL);
	}
}

#if 0
static void
safe_unlink(char *path)
{
	if (unlink(path) && errno != ENOENT) {
		fprintf(stderr, "%s: unlink %s failed, %s\n", progname, path,
			strerror(errno));
		exit(FSUR_IO_FAIL);
	}
}
#endif


static void
safe_read(int fd, char *buf, int nbytes, off_t off)
{
	if (lseek(fd, off, SEEK_SET) == -1) {
		fprintf(stderr, "%s: device seek error @ %qu, %s\n", progname,
			off, strerror(errno));
		exit(FSUR_IO_FAIL);
	}
	if (read(fd, buf, nbytes) != nbytes) {
		fprintf(stderr, "%s: device safe_read error @ %qu, %s\n", progname,
			off, strerror(errno));
		exit(FSUR_IO_FAIL);
	}
}

#if 0
void
safe_write(int fd, char *buf, int nbytes, off_t off)
{
        if (lseek(fd, off, SEEK_SET) == -1) {
                fprintf(stderr, "%s: device seek error @ %qu, %s\n", progname,
                        off, strerror(errno));
                exit(FSUR_IO_FAIL);
        }
        if (write(fd, buf, nbytes) != nbytes) {
                fprintf(stderr, "%s: write failed, %s\n", progname,
                        strerror(errno));
                exit(FSUR_IO_FAIL);
        }
}
#endif

#ifdef DEBUG
static void
report_exit_code(int ret)
{
    printf("...ret = %d\n", ret);

    switch (ret) {
    case FSUR_RECOGNIZED:
	printf("File system recognized; a mount is possible.\n");
	break;
    case FSUR_UNRECOGNIZED:
	printf("File system unrecognized; a mount is not possible.\n");
	break;
    case FSUR_IO_SUCCESS:
	printf("Mount, unmount, or repair succeeded.\n");
	break;
    case FSUR_IO_FAIL:
	printf("Unrecoverable I/O error.\n");
	break;
    case FSUR_IO_UNCLEAN:
	printf("Mount failed; file system is not clean.\n");
	break;
    case FSUR_INVAL:
	printf("Invalid argument.\n");
	break;
    case FSUR_LOADERR:
	printf("kern_loader error.\n");
	break;
    case FSUR_INITRECOGNIZED:
	printf("File system recognized; initialization is possible.\n");
	break;
    }
}
#endif

/*
 * Begin Filesystem-specific code
 */
static int fs_probe(char *devpath, int removable, int writable)
{
   char buf[SBSIZE];
   struct ext2_super_block *sbp;
   int fd;
   
   fd = safe_open(devpath, O_RDONLY, 0);
   
   /* Read the superblock */
   safe_read(fd, buf, SBSIZE, SBLOCK);
   
   sbp = (struct ext2_super_block*)buf;
   
   safe_close(fd);
   
   if (EXT2_SUPER_MAGIC != sbp->s_magic) {
      return (EINVAL);
   }

   return(FSUR_RECOGNIZED);
}

#if 0
static int fs_label(char *devpath, char *volName)
{
}
#endif
