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
/* Copyright (c) 1995 NeXT Computer, Inc. All Rights Reserved */
/*-
 * Copyright (c) 1993
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
 *	@(#)ufs_readwrite.c	8.11 (Berkeley) 5/8/95
 */

#include <sys/param.h>

#include <sys/resourcevar.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/mount.h>
#include <sys/signalvar.h>
#include <sys/stat.h>
#include <sys/ucred.h>
#include <sys/vnode.h>

#include <sys/ubc.h>
#include "ext2_apple.h"

#include <gnu/ext2fs/inode.h>
#include <gnu/ext2fs/ext2_extern.h>
#include <gnu/ext2fs/ext2_fs_sb.h>
#include <gnu/ext2fs/fs.h>
#include <gnu/ext2fs/ext2_fs.h>
#include "ext2_apple.h"

#define	BLKSIZE(a, b, c)	blksize(a, b, c)
#define	FS			struct ext2_sb_info
#define	I_FS			i_e2fs

static int ext2_blkalloc(register struct inode *, int32_t, int, struct ucred *, int);

/*
 * Vnode op for pagein.
 * Similar to ffs_read()
 */
/* ARGSUSED */
int
ext2_pagein(ap)
	struct vop_pagein_args /* {
	   	struct vnode *a_vp,
	   	upl_t 	a_pl,
		vm_offset_t   a_pl_offset,
		off_t         a_f_offset,
		size_t        a_size,
		struct ucred *a_cred,
		int           a_flags
	} */ *ap;
{
	register struct vnode *vp = ap->a_vp;
	upl_t pl = ap->a_pl;
	size_t size= ap->a_size;
	off_t f_offset = ap->a_f_offset;
	vm_offset_t pl_offset = ap->a_pl_offset;
	int flags  = ap->a_flags;
	register struct inode *ip;
	int devBlockSize=0;
	int error;

	ip = VTOI(vp);

	/* check pageins for reg file only  and ubc info is present*/
	if  (UBCINVALID(vp))
		panic("ext2_pagein: Not a  VREG: vp=%x", vp);
	if (UBCINFOMISSING(vp))
		panic("ext2_pagein: No mapping: vp=%x", vp);

#if DIAGNOSTIC
	if (vp->v_type == VLNK) {
		if ((int)ip->i_size < vp->v_mount->mnt_maxsymlinklen)
			panic("%s: short symlink", "ext2_pagein");
	} else if (vp->v_type != VREG && vp->v_type != VDIR)
		panic("%s: type %d", "ext2_pagein", vp->v_type);
#endif

	VOP_DEVBLOCKSIZE(ip->i_devvp, &devBlockSize);

  	error = cluster_pagein(vp, pl, pl_offset, f_offset, size,
			    (off_t)ip->i_size, devBlockSize, flags);
	/* ip->i_flag |= IN_ACCESS; */
	return (error);
}

/*
 * Vnode op for pageout.
 * Similar to ffs_write()
 * make sure the buf is not in hash queue when you return
 */
