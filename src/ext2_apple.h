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

#define EXT2FS_NAME "ext2"
#define EXT3FS_NAME "ext3"

#define EXT2_VOL_LABEL_LENGTH 16
#define EXT2_VOL_LABEL_INVAL_CHARS "\"*/:;<=>?[\\]|"

static __inline__
int ext2_vol_label_len(char *label) {
   int i;
   for (i = 0; i < EXT2_VOL_LABEL_LENGTH && 0 != label[i]; i++) ;
   return (i);
}

#include <sys/mount.h>
struct ext2_args {
	char	*fspec;			/* block special device to mount */
   unsigned long e2_mnt_flags; /* Ext2 specific mount flags */
    uid_t   e2_uid; /* Only active when MNT_UNKNOWNPERMISSIONS is set */
    gid_t   e2_gid; /* ditto */
	struct	export_args export;	/* network export information */
};

#define EXT2_MNT_INDEX 0x00000001
#define EXT2_MOPT_INDEX { "index", 0, EXT2_MNT_INDEX, 0 }

#define UNKNOWNUID ((uid_t)99)
#define UNKNOWNGID ((gid_t)99)

#ifdef KERNEL

#ifdef DIAGNOSTIC
#define MACH_ASSERT 1
#include <kern/assert.h>
#endif

#include <sys/ubc.h>

#include <xnu/bsd/miscfs/specfs/specdev.h>

/* In kernel, but not defined in headers */
extern int groupmember(gid_t gid, struct ucred *cred);
extern int vfs_init_io_attributes (struct vnode *, struct mount *);
extern uid_t console_user;
extern int prtactive; /* 1 => print out reclaim of active vnodes */
/**/

#define M_EXT3DIRPRV M_MISCFSNODE
#define M_EXT2NODE M_MISCFSNODE
#define M_EXT2MNT M_MISCFSMNT
#define VT_EXT2 VT_OTHER

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
__private_extern__ int ext2_ioctl __P((struct vop_ioctl_args *));
__private_extern__ int ext2_setattrlist __P((struct vop_setattrlist_args *));

#if 0 //DIAGNOSTIC
__private_extern__ void ext2_checkdirsize(struct vnode *dvp);
#else
#define ext2_checkdirsize(dvp)
#endif

/* Set to 1 to enable inode/block bitmap caching -- currently, this will
cause a panic situation when the filesystem is stressed. */
#define EXT2_SB_BITMAP_CACHE 0

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

static __inline__ int msleep(void *chan, void *lock /*ignored*/, int pri,
   char *wmesg, int timo)
{
   return (tsleep(chan, pri, wmesg, timo));
}

/*
 * Test the active securelevel against a given level.  securelevel_gt()
 * implements (securelevel > level).  securelevel_ge() implements
 * (securelevel >= level).  Note that the logic is inverted -- these
 * functions return EPERM on "success" and 0 on "failure".
 *
 * Must call while holding kernel funnel for SMP safeness.
 */
#define securelevel_gt(cr,level) ( securelevel > (level) ? EPERM : 0 )
#define securelevel_ge(cr,level) ( securelevel >= (level) ? EPERM : 0 )

#define devtoname(d) "unknown"

#define mnt_iosize_max mnt_maxreadcnt

#define VI_MTX(vp) NULL
/* #define VI_MTX(vp) (&(vp)->v_interlock) */

#if defined(EXT2FS_DEBUG) && defined(EXT2FS_TRACE)

#define VI_LOCK(vp) \
do { \
ext2_trace("grabbing vp %lu interlock\n", (vp)->v_id); \
simple_lock(&(vp)->v_interlock); \
} while(0)

#define VI_UNLOCK(vp) \
do { \
simple_unlock(&(vp)->v_interlock); \
ext2_trace("dropped vp %lu interlock\n", (vp)->v_id); \
} while(0)

#else

#define VI_LOCK(vp) simple_lock(&(vp)->v_interlock)
#define VI_UNLOCK(vp) simple_unlock(&(vp)->v_interlock)

#endif /* defined(EXT2FS_DEBUG) && defined(EXT2FS_TRACE) */

/* FreeBSD kern calls */

#define vnode_pager_setsize(vp,sz) \
do { \
   if (UBCISVALID((vp))) {ubc_setsize((vp), (sz));} \
} while(0)

#define vfs_timestamp nanotime

__private_extern__ int vrefcnt(struct vnode *);
__private_extern__ int vop_stdfsync(struct vop_fsync_args *);

/* Separate flag fields in FreeBSD, only one in Darwin. */
#define v_vflag v_flag
#define v_iflag v_flag

/* Vnode flags */
#define VV_ROOT VROOT
#define VI_BWAIT VBWAIT

/* FreeBSD flag for vn_rdwr() */
#define IO_NOMACCHECK 0

/* FreeBSD Mount flags */
#define MNT_NOCLUSTERR 0
#define MNT_NOCLUSTERW 0
#define MNT_NOATIME 0

/* Soft Updates */
#define SF_SNAPSHOT 0
#define SF_NOUNLINK 0
/* No delete */
#define NOUNLINK 0

/* XXX Equivalent fn in Darwin ? */
#define vn_write_suspend_wait(vp,mp,flag) (0)

#define vfs_bio_clrbuf clrbuf

#define b_ioflags b_flags
#define BIO_ERROR B_ERROR

/* FreeBSD getblk flag */
#define GB_LOCK_NOWAIT 0

#define BUF_WRITE bwrite
#define BUF_STRATEGY VOP_STRATEGY
/* Buffer, Lock, InterLock */
#define BUF_LOCK(b,l,il)

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
    
/* Linux compat */

#define printk printf
#define memcmp bcmp

#define ERR_PTR(err) (void*)(err)

#define EXT3_I(inode) (inode)

#define KERN_CRIT "CRITICAL"

#endif /*KERNEL*/

#endif /* EXT2_APPLE_H */
