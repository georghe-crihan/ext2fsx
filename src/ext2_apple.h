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

#ifndef EXT2_APPLE_H
#define EXT2_APPLE_H

#ifdef KERNEL

#include <sys/ubc.h>

#include <xnu/bsd/miscfs/specfs/specdev.h>

/* In kernel, but not defined in headers */
extern int groupmember(gid_t gid, struct ucred *cred);
extern int vfs_init_io_attributes (struct vnode *, struct mount *);
extern uid_t console_user;
/**/

#define M_EXT2NODE M_MISCFSNODE
#define M_EXT2MNT M_MISCFSMNT

typedef int vop_t __P((void *));

__private_extern__ int ext2_cache_lookup __P((struct vop_lookup_args *));
__private_extern__ int ext2_blktooff __P((struct vop_blktooff_args *));
__private_extern__ int ext2_offtoblk __P((struct vop_offtoblk_args *));
__private_extern__ int ext2_getattrlist __P((struct vop_getattrlist_args *));
__private_extern__ int ext2_pagein __P((struct vop_pagein_args *));
__private_extern__ int ext2_pageout __P((struct vop_pageout_args *));
__private_extern__ int ext2_cmap __P((struct vop_cmap_args *));
__private_extern__ int ext2_mmap __P((struct vop_mmap_args *));
__private_extern__ int ext2_lock __P((struct vop_lock_args *));
__private_extern__ int ext2_unlock __P((struct vop_unlock_args *));
__private_extern__ int ext2_islocked __P((struct vop_islocked_args *));
__private_extern__ int ext2_abortop __P((struct vop_abortop_args *));

#define UNKNOWNUID ((uid_t)99)

#define VT_EXT2 VT_OTHER

#endif /* KERNEL */

#define EXT2FS_NAME "ext2"

#ifdef KERNEL

/* Process stuff */
#define curproc (current_proc())
#define curthread curproc
#define thread proc
#define td_ucred p_ucred
#define a_td a_p
#define cn_thread cn_proc
#define uio_td uio_procp
#define PROC_LOCK(p)
#define PROC_UNLOCK(p)

/*
 * Test the active securelevel against a given level.  securelevel_gt()
 * implements (securelevel > level).  securelevel_ge() implements
 * (securelevel >= level).  Note that the logic is inverted -- these
 * functions return EPERM on "success" and 0 on "failure".
 *
 * Must call while holding kernel funnel for MP safeness.
 */
#define securelevel_gt(cr,level) ( securelevel > (level) ? EPERM : 0 )
#define securelevel_ge(cr,level) ( securelevel >= (level) ? EPERM : 0 )

#define devtoname(d) "unknown"

#define mnt_iosize_max mnt_maxreadcnt

#define VI_LOCK(vp) simple_lock((vp)->v_interlock)
#define VI_UNLOCK(vp) simple_unlock((vp)->v_interlock)

#define vnode_pager_setsize(vp,sz) \
do { \
   if (UBCISVALID((vp))) {ubc_setsize((vp), (sz));} \
} while(0)

__private_extern__ int vrefcnt(struct vnode *);

#define v_vflag v_flag

#define prtactive TRUE /* XXX what is this global in FBSD? */

/* Flag for vn_rdwr() */
#define IO_NOMACCHECK 0

/* Mount flags */
#define MNT_NOCLUSTERR 0
#define MNT_NOCLUSTERW 0
#define MNT_NOATIME 0

#define SF_SNAPSHOT 0
#define SF_NOUNLINK 0
#define NOUNLINK 0

/* Vnode flags */
#define VV_ROOT VROOT

/* XXX Equivalent fn in Darwin ? */
#define vn_write_suspend_wait(vp,mp,flag) (0)

#define vfs_bio_clrbuf clrbuf

#define b_ioflags b_flags
#define BIO_ERROR B_ERROR

#define BUF_WRITE bwrite
#define BUF_STRATEGY VOP_STRATEGY

#define bqrelse brelse

#define bufwait biowait
#define bufdone biodone

#define hashdestroy(tbl,type,cnt) FREE((tbl), (type))

#ifndef mtx_destroy
#define mtx_destroy(mp) mutex_free((mp))
#endif
#ifndef mtx_lock
#define mtx_lock(mp) mutex_lock((mp))
#endif
#ifndef mtx_unlock
#define mtx_unlock(mp) mutex_unlock((mp))
#endif

static __inline void * memscan(void * addr, int c, size_t size)
{
	unsigned char * p = (unsigned char *) addr;

	while (size) {
		if (*p == c)
			return (void *) p;
		p++;
		size--;
	}
  	return (void *) p;
}

/* Cribbed from FBSD sys/dirent.h */
/*
 * The _GENERIC_DIRSIZ macro gives the minimum record length which will hold
 * the directory entry.  This requires the amount of space in struct direct
 * without the d_name field, plus enough space for the name with a terminating
 * null byte (dp->d_namlen+1), rounded up to a 4 byte boundary.
 */
#define	GENERIC_DIRSIZ(dp) \
    ((sizeof (struct dirent) - (MAXNAMLEN+1)) + (((dp)->d_namlen+1 + 3) &~ 3))

#endif /*KERNEL*/

#endif /* EXT2_APPLE_H */
