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
/*	
 * Copyright (c) 1989, 1991, 1993, 1994	
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
 *
 */

static const char whatid[] __attribute__ ((unused)) =
"@(#) $Id$";

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/mount.h>
#include <sys/unistd.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <vfs/vfs_support.h>
#include <machine/spl.h>

#include "ext2_apple.h"
#include <gnu/ext2fs/ext2_fs.h>
#include <gnu/ext2fs/ext2_fs_sb.h>
#include <gnu/ext2fs/inode.h>
#include <gnu/ext2fs/fs.h>
#include <gnu/ext2fs/ext2_extern.h>

/* Cribbed from FreeBSD kern/vfs_subr.c */
/* This will cause the KEXT to break if/when the kernel proper defines this routine.
 * (It's declared in <sys/vnode.h>, but is not actually in the 6.x kernels.)
 */
/*
 * Common filesystem object access control check routine.  Accepts a
 * vnode's type, "mode", uid and gid, requested access mode, credentials,
 * and optional call-by-reference privused argument allowing vaccess()
 * to indicate to the caller whether privilege was used to satisfy the
 * request (obsoleted).  Returns 0 on success, or an errno on failure.
 */
int
vaccess(file_mode, file_uid, file_gid, acc_mode, cred)
	mode_t file_mode;
	uid_t file_uid;
	gid_t file_gid;
	mode_t acc_mode;
	struct ucred *cred;
{
   mode_t dac_granted;
   struct proc *p = curproc;
   /*
    * root always gets access
    */
   if (0 == suser(cred, p ? &p->p_acflag : NULL))
      return (0);

	/*
	 * Look for a normal, non-privileged way to access the file/directory
	 * as requested.  If it exists, go with that.
	 */

	dac_granted = 0;

	/* Check the owner. */
	if (cred->cr_uid == file_uid) {
		if (file_mode & S_IXUSR)
			dac_granted |= VEXEC;
		if (file_mode & S_IRUSR)
			dac_granted |= VREAD;
		if (file_mode & S_IWUSR)
			dac_granted |= (VWRITE);

		if ((acc_mode & dac_granted) == acc_mode)
			return (0);
	}

	/* Otherwise, check the groups (first match) */
	if (groupmember(file_gid, cred)) {
		if (file_mode & S_IXGRP)
			dac_granted |= VEXEC;
		if (file_mode & S_IRGRP)
			dac_granted |= VREAD;
		if (file_mode & S_IWGRP)
			dac_granted |= (VWRITE);

		if ((acc_mode & dac_granted) == acc_mode)
			return (0);
	}

	/* Otherwise, check everyone else. */
	if (file_mode & S_IXOTH)
		dac_granted |= VEXEC;
	if (file_mode & S_IROTH)
		dac_granted |= VREAD;
	if (file_mode & S_IWOTH)
		dac_granted |= (VWRITE);
	if ((acc_mode & dac_granted) == acc_mode)
		return (0);

	return (EACCES);
}

/* Cribbed from FreeBSD kern/vfs_subr.c */
/* This will cause the KEXT to break if/when the kernel proper defines this routine. */
/*
 * Return reference count of a vnode.
 *
 * The results of this call are only guaranteed when some mechanism other
 * than the VI lock is used to stop other processes from gaining references
 * to the vnode.  This may be the case if the caller holds the only reference.
 * This is also useful when stale data is acceptable as race conditions may
 * be accounted for by some other means.
 */
int
vrefcnt(struct vnode *vp)
{
	int usecnt;

	VI_LOCK(vp);
	usecnt = vp->v_usecount;
    if (usecnt < 1 && (UBCISVALID(vp) && ubc_isinuse(vp, 1)))
      usecnt = 1;
	VI_UNLOCK(vp);

	return (usecnt);
}