int
ext2_pageout(ap)
	struct vop_pageout_args /* {
	   struct vnode *a_vp,
	   upl_t        a_pl,
	   vm_offset_t   a_pl_offset,
	   off_t         a_f_offset,
	   size_t        a_size,
	   struct ucred *a_cred,
	   int           a_flags
	} */ *ap;
{
	register struct vnode *vp = ap->a_vp;
	upl_t pl = ap->a_pl;
	size_t size= ap->a_size;
	off_t f_offset = ap->a_f_offset;
	vm_offset_t pl_offset = ap->a_pl_offset;
	int flags  = ap->a_flags;
	register struct inode *ip;
	register FS *fs;
	int error ;
	int devBlockSize=0;
	size_t xfer_size = 0;
	int local_flags=0;
	off_t local_offset;
	int resid, blkoffset;
	size_t xsize/*, lsize*/;
	daddr_t lbn;
	int save_error =0, save_size=0;
	vm_offset_t lupl_offset;
	int nocommit = flags & UPL_NOCOMMIT;
	/*struct buf *bp;*/

	ip = VTOI(vp);

	/* check pageouts for reg file only  and ubc info is present*/
	if  (UBCINVALID(vp))
		panic("ext2_pageout: Not a  VREG: vp=%x", vp);
	if (UBCINFOMISSING(vp))
		panic("ext2_pageout: No mapping: vp=%x", vp);

        if (vp->v_mount->mnt_flag & MNT_RDONLY) {
		if (!nocommit)
  			ubc_upl_abort_range(pl, pl_offset, size, 
				UPL_ABORT_FREE_ON_EMPTY);
		return (EROFS);
	}
	fs = ip->I_FS;

	if (f_offset < 0 || f_offset >= ip->i_size) {
	        if (!nocommit)
		        ubc_upl_abort_range(pl, pl_offset, size, 
				UPL_ABORT_FREE_ON_EMPTY);
		return (EINVAL);
	}

	/*
	 * once we enable multi-page pageouts we will
	 * need to make sure we abort any pages in the upl
	 * that we don't issue an I/O for
	 */
	if (f_offset + size > ip->i_size)
	        xfer_size = ip->i_size - f_offset;
	else
	        xfer_size = size;

	VOP_DEVBLOCKSIZE(ip->i_devvp, &devBlockSize);

	if (xfer_size & (PAGE_SIZE - 1)) {
	        /* if not a multiple of page size
		 * then round up to be a multiple
		 * the physical disk block size
		 */
		xfer_size = (xfer_size + (devBlockSize - 1)) & ~(devBlockSize - 1);
	}

	/*
	 * once the block allocation is moved to ext2_cmap
	 * we can remove all the size and offset checks above
	 * cluster_pageout does all of this now
	 * we need to continue to do it here so as not to
	 * allocate blocks that aren't going to be used because
	 * of a bogus parameter being passed in
	 */
	local_flags = 0;
	resid = xfer_size;
	local_offset = f_offset;
	for (error = 0; resid > 0;) {
		lbn = lblkno(fs, local_offset);
		blkoffset = blkoff(fs, local_offset);
		xsize = EXT2_BLOCK_SIZE(fs) - blkoffset;
		if (resid < xsize)
			xsize = resid;
		/* Allocate block without reading into a buf */
		error = ext2_blkalloc(ip,
			lbn, blkoffset + xsize, ap->a_cred, 
			local_flags);
		if (error)
			break;
		resid -= xsize;
		local_offset += (off_t)xsize;
	}

	if (error) {
		save_size = resid;
		save_error = error;
		xfer_size -= save_size;
	}

	error = cluster_pageout(vp, pl, pl_offset, f_offset, round_page(xfer_size), ip->i_size, devBlockSize, flags);

	if(save_error) {
		lupl_offset = size - save_size;
		resid = round_page(save_size);
		if (!nocommit)
			ubc_upl_abort_range(pl, lupl_offset, resid,
				UPL_ABORT_FREE_ON_EMPTY);
		if(!error)
			error= save_error;
	}
	return (error);
}

/*
 * Cmap converts a the file offset of a file to its physical block
 * number on the disk And returns  contiguous size for transfer.
 */
int
ext2_cmap(ap)
	struct vop_cmap_args /* {
		struct vnode *a_vp;
		off_t a_foffset;    
		size_t a_size;
		daddr_t *a_bpn;
		size_t *a_run;
		void *a_poff;
	} */ *ap;
{
	struct vnode * vp = ap->a_vp;
	int32_t *bnp = ap->a_bpn;
	size_t *runp = ap->a_run;
	int size = ap->a_size;
	daddr_t bn;
	int nblks;
	register struct inode *ip;
	int32_t daddr = 0;
	int devBlockSize=0;
	FS *fs;
	int retsize=0;
	int error=0;

	ip = VTOI(vp);
	fs = ip->i_e2fs;
	

	if (blkoff(fs, ap->a_foffset)) {
		panic("ext2_cmap; allocation requested inside a block");
	}

	bn = (daddr_t)lblkno(fs, ap->a_foffset);
	VOP_DEVBLOCKSIZE(ip->i_devvp, &devBlockSize);

	if (size % devBlockSize) {
		panic("ext2_cmap: size is not multiple of device block size\n");
	}

	if (error =  VOP_BMAP(vp, bn, (struct vnode **) 0, &daddr, &nblks)) {
			return(error);
	}

	retsize = nblks * EXT2_BLOCK_SIZE(fs);

	if (bnp)
		*bnp = daddr;

	if (ap->a_poff) 
		*(int *)ap->a_poff = 0;

	if (daddr == -1) {
		if (size < EXT2_BLOCK_SIZE(fs)) {
			retsize = fragroundup(fs, size);
			if(size >= retsize)
				*runp = retsize;
			else
				*runp = size;
		} else {
			*runp = EXT2_BLOCK_SIZE(fs);
		}
		return(0);
	}

	if (runp) {
		if ((size < EXT2_BLOCK_SIZE(fs))) {
			*runp = size;
			return(0);
		}
		if (retsize) {
			retsize += EXT2_BLOCK_SIZE(fs);
			if(size >= retsize)
				*runp = retsize;
			else
				*runp = size;
		} else {
			if (size < EXT2_BLOCK_SIZE(fs)) {
				retsize = fragroundup(fs, size);
				if(size >= retsize)
					*runp = retsize;
				else
					*runp = size;
			} else {
				*runp = EXT2_BLOCK_SIZE(fs);
			}
		}
	}
	return (0);
}

