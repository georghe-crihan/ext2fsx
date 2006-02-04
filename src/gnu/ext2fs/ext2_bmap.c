/*
 * Copyright (c) 1989, 1991, 1993
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
 *	@(#)ufs_bmap.c	8.7 (Berkeley) 3/21/95
 * $FreeBSD: src/sys/gnu/ext2fs/ext2_bmap.c,v 1.53 2003/01/03 06:32:14 phk Exp $
 */

static const char whatid[] __attribute__ ((unused)) =
"@(#) $Id$";

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/resourcevar.h>
#include <sys/stat.h>

//#include <sys/trace.h>
#include "ext2_apple.h"

#include <gnu/ext2fs/ext2_fs.h> /* For linux type defines. */
#include <gnu/ext2fs/fs.h>
#include <gnu/ext2fs/inode.h>
#include <gnu/ext2fs/ext2_mount.h>
#include <gnu/ext2fs/ext2_extern.h>
#include <gnu/ext2fs/ext2_fs_sb.h>
#include <ext2_byteorder.h>

#ifdef obsolete
/*
 * Bmap converts a the logical block number of a file to its physical block
 * number on the disk. The conversion is done by using the logical block
 * number to index into the array of block pointers described by the dinode.
 */
int
ext2_bmap(ap)
	struct vop_bmap_args /* {
		vnode_t a_vp;
        off_t a_foffset;
        size_t a_size;
		daddr64_t *a_bpn;
        size_t *a_run;
        void *a_poff;
        int a_flags;
        vfs_context_t a_context;
	} */ *ap;
{
	struct ext2_sb_info *fs;
    ext2_daddr_t blkno, bn;
	int error;

	/*
	 * Check for underlying vnode requests and ensure that logical
	 * to physical mapping is requested.
	 */
#ifdef obsolete
    if (ap->a_vpp != NULL)
		*ap->a_vpp = VTOI(ap->a_vp)->i_devvp;
#endif
	if (ap->a_bpn == NULL)
		return (0);
   
    ext2_trace_enter();

	fs = VTOI(ap->a_vp)->i_e2fs;
    if ((error = blkoff(fs, ap->a_foffset))) {
		panic("ext2_bmap: allocation requested inside a block (possible filesystem corruption): "
         "qbmask=%qd, inode=%u, offset=%qu, blkoff=%d",
         fs->s_qbmask, VTOI(ap->a_vp)->i_number, ap->a_foffset, error);
	}
    
    bn = (ext2_daddr_t)lblkno(fs, ap->a_foffset);
    error = ext2_bmaparray(ap->a_vp, bn, &blkno, ap->a_run, NULL);
	*ap->a_bpn = (daddr64_t)blkno;
    ext2_trace("returning dblock: %d for lblock: %d in inode %u\n", blkno, bn,
        VTOI(ap->a_vp)->i_number);
	return (error);
}
#endif

/*
 * Indirect blocks are now on the vnode for the file.  They are given negative
 * logical block numbers.  Indirect blocks are addressed by the negative
 * address of the first data block to which they point.  Double indirect blocks
 * are addressed by one less than the address of the first indirect block to
 * which they point.  Triple indirect blocks are addressed by one less than
 * the address of the first double indirect block to which they point.
 *
 * ext2_bmaparray does the bmap conversion, and if requested returns the
 * array of logical blocks which must be traversed to get to a block.
 * Each entry contains the offset into that block that gets you to the
 * next block and the disk address of the block (if it is assigned).
 */

