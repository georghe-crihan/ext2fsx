/*	
 * Copyright (c) 2002-2003
 *	Brian Bergstrand.  All rights reserved.
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
 * $Revision$
 */

#ifndef EXT2_APPLE_H
#define EXT2_APPLE_H

// In kernel, but not defined in headers
extern int groupmember(gid_t gid, struct ucred *cred);

#define M_EXT2NODE M_MISCFSNODE
#define M_EXT2MNT M_MISCFSMNT

typedef int vop_t __P((void *));

#define EXT2FS_NAME "ext2fs"

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

#define v_rdev v_specinfo
#define devtoname(d) (char*)(d)

#define VI_LOCK(vp)
#define VI_UNLOCK(vp)

#define vnode_pager_setsize ubc_setsize

#define vrefcnt(vp) ubc_isinuse((vp), 1)
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

#define vn_write_suspend_wait(vp,mp,flag) (0)

/* XXX Equivalent fn in Darwin ? */
#define vfs_bio_clrbuf clrbuf

#define BUF_WRITE bwrite
#define BUF_STRATEGY VOP_STRATEGY

#define bqrelse brelse

#define bufwait biowait
#define bufdone biodone

#define hashdestroy(tbl,type,cnt) FREE((tbl), (type))

#ifndef mtx_destroy
#define mtx_destroy(mpp) mutex_free(*(mpp))
#endif
#ifndef mtx_lock
#define mtx_lock(mpp) mutex_lock(*(mpp))
#endif
#ifndef mtx_unlock
#define mtx_unlock(mpp) mutex_unlock(*(mpp))
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
    
int groupmember(gid_t, register struct ucred *);

#endif /* EXT2_APPLE_H */
