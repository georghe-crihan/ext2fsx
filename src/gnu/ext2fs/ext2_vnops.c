/*
 *  modified for EXT2FS support in Lites 1.1
 *
 *  Aug 1995, Godmar Back (gback@cs.utah.edu)
 *  University of Utah, Department of Computer Science
 */
/*
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 *
 *	@(#)ufs_vnops.c	8.7 (Berkeley) 2/3/94
 *	@(#)ufs_vnops.c 8.27 (Berkeley) 5/27/95
 * $FreeBSD: src/sys/gnu/ext2fs/ext2_vnops.c,v 1.75 2003/01/04 08:47:19 phk Exp $
 */
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

/* Use vwhatid so we don't conflict with whatid in ext2_readwrite.c. */
static const char vwhatid[] __attribute__ ((unused)) =
"@(#) $Id$";

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/resourcevar.h>
#include <sys/kernel.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/mount.h>
#include <sys/unistd.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <sys/namei.h>
#include <sys/conf.h>
#include <sys/file.h>

#include <sys/signalvar.h>
#include <ufs/ufs/dir.h>

#ifdef APPLE
#include <vfs/vfs_support.h>

#include <xnu/bsd/miscfs/fifofs/fifo.h>
#include "ext2_apple.h"
#endif /* APPLE */

#include <gnu/ext2fs/inode.h>
#include <gnu/ext2fs/ext2_mount.h>
#include <gnu/ext2fs/ext2_fs_sb.h>
#include <gnu/ext2fs/fs.h>
#include <gnu/ext2fs/ext2_extern.h>
#include <gnu/ext2fs/ext2_fs.h>

#include <kern/ext2_lockf.h>

#define vop_kqfilter_args vop_kqfilt_add_args
#define vop_kqfilter_desc vop_kqfilt_add_desc
#define vop_kqfilter vop_kqfilt_add

static int ext2_makeinode(int mode, struct vnode *, struct vnode **, struct componentname *);

static int ext2_access(struct vop_access_args *);
static int ext2_advlock(struct vop_advlock_args *);
static int ext2_chmod(struct vnode *, int, struct ucred *, struct thread *);
static int ext2_chown(struct vnode *, uid_t, gid_t, struct ucred *,
    struct thread *);
static int ext2_close(struct vop_close_args *);
static int ext2_create(struct vop_create_args *);
static int ext2_fsync(struct vop_fsync_args *);
static int ext2_getattr(struct vop_getattr_args *);
#ifdef EXT_KNOTE
static int ext2_kqfilter(struct vop_kqfilter_args *ap);
#endif
static int ext2_link(struct vop_link_args *);
static int ext2_mkdir(struct vop_mkdir_args *);
static int ext2_mknod(struct vop_mknod_args *);
static int ext2_open(struct vop_open_args *);
static int ext2_pathconf(struct vop_pathconf_args *);
static int ext2_print(struct vop_print_args *);
static int ext2_read(struct vop_read_args *);
static int ext2_readlink(struct vop_readlink_args *);
static int ext2_remove(struct vop_remove_args *);
static int ext2_rename(struct vop_rename_args *);
static int ext2_rmdir(struct vop_rmdir_args *);
static int ext2_setattr(struct vop_setattr_args *);
static int ext2_strategy(struct vop_strategy_args *);
static int ext2_symlink(struct vop_symlink_args *);
static int ext2_write(struct vop_write_args *);
static int ext2fifo_close(struct vop_close_args *);
#ifdef EXT_KNOTE
static int ext2fifo_kqfilter(struct vop_kqfilter_args *);
#endif
static int ext2fifo_read(struct vop_read_args *);
static int ext2fifo_write(struct vop_write_args *);
static int ext2spec_close(struct vop_close_args *);
static int ext2spec_read(struct vop_read_args *);
static int ext2spec_write(struct vop_write_args *);
#ifdef EXT_KNOTE
static int filt_ext2read(struct knote *kn, long hint);
static int filt_ext2write(struct knote *kn, long hint);
static int filt_ext2vnode(struct knote *kn, long hint);
static void filt_ext2detach(struct knote *kn);
#endif

static int ext2fs_truncate (struct vop_truncate_args *);

#define vop_defaultop vn_default_error
#define spec_vnoperate vn_default_error
#define fifo_vnoperate vn_default_error

#define vfs_cache_lookup cache_lookup

extern int (**fifo_vnodeop_p)(void *); /* From kernel */

/* Global vfs data structures for ext2. */
vop_t **ext2_vnodeop_p;
static struct vnodeopv_entry_desc ext2_vnodeop_entries[] = {
	{ &vop_default_desc,		(vop_t *) vop_defaultop },
	{ &vop_access_desc,		(vop_t *) ext2_access },
	{ &vop_advlock_desc,		(vop_t *) ext2_advlock },
	{ &vop_bmap_desc,		(vop_t *) ext2_bmap },
	{ &vop_cachedlookup_desc,	(vop_t *) ext2_lookup },
	{ &vop_close_desc,		(vop_t *) ext2_close },
	{ &vop_create_desc,		(vop_t *) ext2_create },
	{ &vop_fsync_desc,		(vop_t *) ext2_fsync },
	{ &vop_getattr_desc,		(vop_t *) ext2_getattr },
	{ &vop_inactive_desc,		(vop_t *) ext2_inactive },
	{ &vop_link_desc,		(vop_t *) ext2_link },
	{ &vop_mkdir_desc,		(vop_t *) ext2_mkdir },
	{ &vop_mknod_desc,		(vop_t *) ext2_mknod },
	{ &vop_open_desc,		(vop_t *) ext2_open },
	{ &vop_pathconf_desc,		(vop_t *) ext2_pathconf },
   #ifdef EXT_KNOTE
	{ &vop_kqfilter_desc,		(vop_t *) ext2_kqfilter },
   #endif
	{ &vop_print_desc,		(vop_t *) ext2_print },
	{ &vop_read_desc,		(vop_t *) ext2_read },
	{ &vop_readdir_desc,		(vop_t *) ext2_readdir },
	{ &vop_readlink_desc,		(vop_t *) ext2_readlink },
	{ &vop_reallocblks_desc,	(vop_t *) ext2_reallocblks },
	{ &vop_reclaim_desc,		(vop_t *) ext2_reclaim },
	{ &vop_remove_desc,		(vop_t *) ext2_remove },
	{ &vop_rename_desc,		(vop_t *) ext2_rename },
	{ &vop_rmdir_desc,		(vop_t *) ext2_rmdir },
	{ &vop_setattr_desc,		(vop_t *) ext2_setattr },
	{ &vop_strategy_desc,		(vop_t *) ext2_strategy },
	{ &vop_symlink_desc,		(vop_t *) ext2_symlink },
	{ &vop_write_desc,		(vop_t *) ext2_write },
   
   { &vop_lookup_desc,	(vop_t *) ext2_cache_lookup },
   { &vop_truncate_desc, (vop_t *) ext2fs_truncate },		/* truncate */
   { &vop_lock_desc, (vop_t *)ext2_lock },
	{ &vop_unlock_desc, (vop_t *)ext2_unlock },
   { &vop_islocked_desc, (vop_t *)ext2_islocked },
   { &vop_abortop_desc, (vop_t *)ext2_abortop },
   { &vop_pagein_desc,			(vop_t *) ext2_pagein },
	{ &vop_pageout_desc,		(vop_t *) ext2_pageout },
	{ &vop_blktooff_desc,		(vop_t *) ext2_blktooff },
	{ &vop_offtoblk_desc,		(vop_t *) ext2_offtoblk },
  	{ &vop_cmap_desc,			(vop_t *) ext2_cmap },
   { &vop_mmap_desc,       (vop_t *) ext2_mmap },		/* mmap */
   { &vop_copyfile_desc, (vop_t *) err_copyfile },		/* copyfile */
   { &vop_bwrite_desc, (vop_t *)vn_bwrite },
   /* { &vop_update_desc, (vop_t *) ext2_update }, */
   { &vop_ioctl_desc, (vop_t *)ext2_ioctl },
   { &vop_getattrlist_desc,	(vop_t *) ext2_getattrlist },
   { &vop_setattrlist_desc,	(vop_t *) ext2_setattrlist },
	{ NULL, NULL }
};
__private_extern__ struct vnodeopv_desc ext2fs_vnodeop_opv_desc =
	{ &ext2_vnodeop_p, ext2_vnodeop_entries };

vop_t **ext2_specop_p;
static struct vnodeopv_entry_desc ext2_specop_entries[] = {
	{ &vop_default_desc,		(vop_t *) spec_vnoperate },
	{ &vop_access_desc,		(vop_t *) ext2_access },
	{ &vop_close_desc,		(vop_t *) ext2spec_close },
	{ &vop_fsync_desc,		(vop_t *) ext2_fsync },
	{ &vop_getattr_desc,		(vop_t *) ext2_getattr },
	{ &vop_inactive_desc,		(vop_t *) ext2_inactive },
	{ &vop_print_desc,		(vop_t *) ext2_print },
	{ &vop_read_desc,		(vop_t *) ext2spec_read },
	{ &vop_reclaim_desc,		(vop_t *) ext2_reclaim },
	{ &vop_setattr_desc,		(vop_t *) ext2_setattr },
	{ &vop_write_desc,		(vop_t *) ext2spec_write },
   