__private_extern__ int vop_stdfsync(struct vop_fsync_args *ap)
{
    struct vnode *vp = ap->a_vp;
	struct buf *bp;
	struct buf *nbp;
	int s;
   
   loop:
	VI_LOCK(vp);
	s = splbio();
    for (bp = LIST_FIRST(&vp->v_dirtyblkhd); bp; bp = nbp) {
		nbp = LIST_NEXT(bp, b_vnbufs);
		VI_UNLOCK(vp);
      if ((bp->b_flags & B_BUSY)) {
         VI_LOCK(vp);
         continue;
      }
		if ((bp->b_flags & B_DELWRI) == 0)
			panic("vop_stdfsync: not dirty");
		bremfree(bp);
      bp->b_flags |= B_BUSY;
		splx(s);
		/*
		 * Wait for I/O associated with indirect blocks to complete,
		 * since there is no way to quickly wait for them below.
		 */
		if (bp->b_vp == vp || ap->a_waitfor == MNT_NOWAIT)
			(void) bawrite(bp);
		else
			(void) bwrite(bp);
		goto loop;
	}
	if (ap->a_waitfor == MNT_WAIT) {
		while (vp->v_numoutput) {
         vp->v_iflag |= VI_BWAIT;
			msleep(&vp->v_numoutput, VI_MTX(vp), 
			    PRIBIO + 1, "e2fsyn", 0);
		}
#if DIAGNOSTIC
      if (!LIST_EMPTY(&vp->v_dirtyblkhd)) {
			vprint("ext2_fsync: dirty", vp);
			goto loop;
		}
#endif
	}
	VI_UNLOCK(vp);
	splx(s);
   
    return (0);
}

/* VNode Ops */

#define EXT_NO_ILOCKS 0

/*
 * Lock a node.
 */
__private_extern__ int
ext2_lock(ap)
	struct vop_lock_args /* {
		struct vnode *a_vp;
		int a_flags;
		struct proc *a_p;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;

#ifdef DIAGNOSTIC
	if (VTOI(vp) == (struct inode *) NULL)
		panic ("ext2_lock: null node");
#endif

   ext2_trace("locking inode %lu for pid %ld -- cur pid:%ld, cur ex: %d, cur shr: %d, cur wait:%d\n",
      (unsigned long)VTOI(vp)->i_number, (long)ap->a_p->p_pid, 
      (long)VTOI(vp)->i_lock.lk_lockholder, VTOI(vp)->i_lock.lk_exclusivecount,
      VTOI(vp)->i_lock.lk_sharecount, VTOI(vp)->i_lock.lk_waitcount);
   
#if EXT_NO_ILOCKS
   return (0);
#endif
   return (lockmgr(&VTOI(vp)->i_lock, ap->a_flags, &vp->v_interlock,ap->a_p));
}

/*
 * Unlock an node.
 */
__private_extern__ int
ext2_unlock(ap)
	struct vop_unlock_args /* {
		struct vnode *a_vp;
		int a_flags;
		struct proc *a_p;
	} */ *ap;
{
   struct vnode *vp = ap->a_vp;
   
   ext2_trace("unlocking inode %lu for pid:%ld\n", (unsigned long)VTOI(vp)->i_number,
      (long)ap->a_p->p_pid);
   
#if EXT_NO_ILOCKS
   return (0);
#endif
	return (lockmgr(&VTOI(vp)->i_lock, ap->a_flags | LK_RELEASE, &vp->v_interlock,ap->a_p));
}

/*
 * Check for a locked node.
 */
__private_extern__ int
ext2_islocked(ap)
	struct vop_islocked_args /* {
		struct vnode *a_vp;
	} */ *ap;
{

	return (lockstatus(&VTOI(ap->a_vp)->i_lock));
}

__private_extern__ int
ext2_abortop(ap)
struct vop_abortop_args /* {
    struct vnode *a_dvp;
    struct componentname *a_cnp;
} */ *ap;
{

    if ((ap->a_cnp->cn_flags & (HASBUF | SAVESTART)) == HASBUF)
        FREE_ZONE(ap->a_cnp->cn_pnbuf, ap->a_cnp->cn_pnlen, M_NAMEI);

    return (0);
}

