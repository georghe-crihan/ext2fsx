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
#ifdef EXT2FS_DEBUG
#include <gnu/ext2fs/ext2_fs.h>
#endif
#include <gnu/ext2fs/inode.h>

#if 0
/* Cribbed from FreeBSD kern/kern_prot.c */
/*
 * Check if gid is a member of the group set.
 */
int
groupmember(gid, cred)
	gid_t gid;
	register struct ucred *cred;
{
	register gid_t *gp;
	gid_t *egp;

	egp = &(cred->cr_groups[cred->cr_ngroups]);
	for (gp = cred->cr_groups; gp < egp; gp++)
		if (*gp == gid)
			return (1);
	return (0);
}
#endif

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
   if (usecnt < 1 && UBCISVALID(vp))
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
   #ifndef APPLE
	for (bp = TAILQ_FIRST(&vp->v_dirtyblkhd); bp; bp = nbp) {
		nbp = TAILQ_NEXT(bp, b_vnbufs);
   #else
   for (bp = LIST_FIRST(&vp->v_dirtyblkhd); bp; bp = nbp) {
		nbp = LIST_NEXT(bp, b_vnbufs);
   #endif /* APPLE */
		VI_UNLOCK(vp);
      #ifndef APPLE
		if (BUF_LOCK(bp, LK_EXCLUSIVE | LK_NOWAIT)) {
			VI_LOCK(vp);
			continue;
		}
      #else
      if ((bp->b_flags & B_BUSY)) {
         VI_LOCK(vp);
         continue;
      }
      #endif
		if ((bp->b_flags & B_DELWRI) == 0)
			panic("vop_stdfsync: not dirty");
		bremfree(bp);
      #ifdef APPLE
      bp->b_flags |= B_BUSY;
      #endif
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
      #ifndef APPLE
		if (!TAILQ_EMPTY(&vp->v_dirtyblkhd)) {
      #else
      if (!LIST_EMPTY(&vp->v_dirtyblkhd)) {
      #endif
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

	if (VTOI(vp) == (struct inode *) NULL)
		panic ("ext2_lock: null node");
   
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