	{ &vop_advlock_desc,		(vop_t *) spec_advlock },
	{ &vop_bmap_desc,		(vop_t *) spec_bmap },
	{ &vop_lookup_desc,	(vop_t *) spec_lookup },
	{ &vop_create_desc,		(vop_t *) spec_create },
	{ &vop_link_desc,		(vop_t *) spec_link },
	{ &vop_lookup_desc,		(vop_t *) spec_lookup },
	{ &vop_mkdir_desc,		(vop_t *) spec_mkdir },
	{ &vop_mknod_desc,		(vop_t *) spec_mknod },
	{ &vop_open_desc,		(vop_t *) spec_open },
	{ &vop_pathconf_desc,		(vop_t *) spec_pathconf },
	{ &vop_readdir_desc,		(vop_t *) spec_readdir },
	{ &vop_readlink_desc,		(vop_t *) spec_readlink },
	{ &vop_reallocblks_desc,	(vop_t *) spec_reallocblks },
	{ &vop_remove_desc,		(vop_t *) spec_remove },
	{ &vop_rename_desc,		(vop_t *) spec_rename },
	{ &vop_rmdir_desc,		(vop_t *) spec_rmdir },
	{ &vop_strategy_desc,		(vop_t *) spec_strategy },
	{ &vop_symlink_desc,		(vop_t *) spec_symlink },
   { &vop_truncate_desc, (vop_t *) spec_truncate },		/* truncate */
   { &vop_lock_desc, (vop_t *)ext2_lock },
	{ &vop_unlock_desc, (vop_t *)ext2_unlock },
   { &vop_islocked_desc, (vop_t *)ext2_islocked },
   { &vop_abortop_desc, (vop_t *)spec_abortop },
   { &vop_pagein_desc,			(vop_t *) ext2_pagein },
	{ &vop_pageout_desc,		(vop_t *) ext2_pageout },
	{ &vop_blktooff_desc,		(vop_t *) ext2_blktooff },
	{ &vop_offtoblk_desc,		(vop_t *) ext2_offtoblk },
  	{ &vop_cmap_desc,			(vop_t *) spec_cmap },
   { &vop_mmap_desc,       (vop_t *) spec_mmap },		/* mmap */
   { &vop_copyfile_desc, (vop_t *) err_copyfile },		/* copyfile */
   { &vop_bwrite_desc, (vop_t *)vn_bwrite },
   /* { &vop_update_desc, (vop_t *) ext2_update }, */
   { &vop_ioctl_desc, (vop_t *)spec_ioctl },
	{ NULL, NULL }
};
__private_extern__ struct vnodeopv_desc ext2fs_specop_opv_desc =
	{ &ext2_specop_p, ext2_specop_entries };

vop_t **ext2_fifoop_p;
static struct vnodeopv_entry_desc ext2_fifoop_entries[] = {
	{ &vop_default_desc,		(vop_t *) fifo_vnoperate },
	{ &vop_access_desc,		(vop_t *) ext2_access },
	{ &vop_close_desc,		(vop_t *) ext2fifo_close },
	{ &vop_fsync_desc,		(vop_t *) ext2_fsync },
	{ &vop_getattr_desc,		(vop_t *) ext2_getattr },
	{ &vop_inactive_desc,		(vop_t *) ext2_inactive },
   #ifdef EXT_KNOTE
	{ &vop_kqfilter_desc,		(vop_t *) ext2fifo_kqfilter },
   #endif
	{ &vop_print_desc,		(vop_t *) ext2_print },
	{ &vop_read_desc,		(vop_t *) ext2fifo_read },
	{ &vop_reclaim_desc,		(vop_t *) ext2_reclaim },
	{ &vop_setattr_desc,		(vop_t *) ext2_setattr },
	{ &vop_write_desc,		(vop_t *) ext2fifo_write },
   
	{ &vop_advlock_desc,		(vop_t *) fifo_advlock },
	{ &vop_bmap_desc,		(vop_t *) fifo_bmap },
	{ &vop_lookup_desc,	(vop_t *) fifo_lookup },
	{ &vop_create_desc,		(vop_t *) fifo_create },
	{ &vop_link_desc,		(vop_t *) fifo_link },
	{ &vop_lookup_desc,		(vop_t *) fifo_lookup },
	{ &vop_mkdir_desc,		(vop_t *) fifo_mkdir },
	{ &vop_mknod_desc,		(vop_t *) fifo_mknod },
	{ &vop_open_desc,		(vop_t *) fifo_open },
	{ &vop_pathconf_desc,		(vop_t *) fifo_pathconf },
	{ &vop_readdir_desc,		(vop_t *) fifo_readdir },
	{ &vop_readlink_desc,		(vop_t *) fifo_readlink },
	{ &vop_reallocblks_desc,	(vop_t *) fifo_reallocblks },
	{ &vop_remove_desc,		(vop_t *) fifo_remove },
	{ &vop_rename_desc,		(vop_t *) fifo_rename },
	{ &vop_rmdir_desc,		(vop_t *) fifo_rmdir },
	{ &vop_strategy_desc,		(vop_t *) fifo_strategy },
	{ &vop_symlink_desc,		(vop_t *) fifo_symlink },
   { &vop_truncate_desc, (vop_t *) fifo_truncate },		/* truncate */
   { &vop_lock_desc, (vop_t *)ext2_lock },
	{ &vop_unlock_desc, (vop_t *)ext2_unlock },
   { &vop_islocked_desc, (vop_t *)ext2_islocked },
   { &vop_abortop_desc, (vop_t *)fifo_abortop },
   { &vop_pagein_desc,			(vop_t *) ext2_pagein },
	{ &vop_pageout_desc,		(vop_t *) ext2_pageout },
	{ &vop_blktooff_desc,		(vop_t *) ext2_blktooff },
	{ &vop_offtoblk_desc,		(vop_t *) ext2_offtoblk },
  	{ &vop_cmap_desc,			(vop_t *) ext2_cmap },
   { &vop_mmap_desc,       (vop_t *) fifo_mmap },		/* mmap */
   { &vop_copyfile_desc, (vop_t *) err_copyfile },		/* copyfile */
   { &vop_bwrite_desc, (vop_t *)vn_bwrite },
   /* { &vop_update_desc, (vop_t *) ext2_update }, */
   { &vop_ioctl_desc, (vop_t *)fifo_ioctl },
	{ NULL, NULL }
};
__private_extern__ struct vnodeopv_desc ext2fs_fifoop_opv_desc =
	{ &ext2_fifoop_p, ext2_fifoop_entries };

#include <gnu/ext2fs/ext2_readwrite.c>

union _qcvt {
	int64_t qcvt;
	int32_t val[2];
};
#define SETHIGH(q, h) { \
	union _qcvt tmp; \
	tmp.qcvt = (q); \
	tmp.val[_QUAD_HIGHWORD] = (h); \
	(q) = tmp.qcvt; \
}
#define SETLOW(q, l) { \
	union _qcvt tmp; \
	tmp.qcvt = (q); \
	tmp.val[_QUAD_LOWWORD] = (l); \
	(q) = tmp.qcvt; \
}

/*
 * A virgin directory (no blushing please).
 * Note that the type and namlen fields are reversed relative to ext2.
 * Also, we don't use `struct odirtemplate', since it would just cause
 * endianness problems.
 */
static struct dirtemplate mastertemplate = {
	0, 12, 1, EXT2_FT_DIR, ".",
	0, DIRBLKSIZ - 12, 2, EXT2_FT_DIR, ".."
};
static struct dirtemplate omastertemplate = {
	0, 12, 1, EXT2_FT_UNKNOWN, ".",
	0, DIRBLKSIZ - 12, 2, EXT2_FT_UNKNOWN, ".."
};

void
ext2_itimes(vp)
	struct vnode *vp;
{
	struct inode *ip;
	struct timespec ts;

	ip = VTOI(vp);
   if (ip->i_e2flags & EXT2_NOATIME_FL)
      ip->i_flag &= ~IN_ACCESS;
	if ((ip->i_flag & (IN_ACCESS | IN_CHANGE | IN_UPDATE)) == 0)
		return;
	if ((vp->v_type == VBLK || vp->v_type == VCHR))
		ip->i_flag |= IN_LAZYMOD;
	else
		ip->i_flag |= IN_MODIFIED;
	if ((vp->v_mount->mnt_flag & MNT_RDONLY) == 0) {
		vfs_timestamp(&ts);
		if (ip->i_flag & IN_ACCESS) {
			ip->i_atime = ts.tv_sec;
			ip->i_atimensec = ts.tv_nsec;
		}
		if (ip->i_flag & IN_UPDATE) {
			ip->i_mtime = ts.tv_sec;
			ip->i_mtimensec = ts.tv_nsec;
			ip->i_modrev++;
		}
		if (ip->i_flag & IN_CHANGE) {
			ip->i_ctime = ts.tv_sec;
			ip->i_ctimensec = ts.tv_nsec;
		}
	}
	ip->i_flag &= ~(IN_ACCESS | IN_CHANGE | IN_UPDATE);
}

/*
 * Create a regular file
 */
static int
ext2_create(ap)
	struct vop_create_args /* {
		struct vnode *a_dvp;
		struct vnode **a_vpp;
		struct componentname *a_cnp;
		struct vattr *a_vap;
	} */ *ap;
{
	int error;
   
   ext2_trace_enter();

	error =
	    ext2_makeinode(MAKEIMODE(ap->a_vap->va_type, ap->a_vap->va_mode),
	    ap->a_dvp, ap->a_vpp, ap->a_cnp);
	if (error)
		ext2_trace_return(error);
   VN_KNOTE(ap->a_dvp, NOTE_WRITE);
	return (0);
}

/*
 * Open called.
 *
 * Nothing to do.
 */
static int
ext2_open(ap)
	struct vop_open_args /* {
		struct vnode *a_vp;
		int  a_mode;
		struct ucred *a_cred;
		struct thread *a_td;
	} */ *ap;
{

	ext2_trace_enter();
   /*
	 * Files marked append-only must be opened for appending.
	 */
	if ((VTOI(ap->a_vp)->i_flags & APPEND) &&
	    (ap->a_mode & (FWRITE | O_APPEND)) == FWRITE)
		ext2_trace_return(EPERM);
	return (0);
}

/*
 * Close called.
 *
 * Update the times on the inode.
 */