/*
 * ext2_blkalloc allocates a disk block for ext2_pageout(), as a consequence
 * it does no breads (that could lead to deadblock as the page may be already
 * marked busy as it is being paged out. Also important to note that we are not
 * growing the file in pageouts. So ip->i_size  cannot increase by this call
 * due to the way UBC works.  
 * This code is derived from ffs_balloc and many cases of that are  dealt
 * in ffs_balloc are not applicable here 
 * Do not call with B_CLRBUF flags as this should only be called only 
 * from pageouts
 */
int
ext2_blkalloc(ip, lbn, size, cred, flags)
	register struct inode *ip;
	int32_t lbn;
	int size;
	struct ucred *cred;
	int flags;
{
	register FS *fs;
	register int32_t nb;
	struct buf *bp, *nbp;
	struct vnode *vp = ITOV(ip);
	struct indir indirs[NIADDR + 2];
	int32_t newb, *bap, pref;
	int deallocated, osize, nsize, num, i, error;
	int32_t *allocib, *blkp, *allocblk, allociblk[NIADDR + 1];
	int devBlockSize=0;

	fs = ip->i_e2fs;

	if(size > EXT2_BLOCK_SIZE(fs))
		panic("ext2_blkalloc: too large for allocation\n");

	/*
	 * If the next write will extend the file into a new block,
	 * and the file is currently composed of a fragment
	 * this fragment has to be extended to be a full block.
	 */
	nb = lblkno(fs, ip->i_size);
	if (nb < NDADDR && nb < lbn) {
		panic("ext2_blkalloc():cannot extend file: i_size %d, lbn %d\n", ip->i_size, lbn);
	}
	/*
	 * The first NDADDR blocks are direct blocks
	 */
	if (lbn < NDADDR) {
		nb = ip->i_db[lbn];
		if (nb != 0 && ip->i_size >= (lbn + 1) * EXT2_BLOCK_SIZE(fs)) {
		/* TBD: trivial case; the block  is already allocated */
			return (0);
		}
		if (nb != 0) {
			/*
			 * Consider need to reallocate a fragment.
			 */
			osize = fragroundup(fs, blkoff(fs, ip->i_size));
			nsize = fragroundup(fs, size);
			if (nsize > osize) {
				panic("ffs_allocblk: trying to extend a fragment \n");
			}
			return(0);
		} else {
			if (ip->i_size < (lbn + 1) * EXT2_BLOCK_SIZE(fs))
				nsize = fragroundup(fs, size);
			else
				nsize = EXT2_BLOCK_SIZE(fs);
			error = ext2_alloc(ip, lbn,
			    ext2_blkpref(ip, lbn, (int)lbn, &ip->i_db[0], 0),
			    nsize, cred, &newb);
			if (error)
				return (error);
			ip->i_db[lbn] = newb;
			ip->i_flag |= IN_CHANGE | IN_UPDATE;
			return (0);
		}
	}
	/*
	 * Determine the number of levels of indirection.
	 */
	pref = 0;
	if (error = ext2_getlbns(vp, lbn, indirs, &num))
		return(error);

	if(num == 0) {
		panic("ext2_blkalloc: file with direct blocks only\n"); 
	}

	/*
	 * Fetch the first indirect block allocating if necessary.
	 */
	--num;
	nb = ip->i_ib[indirs[0].in_off];
	allocib = NULL;
	allocblk = allociblk;
	if (nb == 0) {
		pref = ext2_blkpref(ip, lbn, 0, (int32_t *)0, 0);
	        if (error = ext2_alloc(ip, lbn, pref, (int)EXT2_BLOCK_SIZE(fs),
		    cred, &newb))
			return (error);
		nb = newb;
		*allocblk++ = nb;
		bp = getblk(vp, indirs[1].in_lbn, EXT2_BLOCK_SIZE(fs), 0, 0, BLK_META);
		bp->b_blkno = fsbtodb(fs, nb);
		clrbuf(bp);
		/*
		 * Write synchronously so that indirect blocks
		 * never point at garbage.
		 */
		if (error = bwrite(bp))
			goto fail;
		allocib = &ip->i_ib[indirs[0].in_off];
		*allocib = nb;
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
	}
	/*
	 * Fetch through the indirect blocks, allocating as necessary.
	 */
	for (i = 1;;) {
		error = meta_bread(vp,
		    indirs[i].in_lbn, (int)EXT2_BLOCK_SIZE(fs), NOCRED, &bp);
		if (error) {
			brelse(bp);
			goto fail;
		}
		bap = (int32_t *)bp->b_data;
		nb = bap[indirs[i].in_off];

		if (i == num)
			break;
		i += 1;
		if (nb != 0) {
			brelse(bp);
			continue;
		}
		if (pref == 0)
			pref = ext2_blkpref(ip, lbn, 0, (int32_t *)0, 0);
		if (error =
		    ext2_alloc(ip, lbn, pref, (int)EXT2_BLOCK_SIZE(fs), cred, &newb)) {
			brelse(bp);
			goto fail;
		}
		nb = newb;
		*allocblk++ = nb;
		nbp = getblk(vp, indirs[i].in_lbn, EXT2_BLOCK_SIZE(fs), 0, 0, BLK_META);
		nbp->b_blkno = fsbtodb(fs, nb);
		clrbuf(nbp);
		/*
		 * Write synchronously so that indirect blocks
		 * never point at garbage.
		 */
		if (error = bwrite(nbp)) {
			brelse(bp);
			goto fail;
		}

		bap[indirs[i - 1].in_off] = nb;

		/*
		 * If required, write synchronously, otherwise use
		 * delayed write.
		 */
		if (flags & B_SYNC) {
			bwrite(bp);
		} else {
			bdwrite(bp);
		}
	}
	/*
	 * Get the data block, allocating if necessary.
	 */
	if (nb == 0) {
		pref = ext2_blkpref(ip, lbn, indirs[i].in_off, &bap[0], 0);
		if (error = ext2_alloc(ip,
		    lbn, pref, (int)EXT2_BLOCK_SIZE(fs), cred, &newb)) {
			brelse(bp);
			goto fail;
		}
		nb = newb;
		*allocblk++ = nb;

		bap[indirs[i].in_off] = nb;

		/*
		 * If required, write synchronously, otherwise use
		 * delayed write.
		 */
		if (flags & B_SYNC) {
			bwrite(bp);
		} else {
			bdwrite(bp);
		}
		return (0);
	}
	brelse(bp);
	return (0);
fail:
	/*
	 * If we have failed part way through block allocation, we
	 * have to deallocate any indirect blocks that we have allocated.
	 */
	for (deallocated = 0, blkp = allociblk; blkp < allocblk; blkp++) {
		ext2_blkfree(ip, *blkp, EXT2_BLOCK_SIZE(fs));
		deallocated += EXT2_BLOCK_SIZE(fs);
	}
	if (allocib != NULL)
		*allocib = 0;
	if (deallocated) {
	VOP_DEVBLOCKSIZE(ip->i_devvp,&devBlockSize);

#if QUOTA
		/*
		 * Restore user's disk quota because allocation failed.
		 */
		(void) chkdq(ip, (int64_t)-deallocated, cred, FORCE);
#endif /* QUOTA */
		ip->i_blocks -= btodb(deallocated, devBlockSize);
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
	}
	return (error);
}