__private_extern__ int
ext2_ioctl(ap)
	struct vop_ioctl_args /* {
      struct vnode *a_vp;
      u_long a_command;
      caddr_t a_data;
      int a_fflag;
      struct ucred *a_cred;
      struct proc *a_p;
   } */ *ap;
{
   struct inode *ip = VTOI(ap->a_vp);
   struct ext2_sb_info *fs;
   int err = 0, super;
   u_int32_t flags, oldflags;
   
   super = (0 == suser(ap->a_cred, &ap->a_p->p_acflag));
   fs = ip->i_e2fs;
   
   switch (ap->a_command) {
      case IOCBASECMD(EXT2_IOC_GETFLAGS):
         flags = ip->i_e2flags & EXT2_FL_USER_VISIBLE;
         bcopy(&flags, ap->a_data, sizeof(u_int32_t));
      break;
      
      case IOCBASECMD(EXT2_IOC_SETFLAGS):
         if (ip->i_e2fs->s_rd_only)
            return (EROFS);
         
         if (ap->a_cred->cr_uid != ip->i_uid && !super)
            return (EACCES);
         
         bcopy(ap->a_data, &flags, sizeof(u_int32_t));
         
         if (!S_ISDIR(ip->i_mode))
            flags &= ~EXT3_DIRSYNC_FL;
            
         oldflags = ip->i_e2flags;
         
         /* Update e2flags incase someone went through chflags and the
          inode has not been sync'd to disk yet. */
         if (ip->i_flags & APPEND)
            oldflags |= EXT2_APPEND_FL;
         
         if (ip->i_flags & IMMUTABLE)
            oldflags |= EXT2_IMMUTABLE_FL;
         
         /* Root owned files marked APPEND||IMMUTABLE can only be unset
            when the kernel is not protected. */
         if ((flags ^ oldflags) & (EXT2_APPEND_FL | EXT2_IMMUTABLE_FL)) {
            if (!super && (oldflags & (EXT2_APPEND_FL | EXT2_IMMUTABLE_FL)))
               err = EPERM;
            if (super && (oldflags & (EXT2_APPEND_FL | EXT2_IMMUTABLE_FL)) &&
                  ip->i_uid == 0)
               err = securelevel_gt(ap->a_cred, 0);
            if (err)
               return(err);
         }
         
         if ((flags ^ oldflags) & (EXT3_JOURNAL_DATA_FL)) {
            if (!super)
               return (EACCES);
         }
         
         if (!super) {
            flags = flags & EXT2_FL_USER_MODIFIABLE;
            flags |= oldflags & ~EXT2_FL_USER_MODIFIABLE;
         } else {
            flags |= oldflags;
         }
         ip->i_e2flags = flags;
         ip->i_flag |= IN_CHANGE|IN_MODIFIED;
         
         /* Update the BSD flags */
         if (ip->i_e2flags & EXT2_APPEND_FL)
            ip->i_flags |= super ? APPEND : UF_APPEND;
         else
            ip->i_flags &= ~(super ? APPEND : UF_APPEND);
         
         if (ip->i_e2flags & EXT2_IMMUTABLE_FL)
            ip->i_flags |= super ? IMMUTABLE : UF_IMMUTABLE;
         else
            ip->i_flags &= ~(super ? IMMUTABLE : UF_IMMUTABLE);
         
         err = ext2_update(ap->a_vp, 0);
         if (!err)
            VN_KNOTE(ap->a_vp, NOTE_ATTRIB);
      break;
      
      case IOCBASECMD(EXT2_IOC_GETVERSION):
         bcopy(&ip->i_gen, ap->a_data, sizeof(int32_t));
      break;
      
      case IOCBASECMD(EXT2_IOC_SETVERSION):
         if (ap->a_cred->cr_uid != ip->i_uid && !super)
            err = EACCES;
         break;
         err = ENOTSUP;
      break;
      
      case IOCBASECMD(EXT2_IOC_GETSBLOCK):
         lock_super(fs);
         bcopy(fs->s_es, ap->a_data, sizeof(struct ext2_super_block));
         unlock_super(fs);
      break;
      
      default:
         err = ENOTTY;
      break;
   }
   
   nop_ioctl(ap);
   return (err);
}

#if 0 //DIAGNOSTIC
/*
   BDB - This only works with directories that fit in a single block.
   So it works about 98% of the time. I wrote it as a quick 'n dirty
   hack to track down the panics/corruptions in bug #742939.
*/
__private_extern__ void
ext2_checkdirsize(dvp)
   struct vnode *dvp;
{
   struct buf *bp;
   struct ext2_dir_entry_2 *ep;
   char *dirbuf;
   struct inode *dp;
   int  size, error;
   
   dp = VTOI(dvp);
   if (is_dx(dp))
      return;
   if ((error = ext2_blkatoff(dvp, (off_t)0, &dirbuf,
	    &bp)) != 0)
		return;
   
   size = 0;
   while (size < dp->i_size) {
      ep = (struct ext2_dir_entry_2 *)
			((char *)bp->b_data + size);
      if (ep->rec_len == 0)
         break;
      size += le16_to_cpu(ep->rec_len);
   }
   
   if (size != dp->i_size)
      printf("ext2: dir (%d) entries do not match dir size! (%d,%qu)\n",
         dp->i_number, size, dp->i_size);
   
   brelse(bp);
}
#endif