static int
ext2_close(ap)
	struct vop_close_args /* {
		struct vnode *a_vp;
		int  a_fflag;
		struct ucred *a_cred;
		struct thread *a_td;
	} */ *ap;
{
   struct vnode *vp = ap->a_vp;
   struct inode *ip = VTOI(vp);
   
   ext2_trace("inode=%d, name=%s\n", ip->i_number, VNAME(vp));
   
   simple_lock(&vp->v_interlock);
   if ((!UBCISVALID(vp) && vp->v_usecount > 1)
	    || (UBCISVALID(vp) && ubc_isinuse(vp, 1))) {
      ext2_itimes(vp);
   }
   simple_unlock(&vp->v_interlock);
   /*
    * VOP_CLOSE can be called with vp locked (from vclean).
    */
   if (VDIR != vp->v_type && !VOP_ISLOCKED(vp)) {
      enum vtype type = vp->v_type;
      u_long vid = vp->v_id;
      
      dprint_clusters(vp);
      
      if (vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, ap->a_p))
         return (0);
         
      /* vn_lock is a possible preemption point, prevent a 
       * possible race with the likes of umount -f.
       */
      if (vp->v_type != type || vp->v_id != vid
          || ip != VTOI(vp) || !UBCINFOEXISTS(vp)) {
         VOP_UNLOCK(vp, 0, ap->a_p);
         return (0);
      }

      cluster_push(vp);

      VOP_UNLOCK(vp, 0, ap->a_p);
   }
   return (0);
}

static int
ext2_access(ap)
	struct vop_access_args /* {
		struct vnode *a_vp;
		int  a_mode;
		struct ucred *a_cred;
		struct thread *a_td;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	struct inode *ip = VTOI(vp);
	mode_t mode = ap->a_mode;
	int error;
   
   ext2_trace_enter();

	/*
	 * Disallow write attempts on read-only file systems;
	 * unless the file is a socket, fifo, or a block or
	 * character device resident on the file system.
	 */
	if (mode & VWRITE) {
		switch (vp->v_type) {
		case VDIR:
		case VLNK:
		case VREG:
			if (vp->v_mount->mnt_flag & MNT_RDONLY)
				ext2_trace_return(EROFS);
			break;
		default:
			break;
		}
	}

	/* If immutable bit set, nobody gets to write it. */
	if ((mode & VWRITE) && (ip->i_flags & (IMMUTABLE | SF_SNAPSHOT)))
		ext2_trace_return(EPERM);

	error = vaccess(ip->i_mode, ip->i_uid, ip->i_gid,
	    ap->a_mode, ap->a_cred);
	
    /* If mounting w/o perms, then allow anything except root access. */
    if (error && (ip->i_uid > 0) && (vp->v_mount->mnt_flag & MNT_UNKNOWNPERMISSIONS)) {
            error = 0;
    }
    
    ext2_trace_return(error);
}

static int
ext2_getattr(ap)
	struct vop_getattr_args /* {
		struct vnode *a_vp;
		struct vattr *a_vap;
		struct ucred *a_cred;
		struct thread *a_td;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	struct inode *ip = VTOI(vp);
	struct vattr *vap = ap->a_vap;
   int devBlockSize = ip->i_e2fs->s_d_blocksize;
   
   ext2_trace_enter();

	ext2_itimes(vp);
	/*
	 * Copy from inode table
	 */
   vap->va_fsid = vp->v_mount->mnt_stat.f_fsid.val[0];
	vap->va_fileid = ip->i_number;
	vap->va_mode = ip->i_mode & ~IFMT;
    if (0 != ip->i_uid && (vp->v_mount->mnt_flag & MNT_UNKNOWNPERMISSIONS)) {
        /* The Finder needs to see that the perms are correct.
           Otherwise it won't allow access, even though we would. */
        vap->va_mode = DEFFILEMODE;
        if (ip->i_mode & S_IXUSR)
            vap->va_mode |= S_IXUSR|S_IXGRP|S_IXOTH;
    }
	vap->va_nlink = ip->i_nlink;
	vap->va_uid = ip->i_uid;
	vap->va_gid = ip->i_gid;
	vap->va_rdev = ip->i_rdev;
	vap->va_size = ip->i_size;
	vap->va_atime.tv_sec = ip->i_atime;
	vap->va_atime.tv_nsec = ip->i_atimensec;
	vap->va_mtime.tv_sec = ip->i_mtime;
	vap->va_mtime.tv_nsec = ip->i_mtimensec;
	vap->va_ctime.tv_sec = ip->i_ctime;
	vap->va_ctime.tv_nsec = ip->i_ctimensec;
	vap->va_flags = ip->i_flags;
	vap->va_gen = ip->i_gen;
   
   if (vp->v_type == VBLK)
		vap->va_blocksize = BLKDEV_IOSIZE;
	else if (vp->v_type == VCHR) {
		vap->va_blocksize = MAXPHYSIO;
	} else
      vap->va_blocksize = vp->v_mount->mnt_stat.f_iosize;
	vap->va_bytes = dbtob((u_quad_t)ip->i_blocks, devBlockSize);
	vap->va_type = IFTOVT(ip->i_mode);
	vap->va_filerev = ip->i_modrev;
	return (0);
}

/*
 * Set attribute vnode op. called from several syscalls
 */
static int
ext2_setattr(ap)
	struct vop_setattr_args /* {
		struct vnode *a_vp;
		struct vattr *a_vap;
		struct ucred *a_cred;
		struct thread *a_td;
	} */ *ap;
{
	struct vattr *vap = ap->a_vap;
	struct vnode *vp = ap->a_vp;
	struct inode *ip = VTOI(vp);
	struct ucred *cred = ap->a_cred;
	struct thread *td = ap->a_td;
	int error, noperms = 0;
   
   ext2_trace_enter();

	/*
	 * Check for unsettable attributes.
	 */
	if ((vap->va_type != VNON) || (vap->va_nlink != VNOVAL) ||
	    (vap->va_fsid != VNOVAL) || (vap->va_fileid != VNOVAL) ||
	    (vap->va_blocksize != VNOVAL) || (vap->va_rdev != VNOVAL) ||
	    ((int)vap->va_bytes != VNOVAL) || (vap->va_gen != VNOVAL)) {
		return (EINVAL);
	}
    
    if (0 != ip->i_uid && (vp->v_mount->mnt_flag & MNT_UNKNOWNPERMISSIONS))
        noperms = 1;
    
	if (vap->va_flags != VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			ext2_trace_return(EROFS);
      if (cred->cr_uid != ip->i_uid &&
		    (error = suser(cred, &td->p_acflag)))
         ext2_trace_return(error);
      if (cred->cr_uid == 0) {
			if (ip->i_flags
			    & (SF_NOUNLINK | SF_IMMUTABLE | SF_APPEND)) {
            error = securelevel_gt(cred, 0);
				if (error)
					ext2_trace_return(error);
			}
			ip->i_flags = vap->va_flags;
		} else {
			if (ip->i_flags
			    & (SF_NOUNLINK | SF_IMMUTABLE | SF_APPEND) ||
			    (vap->va_flags & UF_SETTABLE) != vap->va_flags)
				ext2_trace_return(EPERM);
			ip->i_flags &= SF_SETTABLE;
			ip->i_flags |= (vap->va_flags & UF_SETTABLE);
		}
		ip->i_flag |= IN_CHANGE;
		if (vap->va_flags & (IMMUTABLE | APPEND)) {
         VN_KNOTE(vp, NOTE_ATTRIB);
			return (0);
      }
	}
	if (ip->i_flags & (IMMUTABLE | APPEND))
		ext2_trace_return(EPERM);
	/*
	 * Go through the fields and update if not VNOVAL.
	 */
	if (vap->va_uid != (uid_t)VNOVAL || vap->va_gid != (gid_t)VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			ext2_trace_return(EROFS);
		if ((error = ext2_chown(vp, vap->va_uid, vap->va_gid, cred,
		    td)) != 0)
			ext2_trace_return(error);
	}
	if (vap->va_size != VNOVAL) {
		/*
		 * Disallow write attempts on read-only file systems;
		 * unless the file is a socket, fifo, or a block or
		 * character device resident on the file system.
		 */
		switch (vp->v_type) {
		case VDIR:
			ext2_trace_return(EISDIR);
		case VLNK:
		case VREG:
			if (vp->v_mount->mnt_flag & MNT_RDONLY)
				ext2_trace_return(EROFS);
			break;
		default:
			break;
		}
		if ((error = ext2_truncate(vp, vap->va_size, 0, cred, td)) != 0)
			ext2_trace_return(error);
	}
	if (vap->va_atime.tv_sec != VNOVAL || vap->va_mtime.tv_sec != VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			ext2_trace_return(EROFS);
		/*
		 * From utimes(2):
		 * If times is NULL, ... The caller must be the owner of
		 * the file, have permission to write the file, or be the
		 * super-user.
		 * If times is non-NULL, ... The caller must be the owner of
		 * the file or be the super-user.
		 */
      if (0 == noperms && cred->cr_uid != ip->i_uid &&
		    (error = suser(cred, &td->p_acflag)) &&
		    ((vap->va_vaflags & VA_UTIMES_NULL) == 0 ||
		    (error = VOP_ACCESS(vp, VWRITE, cred, td))))
			ext2_trace_return(error);
		if (vap->va_atime.tv_sec != VNOVAL)
			ip->i_flag |= IN_ACCESS;
		if (vap->va_mtime.tv_sec != VNOVAL)
			ip->i_flag |= IN_CHANGE | IN_UPDATE;
		ext2_itimes(vp);
		if (vap->va_atime.tv_sec != VNOVAL) {
			ip->i_atime = vap->va_atime.tv_sec;
			ip->i_atimensec = vap->va_atime.tv_nsec;
		}
		if (vap->va_mtime.tv_sec != VNOVAL) {
			ip->i_mtime = vap->va_mtime.tv_sec;
			ip->i_mtimensec = vap->va_mtime.tv_nsec;
		}
		error = ext2_update(vp, 0);
		if (error)
			ext2_trace_return(error);
	}
	error = 0;
	if (vap->va_mode != (mode_t)VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			ext2_trace_return(EROFS);
		error = ext2_chmod(vp, (int)vap->va_mode, cred, td);
	}
	VN_KNOTE(vp, NOTE_ATTRIB);
	ext2_trace_return(error);
}

/*
 * Change the mode on a file.
 * Inode must be locked before calling.
 */
static int
ext2_chmod(vp, mode, cred, td)
	struct vnode *vp;
	int mode;
	struct ucred *cred;
	struct thread *td;
{
	struct inode *ip = VTOI(vp);
	int error, super;
   
   ext2_trace_enter();
   
    super = (error = suser(cred, &td->p_acflag)) ? 0 : 1;
    
    if ((vp->v_mount->mnt_flag & MNT_UNKNOWNPERMISSIONS)
         && !super)
        return (0);
   