/*
 * Mmap a file
 *
 * NB Currently unsupported.
 */
/* ARGSUSED */
int
ext2_mmap(ap)
	struct vop_mmap_args /* {
		struct vnode *a_vp;
		int  a_fflags;
		struct ucred *a_cred;
		struct proc *a_p;
	} */ *ap;
{

	return (EINVAL);
}

__private_extern__ int
ext2_blktooff (ap)
   struct vop_blktooff_args *ap;
{
   
   struct inode *ip;
	struct ext2_sb_info *fs;
   daddr_t bn = ap->a_lblkno; 
   
   if ((long)bn < 0) {
		panic("-ve blkno in ext2_blktooff");
		bn = -(long)bn;
	}
   
	ip = VTOI(ap->a_vp);
	fs = ip->i_e2fs;
	*ap->a_offset = lblktosize(fs, bn);
   
   return (0);
}

__private_extern__ int
ext2_offtoblk (ap)
   struct vop_offtoblk_args *ap;
{
   
   struct inode *ip;
	struct ext2_sb_info *fs;
   
   if (ap->a_vp == NULL)
		return (EINVAL);
   
   ip = VTOI(ap->a_vp);
	fs = ip->i_e2fs;
	*ap->a_lblkno = lblkno(fs, ap->a_offset);
   