int
ext2_bmaparray(vp, bn, bnp, runp, runb)
	vnode_t vp;
	ext2_daddr_t bn;
	ext2_daddr_t *bnp;
	int *runp;
	int *runb;
{
	struct vfsioattr vfsio;
    struct inode *ip;
	buf_t  bp;
	struct ext2mount *ump;
	mount_t  mp;
	vnode_t devvp;
	struct indir a[NIADDR+1], *ap;
	ext2_daddr_t daddr;
	ext2_daddr_t metalbn;
	size_t maxrun = 0;
    ext2_daddr_t *bap;
	int num, *nump, error;
    long iosize;

	ap = NULL;
	ip = VTOI(vp);
	mp = ITOVFS(ip);
	ump = VFSTOEXT2(mp);
	devvp = ump->um_devvp;
    
    vfs_ioattr(mp, &vfsio);
    iosize = vfs_statfs(mp)->f_iosize;

	if (runp) {
		maxrun = vfsio.io_maxwritecnt / iosize - 1;
		*runp = 0;
	}

	if (runb) {
		*runb = 0;
	}

	ap = a;
	nump = &num;
	error = ext2_getlbns(vp, bn, ap, nump);
	if (error)
		return (error);

	num = *nump;
	if (num == 0) {
		ISLOCK(ip);
        *bnp = blkptrtodb(ump, ip->i_db[bn]);
		if (*bnp == 0) {
			*bnp = -1;
		} else if (runp) {
			int32_t bnb = bn;
			for (++bn; bn < NDADDR && *runp < maxrun &&
			    is_sequential(ump, ip->i_db[bn - 1], ip->i_db[bn]);
			    ++bn, ++*runp);
			bn = bnb;
			if (runb && (bn > 0)) {
				for (--bn; (bn >= 0) && (*runb < maxrun) &&
					is_sequential(ump, ip->i_db[bn],
						ip->i_db[bn+1]);
						--bn, ++*runb);
			}
		}
        IULOCK(ip);
		return (0);
	}


	/* Get disk address out of indirect block array */
    ISLOCK(ip);
	daddr = ip->i_ib[ap->in_off];
    IULOCK(ip);

	for (bp = NULL, ++ap; --num; ++ap) {
		/*
		 * Exit the loop if there is no disk address assigned yet and
		 * the indirect block isn't in the cache, or if we were
		 * looking for an indirect block and we've found it.
		 */

		metalbn = ap->in_lbn;
		if ((daddr == 0 && !incore(vp, (daddr64_t)metalbn, BLK_META)) || metalbn == bn)
			break;
		/*
		 * If we get here, we've either got the block in the cache
		 * or we have a disk address for it, go fetch it.
		 */
		if (bp)
			bqrelse(bp);

		ap->in_exists = 1;
		bp = buf_getblk(vp, (daddr64_t)metalbn, iosize, 0, 0, BLK_META);

#ifdef obsolete
        if (buf_flags(bp) & (/* B_DONE | */ B_DELWRI)) {
			trace(TR_BREADHIT, pack(vp, iosize), metalbn);
		}
#endif

#ifdef DIAGNOSTIC
      //else
        if (!daddr)
            panic("ext2_bmaparray: indirect block not in cache");
#endif

#ifdef obsolete
        else {
            trace(TR_BREADMISS, pack(vp, iosize), metalbn);
#endif
        {
            buf_setblkno(bp, (daddr64_t)blkptrtodb(ump, daddr));
            buf_setflags(bp, B_READ);
#ifdef obsolete
            bp->b_flags &= ~B_ERROR;
#endif
			struct vnop_strategy_args vsargs;
            vsargs.a_desc = &vnop_strategy_desc;
            vsargs.a_bp = bp;
            buf_strategy(vp, &vsargs);
#ifdef obsolete
			curproc->p_stats->p_ru.ru_inblock++;	/* XXX */
#endif
			error = bufwait(bp);
			if (error) {
				buf_brelse(bp);
				return (error);
			}
		}

        ISLOCK(ip);
		bap = (ext2_daddr_t *)buf_dataptr(bp);
        daddr = le32_to_cpu(bap[ap->in_off]);
		if (num == 1 && daddr && runp) {
			for (bn = ap->in_off + 1;
			    bn < MNINDIR(ump) && *runp < maxrun &&
			    is_sequential(ump,
                le32_to_cpu(bap[bn - 1]),
                le32_to_cpu(bap[bn]));
			    ++bn, ++*runp);
			bn = ap->in_off;
			if (runb && bn) {
				for(--bn; bn >= 0 && *runb < maxrun &&
			    		is_sequential(ump,
                        le32_to_cpu(bap[bn]),
					    le32_to_cpu(bap[bn+1]));
			    		--bn, ++*runb);
			}
		}
        IULOCK(ip);
	}
	if (bp)
		bqrelse(bp);
   
	*bnp = blkptrtodb(ump, daddr);
	if (*bnp == 0) {
		*bnp = -1;
	}
	return (0);
}

/*
 * Create an array of logical block number/offset pairs which represent the
 * path of indirect blocks required to access a data block.  The first "pair"
 * contains the logical block number of the appropriate single, double or
 * triple indirect block and the offset into the inode indirect block array.
 * Note, the logical block number of the inode single/double/triple indirect
 * block appears twice in the array, once with the offset into the i_ib and
 * once with the offset into the page itself.
 */
int
ext2_getlbns(vp, bn, ap, nump)
	vnode_t vp;
	ext2_daddr_t bn;
	struct indir *ap;
	int *nump;
{
	ext2_daddr_t blockcnt, metalbn, realbn;
	struct ext2mount *ump;
	int i, numlevels, off;
	int64_t qblockcnt;

	ump = VFSTOEXT2(vnode_mount(vp));
	if (nump)
		*nump = 0;
	numlevels = 0;
	realbn = bn;
	if ((long)bn < 0)
		bn = -(long)bn;

	/* The first NDADDR blocks are direct blocks. */
	if (bn < NDADDR)
		return (0);

	/*
	 * Determine the number of levels of indirection.  After this loop
	 * is done, blockcnt indicates the number of data blocks possible
	 * at the previous level of indirection, and NIADDR - i is the number
	 * of levels of indirection needed to locate the requested block.
	 */
	for (blockcnt = 1, i = NIADDR, bn -= NDADDR;; i--, bn -= blockcnt) {
		if (i == 0)
			return (EFBIG);
		/*
		 * Use int64_t's here to avoid overflow for triple indirect
		 * blocks when longs have 32 bits and the block size is more
		 * than 4K.
		 */
		qblockcnt = (int64_t)blockcnt * MNINDIR(ump);
		if (bn < qblockcnt)
			break;
		blockcnt = qblockcnt;
	}

	/* Calculate the address of the first meta-block. */
	if (realbn >= 0)
		metalbn = -(realbn - bn + NIADDR - i);
	else
		metalbn = -(-realbn - bn + NIADDR - i);

	/*
	 * At each iteration, off is the offset into the bap array which is
	 * an array of disk addresses at the current level of indirection.
	 * The logical block number and the offset in that block are stored
	 * into the argument array.
	 */
	ap->in_lbn = metalbn;
	ap->in_off = off = NIADDR - i;
	ap->in_exists = 0;
	ap++;
	for (++numlevels; i <= NIADDR; i++) {
		/* If searching for a meta-data block, quit when found. */
		if (metalbn == realbn)
			break;

        off = (bn / blockcnt) % MNINDIR(ump);

		++numlevels;
		ap->in_lbn = metalbn;
		ap->in_off = off;
		ap->in_exists = 0;
		++ap;

		metalbn -= -1 + off * blockcnt;
        blockcnt /= MNINDIR(ump);
	}
	if (nump)
		*nump = numlevels;
	return (0);
}