   if (cred->cr_uid != ip->i_uid && !super)
      ext2_trace_return(error);
   if (cred->cr_uid) {
		if (vp->v_type != VDIR && (mode & S_ISTXT))
			ext2_trace_return(EFTYPE);
		if (!groupmember(ip->i_gid, cred) && (mode & ISGID))
			ext2_trace_return(EPERM);
	}
	ip->i_mode &= ~ALLPERMS;
	ip->i_mode |= (mode & ALLPERMS);
	ip->i_flag |= IN_CHANGE;
	return (0);
}

/*
 * Perform chown operation on inode ip;
 * inode must be locked prior to call.
 */
static int
ext2_chown(vp, uid, gid, cred, td)
	struct vnode *vp;
	uid_t uid;
	gid_t gid;
	struct ucred *cred;
	struct thread *td;
{
	struct inode *ip = VTOI(vp);
	uid_t ouid;
	gid_t ogid;
	int error = 0, super;
   
   ext2_trace_enter();

	super = (error = suser(cred, &td->p_acflag)) ? 0 : 1;
    
    if ((vp->v_mount->mnt_flag & MNT_UNKNOWNPERMISSIONS)
         && !super)
        return (0);
    
    if (uid == (uid_t)VNOVAL)
		uid = ip->i_uid;
	if (gid == (gid_t)VNOVAL)
		gid = ip->i_gid;
	/*
	 * To change the owner of a file, or change the group of a file
	 * to a group of which we are not a member, the caller must
	 * have privilege.
	 */
	if ((uid != ip->i_uid || 
	    (gid != ip->i_gid && !groupmember(gid, cred))) && !super)
		ext2_trace_return(error);
	ogid = ip->i_gid;
	ouid = ip->i_uid;
	ip->i_gid = gid;
	ip->i_uid = uid;
	ip->i_flag |= IN_CHANGE;
   if (suser(cred, &td->p_acflag) && (ouid != uid || ogid != gid))
		ip->i_mode &= ~(ISUID | ISGID);
	return (0);
}

/*
 * Synch an open file.
 */
/* ARGSUSED */
static int
ext2_fsync(ap)
	struct vop_fsync_args /* {
		struct vnode *a_vp;
		struct ucred *a_cred;
		int a_waitfor;
		struct thread *a_td;
	} */ *ap;
{
    ext2_trace_enter();
   
    /*
	 * Write out any clusters.
	 */
	cluster_push(ap->a_vp);

	/*
	 * Flush all dirty buffers associated with a vnode.
	 */
	ext2_discard_prealloc(VTOI(ap->a_vp));
   
    vop_stdfsync(ap);

	ext2_trace_return(ext2_update(ap->a_vp, ap->a_waitfor == MNT_WAIT));
}

/*
 * Mknod vnode call
 */
/* ARGSUSED */
static int
ext2_mknod(ap)
	struct vop_mknod_args /* {
		struct vnode *a_dvp;
		struct vnode **a_vpp;
		struct componentname *a_cnp;
		struct vattr *a_vap;
	} */ *ap;
{
	struct vattr *vap = ap->a_vap;
	struct vnode **vpp = ap->a_vpp;
	struct inode *ip;
	ino_t ino;
	int error;
   
    ext2_trace_enter();

	error = ext2_makeinode(MAKEIMODE(vap->va_type, vap->va_mode),
	    ap->a_dvp, vpp, ap->a_cnp);
	if (error)
		ext2_trace_return(error);
    VN_KNOTE(ap->a_dvp, NOTE_WRITE);
	ip = VTOI(*vpp);
	ip->i_flag |= IN_ACCESS | IN_CHANGE | IN_UPDATE;
	if (vap->va_rdev != VNOVAL) {
		/*
		 * Want to be able to use this to make badblock
		 * inodes, so don't truncate the dev number.
		 */
		ip->i_rdev = vap->va_rdev;
	}
	/*
	 * Remove inode, then reload it through VFS_VGET so it is
	 * checked to see if it is an alias of an existing entry in
	 * the inode cache.
	 */
	vput(*vpp);
	(*vpp)->v_type = VNON;
	ino = ip->i_number;	/* Save this before vgone() invalidates ip. */
	vgone(*vpp);
   #if 0
	error = VFS_VGET(ap->a_dvp->v_mount, ino, LK_EXCLUSIVE, vpp);
	if (error) {
		*vpp = NULL;
		ext2_trace_return(error);
	}
   #else
    /* lookup will reload the inode for us */
    *vpp = NULL;
   #endif
	return (0);
}

static int
ext2_remove(ap)
	struct vop_remove_args /* {
		struct vnode *a_dvp;
		struct vnode *a_vp;
		struct componentname *a_cnp;
	} */ *ap;
{
	struct inode *ip;
	struct vnode *vp = ap->a_vp;
	struct vnode *dvp = ap->a_dvp;
	int error;
   
   ext2_trace_enter();

	ip = VTOI(vp);
	if ((ip->i_flags & (NOUNLINK | IMMUTABLE | APPEND)) ||
	    (VTOI(dvp)->i_flags & APPEND)) {
		error = EPERM;
		goto out;
	}
   
   if (ap->a_cnp->cn_flags & NODELETEBUSY) {
		/* Caller requested Carbon delete semantics */
		if ((!UBCISVALID(vp) && vp->v_usecount > 1)
		    || (UBCISVALID(vp) && ubc_isinuse(vp, 1))) {
			error = EBUSY;
			goto out;
		}
	}
   
	error = ext2_dirremove(dvp, ap->a_cnp);
	if (error == 0) {
		ip->i_nlink--;
		ip->i_flag |= IN_CHANGE;
      VN_KNOTE(vp, NOTE_DELETE);
      VN_KNOTE(dvp, NOTE_WRITE);
	}
   
   if (dvp != vp)
		VOP_UNLOCK(vp, 0, ap->a_cnp->cn_proc);
   
   /* ufs calls ubc_uncache even for errors, but we follow hfs precedent */
   if (!error)
      (void)ubc_uncache(vp);
   
   vrele(vp);
	vput(dvp);

	ext2_trace_return(error);
   
out:
   if (dvp == vp)
      vrele(vp);
   else
      vput(vp);
   vput(dvp);
   
	ext2_trace_return(error);
}

/*
 * link vnode call
 */
static int
ext2_link(ap)
	struct vop_link_args /* {
		struct vnode *a_tdvp;
		struct vnode *a_vp;
		struct componentname *a_cnp;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	struct vnode *tdvp = ap->a_tdvp;
	struct componentname *cnp = ap->a_cnp;
	struct inode *ip;
   struct proc *p = curproc;
	int error;
   
   ext2_trace_enter();

#ifdef DIAGNOSTIC
	if ((cnp->cn_flags & HASBUF) == 0)
		panic("ext2_link: no name");
#endif
	if (tdvp->v_mount != vp->v_mount) {
      VOP_ABORTOP(tdvp, cnp);
		error = EXDEV;
		goto out;
	}
   if (tdvp != vp && (error = vn_lock(vp, LK_EXCLUSIVE, p))) {
		VOP_ABORTOP(tdvp, cnp);
		goto out;
	}
	ip = VTOI(vp);
	if ((nlink_t)ip->i_nlink >= LINK_MAX) {
      VOP_ABORTOP(tdvp, cnp);
		error = EMLINK;
		goto out1;
	}
	if (ip->i_flags & (IMMUTABLE | APPEND)) {
      VOP_ABORTOP(tdvp, cnp);
		error = EPERM;
		goto out1;
	}
	ip->i_nlink++;
	ip->i_flag |= IN_CHANGE;
	error = ext2_update(vp, 1);
	if (!error)
		error = ext2_direnter(ip, tdvp, cnp);
	if (error) {
		ip->i_nlink--;
		ip->i_flag |= IN_CHANGE;
	}
   FREE_ZONE(cnp->cn_pnbuf, cnp->cn_pnlen, M_NAMEI);
   VN_KNOTE(vp, NOTE_LINK);
   VN_KNOTE(tdvp, NOTE_WRITE);
   
out1:
   if (tdvp != vp)
		VOP_UNLOCK(vp, 0, p);

out:
   vput(tdvp);
	ext2_trace_return(error);
}

/*
 * Rename system call.
 *   See comments in sys/ufs/ufs/ufs_vnops.c
 */