   return (0);
}

/* Derived from hfs_cache_lookup() */
__private_extern__ int
ext2_cache_lookup(ap)
	struct vop_lookup_args /* {
		struct vnode *a_dvp;
		struct vnode **a_vpp;
		struct componentname *a_cnp;
	} */ *ap;
{
   struct vnode *dvp;
	struct vnode *vp;
	int lockparent; 
	int error;
	struct vnode **vpp = ap->a_vpp;
	struct componentname *cnp = ap->a_cnp;
	struct ucred *cred = cnp->cn_cred;
	int flags = cnp->cn_flags;
	struct proc *p = cnp->cn_proc;
   
   *vpp = NULL;
	dvp = ap->a_dvp;
	lockparent = flags & LOCKPARENT;

	/*
	 * Check accessiblity of directory.
	 */
	if (dvp->v_type != VDIR)
		return (ENOTDIR);
	if ((flags & ISLASTCN) && (dvp->v_mount->mnt_flag & MNT_RDONLY) &&
	    (cnp->cn_nameiop == DELETE || cnp->cn_nameiop == RENAME))
		return (EROFS);
	if ((error = VOP_ACCESS(dvp, VEXEC, cred, cnp->cn_proc)))
		return (error);
   
   /*
	 * Lookup an entry in the cache
	 * If the lookup succeeds, the vnode is returned in *vpp, and a status of -1 is
	 * returned. If the lookup determines that the name does not exist
	 * (negative cacheing), a status of ENOENT is returned. If the lookup
	 * fails, a status of zero is returned.
	 */
	error = cache_lookup(dvp, vpp, cnp);
	if (error == 0)  {		/* Unsuccessfull */
		error = ext2_lookup((struct vop_cachedlookup_args*)ap);
		return (error);
	}
	
	if (error == ENOENT)
		return (error);
	
	/* We have a name that matched */
	vp = *vpp;
   
   if (dvp == vp) {	/* lookup on "." */
		VREF(vp);
		error = 0;
	} else if (flags & ISDOTDOT) {
		/* 
		 * Carefull on the locking policy,
		 * remember we always lock from parent to child, so have
		 * to release lock on child before trying to lock parent
		 * then regain lock if needed
		 */
		VOP_UNLOCK(dvp, 0, p);
		error = vget(vp, LK_EXCLUSIVE, p);
		if (!error && lockparent && (flags & ISLASTCN))
			error = vn_lock(dvp, LK_EXCLUSIVE, p);
	} else {
		error = vget(vp, LK_EXCLUSIVE, p);
		if (!lockparent || error || !(flags & ISLASTCN))
			VOP_UNLOCK(dvp, 0, p);
	}
   
   if (!error)
      return (0);
   
   /* Error condition */
   
   vput(vp);
   if (lockparent && (dvp != vp) && (flags & ISLASTCN))
      VOP_UNLOCK(dvp, 0, p);
   
   if ((error = vn_lock(dvp, LK_EXCLUSIVE, p)))
		return (error);

	return (ext2_lookup((struct vop_cachedlookup_args*)ap));
}
