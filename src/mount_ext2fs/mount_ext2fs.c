/*-
 * Copyright (c) 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef lint
static char copyright[] __attribute__ ((unused)) =
"@(#) Copyright (c) 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
/*
static char sccsid[] = "@(#)mount_lfs.c	8.3 (Berkeley) 3/27/94";
*/
static const char rcsid[] __attribute__ ((unused)) =
  "$FreeBSD: src/sbin/mount_ext2fs/mount_ext2fs.c,v 1.15 2002/08/13 16:06:14 mux Exp $";
#endif /* not lint */

#if defined(APPLE) && !defined(lint)
static const char whatid[] __attribute__ ((unused)) =
"@(#)Revision: $Revision$ Built: " __DATE__ __TIME__;
#endif

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/uio.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "mntopts.h"

#ifdef APPLE
#include "ext2_apple.h"

/* KEXT support */
int checkLoadable();
int load_kmod();

/* Equiv. to ufs_args */
struct ext2_args {
	char	*fspec;			/* block special device to mount */
	struct	export_args export;	/* network export information */
};

#define __dead2 __dead
#endif

struct mntopt mopts[] = {
	MOPT_STDOPTS,
	MOPT_FORCE,
	MOPT_SYNC,
	MOPT_UPDATE,
	{ NULL }
};

static void	usage(void) __dead2;

int
main(argc, argv)
	int argc;
	char *argv[];
{
	#ifndef APPLE
   struct iovec iov[6];
   #else
   struct ext2_args args;
   #endif
	int ch, mntflags;
	char *fs_name, *fspec, mntpath[MAXPATHLEN];
   
	mntflags = 0;
	while ((ch = getopt(argc, argv, "o:")) != -1)
		switch (ch) {
		case 'o':
			getmntopts(optarg, mopts, &mntflags, 0);
			break;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (argc != 2)
		usage();

	fspec = argv[0];	/* the name of the device file */
	fs_name = argv[1];	/* the mount point */

	/*
	 * Resolve the mountpoint with realpath(3) and remove unnecessary
	 * slashes from the devicename if there are any.
	 */
	(void)checkpath(fs_name, mntpath);
	(void)rmslashes(fspec, fspec);

#ifndef APPLE
	iov[0].iov_base = "fstype";
	iov[0].iov_len = sizeof("fstype");
	iov[1].iov_base = "ext2fs";
	iov[1].iov_len = strlen(iov[1].iov_base) + 1;
	iov[2].iov_base = "fspath";
	iov[2].iov_len = sizeof("fspath");
	iov[3].iov_base = mntpath;
	iov[3].iov_len = strlen(mntpath) + 1;
	iov[4].iov_base = "from";
	iov[4].iov_len = sizeof("from");
	iov[5].iov_base = fspec;
	iov[5].iov_len = strlen(fspec) + 1;
	if (nmount(iov, 6, mntflags) < 0)
		err(EX_OSERR, "%s", fspec);
#else
   args.fspec = fspec;
   args.export.ex_root = 0;
   if (mntflags & MNT_RDONLY)
		args.export.ex_flags = MNT_EXRDONLY;
	else
		args.export.ex_flags = 0;
   
   /* Force NODEV for now. */
   /* mntflags |= MNT_NODEV; */
   
   if (checkLoadable()) {		/* Is it already loaded? */
      if (load_kmod())		/* Load it in */
         errx(EX_OSERR, EXT2FS_NAME " filesystem is not available");
   }
   if (mount(EXT2FS_NAME, mntpath, mntflags, &args) < 0)
		err(EX_OSERR, "%s on %s", args.fspec, mntpath);
#endif
	exit(0);
}

void
usage()
{
	(void)fprintf(stderr,
		"usage: mount_ext2fs [-o options] special node\n");
	exit(EX_USAGE);
}