static int
ext2_rename(ap)
	struct vop_rename_args  /* {
		struct vnode *a_fdvp;
		struct vnode *a_fvp;
		struct componentname *a_fcnp;
		struct vnode *a_tdvp;
		struct vnode *a_tvp;
		struct componentname *a_tcnp;
	} */ *ap;
{
	struct vnode *tvp = ap->a_tvp;
	struct vnode *tdvp = ap->a_tdvp;
	struct vnode *fvp = ap->a_fvp;
	struct vnode *fdvp = ap->a_fdvp;
	struct componentname *tcnp = ap->a_tcnp;
	struct componentname *fcnp = ap->a_fcnp;
	struct thread *td = fcnp->cn_thread;
	struct inode *ip, *xp, *dp;
	struct dirtemplate dirbuf;
	int doingdirectory = 0, oldparent = 0, newparent = 0;
	int error = 0;
	u_char namlen;
   
   ext2_trace_enter();

#ifdef DIAGNOSTIC
	if ((tcnp->cn_flags & HASBUF) == 0 ||
	    (fcnp->cn_flags & HASBUF) == 0)
		panic("ext2_rename: no name");
#endif
	/*
	 * Check for cross-device rename.
	 */
	if ((fvp->v_mount != tdvp->v_mount) ||
	    (tvp && (fvp->v_mount != tvp->v_mount))) {
		error = EXDEV;
abortit:
      VOP_ABORTOP(tdvp, tcnp);
		if (tdvp == tvp)
			vrele(tdvp);
		else
			vput(tdvp);
		if (tvp)
			vput(tvp);
      VOP_ABORTOP(fdvp, fcnp);
		vrele(fdvp);
		vrele(fvp);
		ext2_trace_return(error);
	}

	if (tvp && ((VTOI(tvp)->i_flags & (NOUNLINK | IMMUTABLE | APPEND)) ||
	    (VTOI(tdvp)->i_flags & APPEND))) {
		error = EPERM;
		goto abortit;
	}

	/*
	 * Renaming a file to itself has no effect.  The upper layers should
	 * not call us in that case.  Temporarily just warn if they do.
	 */
	if (fvp == tvp) {
		printf("ext2_rename: fvp == tvp (can't happen)\n");
		error = 0;
		goto abortit;
	}

	if ((error = vn_lock(fvp, LK_EXCLUSIVE, td)) != 0)
		goto abortit;
	dp = VTOI(fdvp);
	ip = VTOI(fvp);
 	if (ip->i_nlink >= LINK_MAX) {
 		VOP_UNLOCK(fvp, 0, td);
 		error = EMLINK;
 		goto abortit;
 	}
	if ((ip->i_flags & (NOUNLINK | IMMUTABLE | APPEND))
	    || (dp->i_flags & APPEND)) {
		VOP_UNLOCK(fvp, 0, td);
		error = EPERM;
		goto abortit;
	}
	if ((ip->i_mode & IFMT) == IFDIR) {
		/*
		 * Avoid ".", "..", and aliases of "." for obvious reasons.
		 */
		if ((fcnp->cn_namelen == 1 && fcnp->cn_nameptr[0] == '.') ||
		    dp == ip || ((fcnp->cn_flags | tcnp->cn_flags) & ISDOTDOT) ||
		    (ip->i_flag & IN_RENAME)) {
			VOP_UNLOCK(fvp, 0, td);
			error = EINVAL;
			goto abortit;
		}
		ip->i_flag |= IN_RENAME;
		oldparent = dp->i_number;
		doingdirectory++;
	}
   VN_KNOTE(fdvp, NOTE_WRITE); /* XXX right place? */
	vrele(fdvp);

	/*
	 * When the target exists, both the directory
	 * and target vnodes are returned locked.
	 */
	dp = VTOI(tdvp);
	xp = NULL;
	if (tvp)
		xp = VTOI(tvp);

	/*
	 * 1) Bump link count while we're moving stuff
	 *    around.  If we crash somewhere before
	 *    completing our work, the link count
	 *    may be wrong, but correctable.
	 */
	ip->i_nlink++;
	ip->i_flag |= IN_CHANGE;
	if ((error = ext2_update(fvp, 1)) != 0) {
		VOP_UNLOCK(fvp, 0, td);
		goto bad;
	}

	/*
	 * If ".." must be changed (ie the directory gets a new
	 * parent) then the source directory must not be in the
	 * directory heirarchy above the target, as this would
	 * orphan everything below the source directory. Also
	 * the user must have write permission in the source so
	 * as to be able to change "..". We must repeat the call
	 * to namei, as the parent directory is unlocked by the
	 * call to checkpath().
	 */
	error = VOP_ACCESS(fvp, VWRITE, tcnp->cn_cred, tcnp->cn_thread);
	VOP_UNLOCK(fvp, 0, td);
	if (oldparent != dp->i_number)
		newparent = dp->i_number;
	if (doingdirectory && newparent) {
		if (error)	/* write access check above */
			goto bad;
		if (xp != NULL)
			vput(tvp);
		error = ext2_checkpath(ip, dp, tcnp->cn_cred);
		if (error)
			goto out;
      if ((tcnp->cn_flags & SAVESTART) == 0)
			panic("ext2_rename: lost to startdir");
		VREF(tdvp);
		error = relookup(tdvp, &tvp, tcnp);
		if (error)
			goto out;
		vrele(tdvp);
		dp = VTOI(tdvp);
		xp = NULL;
		if (tvp)
			xp = VTOI(tvp);
	}
	/*
	 * 2) If target doesn't exist, link the target
	 *    to the source and unlink the source.
	 *    Otherwise, rewrite the target directory
	 *    entry to reference the source inode and
	 *    expunge the original entry's existence.
	 */
	if (xp == NULL) {
		if (dp->i_dev != ip->i_dev)
			panic("ext2_rename: EXDEV");
		/*
		 * Account for ".." in new directory.
		 * When source and destination have the same
		 * parent we don't fool with the link count.
		 */
		if (doingdirectory && newparent) {
			if ((nlink_t)dp->i_nlink >= LINK_MAX) {
				error = EMLINK;
				goto bad;
			}
			dp->i_nlink++;
			dp->i_flag |= IN_CHANGE;
			error = ext2_update(tdvp, 1);
			if (error)
				goto bad;
		}
		error = ext2_direnter(ip, tdvp, tcnp);
		if (error) {
			if (doingdirectory && newparent) {
				dp->i_nlink--;
				dp->i_flag |= IN_CHANGE;
				(void)ext2_update(tdvp, 1);
			}
			goto bad;
		}
      VN_KNOTE(tdvp, NOTE_WRITE);
		vput(tdvp);
	} else {
		if (xp->i_dev != dp->i_dev || xp->i_dev != ip->i_dev)
			panic("ext2_rename: EXDEV");
		/*
		 * Short circuit rename(foo, foo).
		 */
		if (xp->i_number == ip->i_number)
			panic("ext2_rename: same file");
		/*
		 * If the parent directory is "sticky", then the user must
		 * own the parent directory, or the destination of the rename,
		 * otherwise the destination may not be changed (except by
		 * root). This implements append-only directories.
		 */
		if ((dp->i_mode & S_ISTXT) && tcnp->cn_cred->cr_uid != 0 &&
		    tcnp->cn_cred->cr_uid != dp->i_uid &&
		    xp->i_uid != tcnp->cn_cred->cr_uid) {
			error = EPERM;
			goto bad;
		}
		/*
		 * Target must be empty if a directory and have no links
		 * to it. Also, ensure source and target are compatible
		 * (both directories, or both not directories).
		 */
		if ((xp->i_mode&IFMT) == IFDIR) {
			if (! ext2_dirempty(xp, dp->i_number, tcnp->cn_cred) || 
			    xp->i_nlink > 2) {
				error = ENOTEMPTY;
				goto bad;
			}
			if (!doingdirectory) {
				error = ENOTDIR;
				goto bad;
			}
			cache_purge(tdvp);
		} else if (doingdirectory) {
			error = EISDIR;
			goto bad;
		}
		error = ext2_dirrewrite(dp, ip, tcnp);
		if (error)
			goto bad;
		/*
		 * If the target directory is in the same
		 * directory as the source directory,
		 * decrement the link count on the parent
		 * of the target directory.
		 */
		 if (doingdirectory && !newparent) {
			dp->i_nlink--;
			dp->i_flag |= IN_CHANGE;
		}
      VN_KNOTE(tdvp, NOTE_WRITE);
		vput(tdvp);
		/*
		 * Adjust the link count of the target to
		 * reflect the dirrewrite above.  If this is
		 * a directory it is empty and there are
		 * no links to it, so we can squash the inode and
		 * any space associated with it.  We disallowed
		 * renaming over top of a directory with links to
		 * it above, as the remaining link would point to
		 * a directory without "." or ".." entries.
		 */
		xp->i_nlink--;
		if (doingdirectory) {
			if (--xp->i_nlink != 0)
				panic("ext2_rename: linked directory");
			error = ext2_truncate(tvp, (off_t)0, IO_SYNC,
			    tcnp->cn_cred, tcnp->cn_thread);
		}
		xp->i_flag |= IN_CHANGE;
      VN_KNOTE(tvp, NOTE_DELETE);
		vput(tvp);
		xp = NULL;
	}

	/*
	 * 3) Unlink the source.
	 */
	fcnp->cn_flags &= ~MODMASK;
	fcnp->cn_flags |= LOCKPARENT | LOCKLEAF;
	VREF(fdvp);
	error = relookup(fdvp, &fvp, fcnp);
	if (error == 0)
		vrele(fdvp);
	if (fvp != NULL) {
		xp = VTOI(fvp);
		dp = VTOI(fdvp);
	} else {
		/*
		 * From name has disappeared.
		 */
		if (doingdirectory)
			panic("ext2_rename: lost dir entry");
		vrele(ap->a_fvp);
		return (0);
	}
	/*
	 * Ensure that the directory entry still exists and has not
	 * changed while the new name has been entered. If the source is
	 * a file then the entry may have been unlinked or renamed. In
	 * either case there is no further work to be done. If the source
	 * is a directory then it cannot have been rmdir'ed; its link
	 * count of three would cause a rmdir to fail with ENOTEMPTY.
	 * The IN_RENAME flag ensures that it cannot be moved by another
	 * rename.
	 */
	if (xp != ip) {
		if (doingdirectory)
			panic("ext2_rename: lost dir entry");
	} else {
		/*
		 * If the source is a directory with a
		 * new parent, the link count of the old
		 * parent directory must be decremented
		 * and ".." set to point to the new parent.
		 */
		if (doingdirectory && newparent) {
			dp->i_nlink--;
			dp->i_flag |= IN_CHANGE;
			error = vn_rdwr(UIO_READ, fvp, (caddr_t)&dirbuf,
				sizeof (struct dirtemplate), (off_t)0,
				UIO_SYSSPACE, IO_NODELOCKED | IO_NOMACCHECK,
				tcnp->cn_cred, (int *)0, (struct proc *)0);
			if (error == 0) {
				/* Like ufs little-endian: */
				namlen = dirbuf.dotdot_type;
				if (namlen != 2 ||
				    dirbuf.dotdot_name[0] != '.' ||
				    dirbuf.dotdot_name[1] != '.') {
					ext2_dirbad(xp, (doff_t)12,
					    "ext2_rename: mangled dir");
				} else {
					dirbuf.dotdot_ino = cpu_to_le32(newparent);
					(void) vn_rdwr(UIO_WRITE, fvp,
					    (caddr_t)&dirbuf,
					    sizeof (struct dirtemplate),
					    (off_t)0, UIO_SYSSPACE,
					    IO_NODELOCKED | IO_SYNC |
					    IO_NOMACCHECK, tcnp->cn_cred,
                   (int *)0, (struct proc *)0);
					cache_purge(fdvp);
				}
			}
		}
		error = ext2_dirremove(fdvp, fcnp);
		if (!error) {
			xp->i_nlink--;
			xp->i_flag |= IN_CHANGE;
		}
		xp->i_flag &= ~IN_RENAME;
	}
   VN_KNOTE(ap->a_fvp, NOTE_RENAME);
	if (dp)
		vput(fdvp);
	if (xp)
		vput(fvp);
	vrele(ap->a_fvp);
	ext2_trace_return(error);

bad:
	if (xp)
		vput(ITOV(xp));
	vput(ITOV(dp));
out:
	if (doingdirectory)
		ip->i_flag &= ~IN_RENAME;
	if (vn_lock(fvp, LK_EXCLUSIVE, td) == 0) {
		ip->i_nlink--;
		ip->i_flag |= IN_CHANGE;
		ip->i_flag &= ~IN_RENAME;
		vput(fvp);
	} else
		vrele(fvp);
	ext2_trace_return(error);
}

/*
 * Mkdir system call
 */
static int
ext2_mkdir(ap)
	struct vop_mkdir_args /* {
		struct vnode *a_dvp;
		struct vnode **a_vpp;
		struct componentname *a_cnp;
		struct vattr *a_vap;
	} */ *ap;
{
	struct vnode *dvp = ap->a_dvp;
	struct vattr *vap = ap->a_vap;
	struct componentname *cnp = ap->a_cnp;
	struct inode *ip, *dp;
	struct vnode *tvp;
	struct dirtemplate dirtemplate, *dtp;
	int error, dmode;
   
   ext2_trace_enter();

#ifdef DIAGNOSTIC
	if ((cnp->cn_flags & HASBUF) == 0)
		panic("ext2_mkdir: no name");
#endif
	dp = VTOI(dvp);
	if ((nlink_t)dp->i_nlink >= LINK_MAX) {
		error = EMLINK;
		goto out;
	}
	dmode = vap->va_mode & 0777;
	dmode |= IFDIR;
	/*
	 * Must simulate part of ext2_makeinode here to acquire the inode,
	 * but not have it entered in the parent directory. The entry is
	 * made later after writing "." and ".." entries.
	 */
	error = ext2_valloc(dvp, dmode, cnp->cn_cred, &tvp);
	if (error)
		goto out;
	ip = VTOI(tvp);
	ip->i_gid = dp->i_gid;
#ifdef SUIDDIR
	{
		/*
		 * if we are hacking owners here, (only do this where told to)
		 * and we are not giving it TOO root, (would subvert quotas)
		 * then go ahead and give it to the other user.
		 * The new directory also inherits the SUID bit. 
		 * If user's UID and dir UID are the same,
		 * 'give it away' so that the SUID is still forced on.
		 */
		if ( (dvp->v_mount->mnt_flag & MNT_SUIDDIR) &&
		   (dp->i_mode & ISUID) && dp->i_uid) {
			dmode |= ISUID;
			ip->i_uid = dp->i_uid;
		} else {
			ip->i_uid = cnp->cn_cred->cr_uid;
		}
	}
#else
	ip->i_uid = cnp->cn_cred->cr_uid;
#endif
	ip->i_flag |= IN_ACCESS | IN_CHANGE | IN_UPDATE;
	ip->i_mode = dmode;
	tvp->v_type = VDIR;	/* Rest init'd in getnewvnode(). */
	ip->i_nlink = 2;
	if (cnp->cn_flags & ISWHITEOUT)
		ip->i_flags |= UF_OPAQUE;
	error = ext2_update(tvp, 1);

	/*
	 * Bump link count in parent directory
	 * to reflect work done below.  Should
	 * be done before reference is created
	 * so reparation is possible if we crash.
	 */
	dp->i_nlink++;
	dp->i_flag |= IN_CHANGE;
	error = ext2_update(dvp, 1);
	if (error)
		goto bad;

	/* Initialize directory with "." and ".." from static template. */
	if (EXT2_HAS_INCOMPAT_FEATURE(ip->i_e2fs,
	    EXT2_FEATURE_INCOMPAT_FILETYPE))
		dtp = &mastertemplate;
	else
		dtp = &omastertemplate;
	dirtemplate = *dtp;
   dirtemplate.dot_reclen = cpu_to_le16(dirtemplate.dot_reclen);
	dirtemplate.dot_ino = cpu_to_le32(ip->i_number);
	dirtemplate.dotdot_ino = cpu_to_le32(dp->i_number);
	/* note that in ext2 DIRBLKSIZ == blocksize, not DEV_BSIZE 
	 * so let's just redefine it - for this function only
	 */
#undef  DIRBLKSIZ 
#define DIRBLKSIZ  VTOI(dvp)->i_e2fs->s_blocksize
	dirtemplate.dotdot_reclen = cpu_to_le16(DIRBLKSIZ - 12);
	error = vn_rdwr(UIO_WRITE, tvp, (caddr_t)&dirtemplate,
	    sizeof (dirtemplate), (off_t)0, UIO_SYSSPACE,
	    IO_NODELOCKED | IO_SYNC | IO_NOMACCHECK, cnp->cn_cred,
       (int *)0, (struct proc *)0);
	if (error) {
		dp->i_nlink--;
		dp->i_flag |= IN_CHANGE;
		goto bad;
	}
	if (DIRBLKSIZ > VFSTOEXT2(dvp->v_mount)->um_mountp->mnt_stat.f_bsize)
		/* XXX should grow with balloc() */
		panic("ext2_mkdir: blksize");
	else {
		ip->i_size = DIRBLKSIZ;
		ip->i_flag |= IN_CHANGE;
	}

	/* Directory set up, now install its entry in the parent directory. */
	error = ext2_direnter(ip, dvp, cnp);
	if (error) {
		dp->i_nlink--;
		dp->i_flag |= IN_CHANGE;
	}
bad:
	/*
	 * No need to do an explicit VOP_TRUNCATE here, vrele will do this
	 * for us because we set the link count to 0.
	 */
	if (error) {
		ip->i_nlink = 0;
		ip->i_flag |= IN_CHANGE;
		vput(tvp);
	} else {
      VN_KNOTE(dvp, NOTE_WRITE | NOTE_LINK);
		*ap->a_vpp = tvp;
   }
out:
   FREE_ZONE(cnp->cn_pnbuf, cnp->cn_pnlen, M_NAMEI);
	vput(dvp);
   
	ext2_trace_return(error);
#undef  DIRBLKSIZ
#define DIRBLKSIZ  DEV_BSIZE
}

/*
 * Rmdir system call.
 */
static int
ext2_rmdir(ap)
	struct vop_rmdir_args /* {
		struct vnode *a_dvp;
		struct vnode *a_vp;
		struct componentname *a_cnp;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	struct vnode *dvp = ap->a_dvp;
	struct componentname *cnp = ap->a_cnp;
	struct thread *td = cnp->cn_thread;
	struct inode *ip, *dp;
	int error;

	ip = VTOI(vp);
	dp = VTOI(dvp);
   
   ext2_trace_enter();
   
   /*
	 * No rmdir "." please.
	 */
	if (dp == ip) {
		vrele(dvp);
		vput(vp);
		ext2_trace_return(EINVAL);
	}

	/*
	 * Verify the directory is empty (and valid).
	 * (Rmdir ".." won't be valid since
	 *  ".." will contain a reference to
	 *  the current directory and thus be
	 *  non-empty.)
	 */
	error = 0;
	if (ip->i_nlink != 2 || !ext2_dirempty(ip, dp->i_number, cnp->cn_cred)) {
		error = ENOTEMPTY;
		goto out;
	}
	if ((dp->i_flags & APPEND)
	    || (ip->i_flags & (NOUNLINK | IMMUTABLE | APPEND))) {
		error = EPERM;
		goto out;
	}
	/*
	 * Delete reference to directory before purging
	 * inode.  If we crash in between, the directory
	 * will be reattached to lost+found,
	 */
	error = ext2_dirremove(dvp, cnp);
	if (error)
		goto out;
   VN_KNOTE(dvp, NOTE_WRITE | NOTE_LINK);
	dp->i_nlink--;
	dp->i_flag |= IN_CHANGE;
	cache_purge(dvp);
   vput(dvp);
	dvp = NULL;
	/*
	 * Truncate inode.  The only stuff left
	 * in the directory is "." and "..".  The
	 * "." reference is inconsequential since
	 * we're quashing it.  The ".." reference
	 * has already been adjusted above.  We've
	 * removed the "." reference and the reference
	 * in the parent directory, but there may be
	 * other hard links so decrement by 2 and
	 * worry about them later.
	 */
	ip->i_nlink -= 2;
	error = ext2_truncate(vp, (off_t)0, IO_SYNC, cnp->cn_cred, td);
	cache_purge(ITOV(ip));

out:
   if (dvp)
		vput(dvp);
   VN_KNOTE(vp, NOTE_DELETE);
	vput(vp);
	ext2_trace_return(error);
}

/*
 * symlink -- make a symbolic link
 */
static int
ext2_symlink(ap)
	struct vop_symlink_args /* {
		struct vnode *a_dvp;
		struct vnode **a_vpp;
		struct componentname *a_cnp;
		struct vattr *a_vap;
		char *a_target;
	} */ *ap;
{
	struct vnode *vp, **vpp = ap->a_vpp;
	struct inode *ip;
	int len, error;
   
   ext2_trace_enter();

	error = ext2_makeinode(IFLNK | ap->a_vap->va_mode, ap->a_dvp,
	    vpp, ap->a_cnp);
	if (error)
		ext2_trace_return(error);
   VN_KNOTE(ap->a_dvp, NOTE_WRITE);
	vp = *vpp;
	len = strlen(ap->a_target);
	if (len < vp->v_mount->mnt_maxsymlinklen) {
		ip = VTOI(vp);
		bcopy(ap->a_target, (char *)ip->i_shortlink, len);
		ip->i_size = len;
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
	} else
		error = vn_rdwr(UIO_WRITE, vp, ap->a_target, len, (off_t)0,
		    UIO_SYSSPACE, IO_NODELOCKED | IO_NOMACCHECK,
		    ap->a_cnp->cn_cred, (int *)0, (struct proc *)0);
		vput(vp);
	ext2_trace_return(error);
}

/*
 * Return target name of a symbolic link
 */
static int
ext2_readlink(ap)
	struct vop_readlink_args /* {
		struct vnode *a_vp;
		struct uio *a_uio;
		struct ucred *a_cred;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	struct inode *ip = VTOI(vp);
	int isize;
   
   ext2_trace_enter();

	isize = ip->i_size;
	if (isize < vp->v_mount->mnt_maxsymlinklen) {
		uiomove((char *)ip->i_shortlink, isize, ap->a_uio);
		return (0);
	}
	ext2_trace_return(VOP_READ(vp, ap->a_uio, 0, ap->a_cred));
}

/*
 * Calculate the logical to physical mapping if not done already,
 * then call the device strategy routine.
 *
 * In order to be able to swap to a file, the ext2_bmaparray() operation may not
 * deadlock on memory.  See ext2_bmap() for details.
 */
static int
ext2_strategy(ap)
	struct vop_strategy_args /* {
		struct buf *a_bp;
	} */ *ap;
{
	struct buf *bp = ap->a_bp;
	struct vnode *vp = bp->b_vp;
	struct inode *ip;
	int error;
   
    ext2_trace_enter();

	ip = VTOI(vp);
    if (0 == (bp->b_flags & B_VECTORLIST)) {
        if (vp->v_type == VBLK || vp->v_type == VCHR)
            panic("ext2_strategy: spec");
       
        if (bp->b_flags & B_PAGELIST) {
          /*
           * If we have a page list associated with this bp,
           * then go through cluster_bp since it knows how to 
           * deal with a page request that might span non-
           * contiguous physical blocks on the disk...
           */
    #if 1
          if (bp->b_blkno == bp->b_lblkno) {
             error = ext2_bmaparray(vp, bp->b_lblkno, &bp->b_blkno, NULL, NULL);
             if (error) {
                bp->b_error = error;
                bp->b_ioflags |= BIO_ERROR;
                bufdone(bp);
                ext2_trace_return(error);
             }
          }
    #endif      
          error = cluster_bp(bp);
          vp = ip->i_devvp;
          bp->b_dev = vp->v_rdev;

          ext2_trace_return(error);
        }
       
        if (bp->b_blkno == bp->b_lblkno) {
            error = ext2_bmaparray(vp, bp->b_lblkno, &bp->b_blkno, NULL, NULL);
            if (error) {
                bp->b_error = error;
                bp->b_ioflags |= BIO_ERROR;
                bufdone(bp);
                ext2_trace_return(error);
            }
            if ((long)bp->b_blkno == -1)
                vfs_bio_clrbuf(bp);
        }
        if ((long)bp->b_blkno == -1) {
            bufdone(bp);
            return (0);
        }
    } /* B_VECTORLIST */
    
    dprint_clusters(vp);
    
	vp = ip->i_devvp;
    bp->b_dev = vp->v_rdev;
    VOCALL (vp->v_op, VOFFSET(vop_strategy), ap);
    return (0);
}

/*
 * Print out the contents of an inode.
 */
static int
ext2_print(ap)
	struct vop_print_args /* {
		struct vnode *a_vp;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	struct inode *ip = VTOI(vp);

	printf("\text2 ino %lu, on dev %s (%d, %d)", (u_long)ip->i_number,
	    devtoname(ip->i_dev), major(ip->i_dev), minor(ip->i_dev));
	printf("\n");
	return (0);
}


/*
 * Read wrapper for special devices.
 */
static int
ext2spec_read(ap)
	struct vop_read_args /* {
		struct vnode *a_vp;
		struct uio *a_uio;
		int  a_ioflag;
		struct ucred *a_cred;
	} */ *ap;
{
	int error, resid;
	struct inode *ip;
	struct uio *uio;
   
   ext2_trace_enter();

	uio = ap->a_uio;
	resid = uio->uio_resid;
	error = VOCALL(spec_vnodeop_p, VOFFSET(vop_read), ap);
	/*
	 * The inode may have been revoked during the call, so it must not
	 * be accessed blindly here or in the other wrapper functions.
	 */
	ip = VTOI(ap->a_vp);
	if (ip != NULL && (uio->uio_resid != resid || (error == 0 && resid != 0)))
		ip->i_flag |= IN_ACCESS;
	ext2_trace_return(error);
}

/*
 * Write wrapper for special devices.
 */
static int
ext2spec_write(ap)
	struct vop_write_args /* {
		struct vnode *a_vp;
		struct uio *a_uio;
		int  a_ioflag;
		struct ucred *a_cred;
	} */ *ap;
{
	int error, resid;
	struct inode *ip;
	struct uio *uio;
   
   ext2_trace_enter();

	uio = ap->a_uio;
	resid = uio->uio_resid;
	error = VOCALL(spec_vnodeop_p, VOFFSET(vop_write), ap);
	ip = VTOI(ap->a_vp);
	if (ip != NULL && (uio->uio_resid != resid || (error == 0 && resid != 0)))
		VTOI(ap->a_vp)->i_flag |= IN_CHANGE | IN_UPDATE;
	ext2_trace_return(error);
}

/*
 * Close wrapper for special devices.
 *
 * Update the times on the inode then do device close.
 */
static int
ext2spec_close(ap)
	struct vop_close_args /* {
		struct vnode *a_vp;
		int  a_fflag;
		struct ucred *a_cred;
		struct thread *a_td;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
   
   ext2_trace_enter();

	VI_LOCK(vp);
	if (vp->v_usecount > 1)
		ext2_itimes(vp);
	VI_UNLOCK(vp);
	ext2_trace_return(VOCALL(spec_vnodeop_p, VOFFSET(vop_close), ap));
}

/*
 * Read wrapper for fifos.
 */
static int
ext2fifo_read(ap)
	struct vop_read_args /* {
		struct vnode *a_vp;
		struct uio *a_uio;
		int  a_ioflag;
		struct ucred *a_cred;
	} */ *ap;
{
	int error, resid;
	struct inode *ip;
	struct uio *uio;
   
   ext2_trace_enter();

	uio = ap->a_uio;
	resid = uio->uio_resid;
	error = VOCALL(fifo_vnodeop_p, VOFFSET(vop_read), ap);
	ip = VTOI(ap->a_vp);
	if ((ap->a_vp->v_mount->mnt_flag & MNT_NOATIME) == 0 && ip != NULL &&
	    (uio->uio_resid != resid || (error == 0 && resid != 0)))
		VTOI(ap->a_vp)->i_flag |= IN_ACCESS;
	ext2_trace_return(error);
}

/*
 * Write wrapper for fifos.
 */
static int
ext2fifo_write(ap)
	struct vop_write_args /* {
		struct vnode *a_vp;
		struct uio *a_uio;
		int  a_ioflag;
		struct ucred *a_cred;
	} */ *ap;
{
	int error, resid;
	struct inode *ip;
	struct uio *uio;
   
   ext2_trace_enter();

	uio = ap->a_uio;
	resid = uio->uio_resid;
	error = VOCALL(fifo_vnodeop_p, VOFFSET(vop_write), ap);
	ip = VTOI(ap->a_vp);
	if (ip != NULL && (uio->uio_resid != resid || (error == 0 && resid != 0)))
		VTOI(ap->a_vp)->i_flag |= IN_CHANGE | IN_UPDATE;
	ext2_trace_return(error);
}

/*
 * Close wrapper for fifos.
 *
 * Update the times on the inode then do device close.
 */
static int
ext2fifo_close(ap)
	struct vop_close_args /* {
		struct vnode *a_vp;
		int  a_fflag;
		struct ucred *a_cred;
		struct thread *a_td;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
   
   ext2_trace_enter();

	VI_LOCK(vp);
	if (vp->v_usecount > 1)
		ext2_itimes(vp);
	VI_UNLOCK(vp);
	ext2_trace_return(VOCALL(fifo_vnodeop_p, VOFFSET(vop_close), ap));
}

#ifdef EXT_KNOTE

/*
 * Kqfilter wrapper for fifos.
 *
 * Fall through to ext2 kqfilter routines if needed 
 */
static int
ext2fifo_kqfilter(ap)
	struct vop_kqfilter_args *ap;
{
	int error;

	error = VOCALL(fifo_vnodeop_p, VOFFSET(vop_kqfilter), ap);
	if (error)
		error = ext2_kqfilter(ap);
	ext2_trace_return(error);
}

#endif

/*
 * Return POSIX pathconf information applicable to ext2 filesystems.
 */
static int
ext2_pathconf(ap)
	struct vop_pathconf_args /* {
		struct vnode *a_vp;
		int a_name;
		int *a_retval;
	} */ *ap;
{

	switch (ap->a_name) {
	case _PC_LINK_MAX:
		*ap->a_retval = LINK_MAX;
		return (0);
	case _PC_NAME_MAX:
		*ap->a_retval = NAME_MAX;
		return (0);
	case _PC_PATH_MAX:
		*ap->a_retval = PATH_MAX;
		return (0);
	case _PC_PIPE_BUF:
		*ap->a_retval = PIPE_BUF;
		return (0);
	case _PC_CHOWN_RESTRICTED:
		*ap->a_retval = 1;
		return (0);
	case _PC_NO_TRUNC:
		*ap->a_retval = 1;
		return (0);
   case _PC_CASE_SENSITIVE:
      *ap->a_retval = 1;
      return(0);
	default:
		ext2_trace_return(EINVAL);
	}
	/* NOTREACHED */
}

/*
 * Advisory record locking support
 */
static int
ext2_advlock(ap)
	struct vop_advlock_args /* {
		struct vnode *a_vp;
		caddr_t  a_id;
		int  a_op;
		struct flock *a_fl;
		int  a_flags;
	} */ *ap;
{
   /* Cribbed straigt from ufs */
   register struct inode *ip = VTOI(ap->a_vp);
	register struct flock *fl = ap->a_fl;
	register struct ext2lockf *lock;
	off_t start, end;
	int error;
   
   ext2_trace_enter();

	/*
	 * Avoid the common case of unlocking when inode has no locks.
	 */
	if (ip->i_lockf == (struct ext2lockf *)0) {
		if (ap->a_op != F_SETLK) {
			fl->l_type = F_UNLCK;
			return (0);
		}
	}
	/*
	 * Convert the flock structure into a start and end.
	 */
	switch (fl->l_whence) {

	case SEEK_SET:
	case SEEK_CUR:
		/*
		 * Caller is responsible for adding any necessary offset
		 * when SEEK_CUR is used.
		 */
		start = fl->l_start;
		break;

	case SEEK_END:
		start = ip->i_size + fl->l_start;
		break;

	default:
		ext2_trace_return(EINVAL);
	}
	if (start < 0)
		ext2_trace_return(EINVAL);
	if (fl->l_len == 0)
		end = -1;
	else
		end = start + fl->l_len - 1;
	/*
	 * Create the ext2lockf structure
	 */
	MALLOC(lock, struct ext2lockf *, sizeof *lock, M_LOCKF, M_WAITOK);
	lock->lf_start = start;
	lock->lf_end = end;
	lock->lf_id = ap->a_id;
	lock->lf_inode = ip;
	lock->lf_type = fl->l_type;
	lock->lf_next = (struct ext2lockf *)0;
	TAILQ_INIT(&lock->lf_blkhd);
	lock->lf_flags = ap->a_flags;
	/*
	 * Do the requested operation.
	 */
	switch(ap->a_op) {
	case F_SETLK:
		ext2_trace_return(ext2_lf_setlock(lock));

	case F_UNLCK:
		error = ext2_lf_clearlock(lock);
		FREE(lock, M_LOCKF);
		ext2_trace_return(error);

	case F_GETLK:
		error = ext2_lf_getlock(lock, fl);
		FREE(lock, M_LOCKF);
		ext2_trace_return(error);
	
	default:
		_FREE(lock, M_LOCKF);
		ext2_trace_return(EINVAL);
	}
	/* NOTREACHED */
}

/*
 * Initialize the vnode associated with a new inode, handle aliased
 * vnodes.
 */
int
ext2_vinit(mntp, specops, fifoops, vpp)
	struct mount *mntp;
	vop_t **specops;
	vop_t **fifoops;
	struct vnode **vpp;
{
	struct inode *ip;
	struct vnode *vp;
   struct vnode *nvp;
	struct timeval tv;

	vp = *vpp;
	ip = VTOI(vp);
	switch(vp->v_type = IFTOVT(ip->i_mode)) {
	case VCHR:
	case VBLK:
		vp->v_op = specops;
      if (nvp = checkalias(vp, ip->i_rdev, mntp)) {
			/*
			 * Discard unneeded vnode, but save its inode.
			 * Note that the lock is carried over in the inode
			 * to the replacement vnode.
			 */
			nvp->v_data = vp->v_data;
			vp->v_data = NULL;
			vp->v_op = spec_vnodeop_p;
			vrele(vp);
			vgone(vp);
			/*
			 * Reinitialize aliased inode.
			 */
			vp = nvp;
			ip->i_vnode = vp;
		}
		break;
	case VFIFO:
		vp->v_op = fifoops;
		break;
	default:
		break;

	}
	if (ip->i_number == ROOTINO)
		vp->v_vflag |= VV_ROOT;
	/*
	 * Initialize modrev times
	 */
	getmicrouptime(&tv);
	SETHIGH(ip->i_modrev, tv.tv_sec);
	SETLOW(ip->i_modrev, tv.tv_usec * 4294);
	*vpp = vp;
	return (0);
}

/*
 * Allocate a new inode.
 */
static int
ext2_makeinode(mode, dvp, vpp, cnp)
	int mode;
	struct vnode *dvp;
	struct vnode **vpp;
	struct componentname *cnp;
{
	struct inode *ip, *pdir;
	struct vnode *tvp;
	int error;

	pdir = VTOI(dvp);
#ifdef DIAGNOSTIC
	if ((cnp->cn_flags & HASBUF) == 0)
		panic("ext2_makeinode: no name");
#endif
	*vpp = NULL;
	if ((mode & IFMT) == 0)
		mode |= IFREG;

	error = ext2_valloc(dvp, mode, cnp->cn_cred, &tvp);
	if (error) {
      _FREE_ZONE(cnp->cn_pnbuf, cnp->cn_pnlen, M_NAMEI);
		vput(dvp);
		ext2_trace_return(error);
	}
	ip = VTOI(tvp);
	ip->i_gid = pdir->i_gid;
#ifdef SUIDDIR
	{
		/*
		 * if we are
		 * not the owner of the directory,
		 * and we are hacking owners here, (only do this where told to)
		 * and we are not giving it TOO root, (would subvert quotas)
		 * then go ahead and give it to the other user.
		 * Note that this drops off the execute bits for security.
		 */
		if ( (dvp->v_mount->mnt_flag & MNT_SUIDDIR) &&
		     (pdir->i_mode & ISUID) &&
		     (pdir->i_uid != cnp->cn_cred->cr_uid) && pdir->i_uid) {
			ip->i_uid = pdir->i_uid;
			mode &= ~07111;
		} else {
         if ((mode & IFMT) == IFLNK)
            ip->i_uid = pdir->i_uid;
         else
            ip->i_uid = cnp->cn_cred->cr_uid;
		}
	}
#else
   if ((mode & IFMT) == IFLNK)
		ip->i_uid = pdir->i_uid;
	else
      ip->i_uid = cnp->cn_cred->cr_uid;
#endif
	ip->i_flag |= IN_ACCESS | IN_CHANGE | IN_UPDATE;
	ip->i_mode = mode;
	tvp->v_type = IFTOVT(mode);	/* Rest init'd in getnewvnode(). */
	ip->i_nlink = 1;
	if ((ip->i_mode & ISGID) && !groupmember(ip->i_gid, cnp->cn_cred) &&
       suser(cnp->cn_cred, NULL))
		ip->i_mode &= ~ISGID;

	if (cnp->cn_flags & ISWHITEOUT)
		ip->i_flags |= UF_OPAQUE;
   
   /*
	 * initialize UBC before calling ext2_update and ext2_direnter
	 * Not doing so introduces probelms in handling error from
	 * those calls.
	 * It results in a "vget: stolen ubc_info" panic due to attempt
	 * to shutdown uninitialized UBC.
	 */
	if (UBCINFOMISSING(tvp) || UBCINFORECLAIMED(tvp))
		ubc_info_init(tvp);

	/*
	 * Make sure inode goes to disk before directory entry.
	 */
	error = ext2_update(tvp, 1);
	if (error)
		goto bad;
	error = ext2_direnter(ip, dvp, cnp);
	if (error)
		goto bad;
   
   if ((cnp->cn_flags & SAVESTART) == 0)
		FREE_ZONE(cnp->cn_pnbuf, cnp->cn_pnlen, M_NAMEI);
	vput(dvp);

	*vpp = tvp;
	return (0);

bad:
	/*
	 * Write error occurred trying to update the inode
	 * or the directory so must deallocate the inode.
	 */
   if ((cnp->cn_flags & SAVESTART) == 0)
      _FREE_ZONE(cnp->cn_pnbuf, cnp->cn_pnlen, M_NAMEI);
	vput(dvp);
	ip->i_nlink = 0;
	ip->i_flag |= IN_CHANGE;
	vput(tvp);
	ext2_trace_return(error);
}

#ifdef EXT_KNOTE

static struct filterops ext2read_filtops = 
	{ 1, NULL, filt_ext2detach, filt_ext2read };
static struct filterops ext2write_filtops = 
	{ 1, NULL, filt_ext2detach, filt_ext2write };
static struct filterops ext2vnode_filtops = 
	{ 1, NULL, filt_ext2detach, filt_ext2vnode };

static int
ext2_kqfilter(ap)
	struct vop_kqfilter_args /* {
		struct vnode *a_vp;
		struct knote *a_kn;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	struct knote *kn = ap->a_kn;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &ext2read_filtops;
		break;
	case EVFILT_WRITE:
		kn->kn_fop = &ext2write_filtops;
		break;
	case EVFILT_VNODE:
		kn->kn_fop = &ext2vnode_filtops;
		break;
	default:
		return (1);
	}

	kn->kn_hook = (caddr_t)vp;

	KNOTE_ATTACH(&VTOI(vp)->i_knotes, kn);

	return (0);
}

static void
filt_ext2detach(struct knote *kn)
{
   struct vnode *vp = (struct vnode *)kn->kn_hook;
   struct proc *p = current_proc();
   int err;
   
   if (1) {	/* ! KNDETACH_VNLOCKED */
      err = vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, p);
      if (err) return;
   };
   
   err = KNOTE_DETACH(&VTOI(vp)->i_knotes, kn);
   
   if (1) {	/* ! KNDETACH_VNLOCKED */
      VOP_UNLOCK(vp, 0, p);
   };
}

/*ARGSUSED*/
static int
filt_ext2read(struct knote *kn, long hint)
{
	struct vnode *vp = (struct vnode *)kn->kn_hook;
	struct inode *ip = VTOI(vp);

	/*
	 * filesystem is gone, so set the EOF flag and schedule 
	 * the knote for deletion.
	 */
	if (hint == NOTE_REVOKE) {
		kn->kn_flags |= (EV_EOF | EV_ONESHOT);
		return (1);
	}

        kn->kn_data = ip->i_size - kn->kn_fp->f_offset;
        return (kn->kn_data != 0);
}

/*ARGSUSED*/
static int
filt_ext2write(struct knote *kn, long hint)
{

	/*
	 * filesystem is gone, so set the EOF flag and schedule 
	 * the knote for deletion.
	 */
	if (hint == NOTE_REVOKE)
		kn->kn_flags |= (EV_EOF | EV_ONESHOT);

        kn->kn_data = 0;
        return (1);
}

static int
filt_ext2vnode(struct knote *kn, long hint)
{

	if (kn->kn_sfflags & hint)
		kn->kn_fflags |= hint;
	if (hint == NOTE_REVOKE) {
		kn->kn_flags |= EV_EOF;
		return (1);
	}
	return (kn->kn_fflags != 0);
}

#endif /* EXT_KNOTE */

int
ext2fs_truncate(ap)
	struct vop_truncate_args /* {
		struct vnode *a_vp;
		off_t a_length;
		int a_flags;
		struct ucred *a_cred;
		struct proc *a_p;
	} */ *ap;
{
   ext2_trace_enter();
   return (ext2_truncate(ap->a_vp, ap->a_length, ap->a_flags, ap->a_cred, ap->a_p));
}
