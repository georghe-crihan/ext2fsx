/*
 *  modified for Lites 1.1
 *
 *  Aug 1995, Godmar Back (gback@cs.utah.edu)
 *  University of Utah, Department of Computer Science
 */
/*
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
 *	@(#)ufs_readwrite.c	8.7 (Berkeley) 1/21/94
 * $FreeBSD: src/sys/gnu/ext2fs/ext2_readwrite.c,v 1.25 2002/05/16 19:43:28 iedowse Exp $
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

static const char whatid[] __attribute__ ((unused)) =
"@(#) $Id$";

#define	BLKSIZE(a, b, c)	blksize(a, b, c)
#define	FS			struct ext2_sb_info
#define	I_FS			i_e2fs
#define	READ			ext2_read
#define	READ_S			"ext2_read"
#define	WRITE			ext2_write
#define	WRITE_S			"ext2_write"

/*
 * Vnode op for reading.
 */
/* ARGSUSED */
static int
READ(ap)
	struct vop_read_args /* {
		struct vnode *a_vp;
		struct uio *a_uio;
		int a_ioflag;
		struct ucred *a_cred;
	} */ *ap;
{
	struct vnode *vp;
	struct inode *ip;
	struct uio *uio;
	FS *fs;
	struct buf *bp;
	ext2_daddr_t lbn, nextlbn;
	off_t bytesinfile;
	long size, xfersize, blkoffset;
	int error, orig_resid;
	u_short mode;
   
    ext2_trace_enter();

	vp = ap->a_vp;
	ip = VTOI(vp);
	mode = ip->i_mode;
	uio = ap->a_uio;

#ifdef DIAGNOSTIC
	if (uio->uio_rw != UIO_READ)
		panic("%s: invalid uio_rw = %x", READ_S, uio->uio_rw);

	if (vp->v_type == VLNK) {
		if ((int)ip->i_size < vp->v_mount->mnt_maxsymlinklen)
			panic("%s: short symlink", READ_S);
	} else if (vp->v_type != VREG && vp->v_type != VDIR)
		panic("%s: invalid v_type %d", READ_S, vp->v_type);
#endif
	fs = ip->I_FS;
    if (uio->uio_offset < 0)
		ext2_trace_return (EINVAL);

	if ((u_quad_t)uio->uio_offset > fs->s_maxfilesize)
		ext2_trace_return (EFBIG);

	orig_resid = uio->uio_resid;
    if (UBCISVALID(vp)) {
		bp = NULL; /* So we don't try to free it later. */
        error = cluster_read(vp, uio, (off_t)ip->i_size, 
			ip->i_e2fs->s_d_blocksize, 0);
	} else {
	for (error = 0, bp = NULL; uio->uio_resid > 0; bp = NULL) {
		if ((bytesinfile = ip->i_size - uio->uio_offset) <= 0)
			break;
		lbn = lblkno(fs, uio->uio_offset);
		nextlbn = lbn + 1;
		size = BLKSIZE(fs, ip, lbn);
		blkoffset = blkoff(fs, uio->uio_offset);

		xfersize = EXT2_BLOCK_SIZE(fs) - blkoffset;
		if (uio->uio_resid < xfersize)
			xfersize = uio->uio_resid;
		if (bytesinfile < xfersize)
			xfersize = bytesinfile;

		if (lblktosize(fs, nextlbn) >= ip->i_size)
			error = bread(vp, lbn, size, NOCRED, &bp);
		else if (lbn - 1 == vp->v_lastr && !(vp->v_flag & VRAOFF)) {
			int nextsize = BLKSIZE(fs, ip, nextlbn);
			error = breadn(vp, lbn,
			    size, &nextlbn, &nextsize, 1, NOCRED, &bp);
		} else
			error = bread(vp, lbn, size, NOCRED, &bp);
		if (error) {
			brelse(bp);
			bp = NULL;
			break;
		}
        vp->v_lastr = lbn;

		/*
		 * We should only get non-zero b_resid when an I/O error
		 * has occurred, which should cause us to break above.
		 * However, if the short read did not cause an error,
		 * then we want to ensure that we do not uiomove bad
		 * or uninitialized data.
		 */
		size -= bp->b_resid;
		if (size < xfersize) {
			if (size == 0)
				break;
			xfersize = size;
		}
		error =
		    uiomove((char *)bp->b_data + blkoffset, (int)xfersize, uio);
		if (error)
			break;
      
      if (S_ISREG(mode) && (xfersize + blkoffset == EXT2_BLOCK_SIZE(fs) ||
		    uio->uio_offset == ip->i_size))
			bp->b_flags |= B_AGE;

		bqrelse(bp);
	}
   } /* if (UBCISVALID(vp)) */
	if (bp != NULL)
		bqrelse(bp);
	if (orig_resid > 0 && (error == 0 || uio->uio_resid != orig_resid) &&
	    (vp->v_mount->mnt_flag & MNT_NOATIME) == 0)
		ip->i_flag |= IN_ACCESS;
	ext2_trace_return(error);
}

/*
 * Vnode op for writing.
 */
static int
WRITE(ap)
	struct vop_write_args /* {
		struct vnode *a_vp;
		struct uio *a_uio;
		int a_ioflag;
		struct ucred *a_cred;
	} */ *ap;
{
	struct vnode *vp;
	struct uio *uio;
	struct inode *ip;
	FS *fs;
	struct buf *bp;
	struct thread *td;
	ext2_daddr_t lbn;
	off_t osize;
	int blkoffset, error, flags, ioflag, resid, size, xfersize;
    int rsd, blkalloc=0, save_error=0, save_size=0;
    int file_extended = 0, dodir=0;

	ioflag = ap->a_ioflag;
	uio = ap->a_uio;
	vp = ap->a_vp;
	ip = VTOI(vp);

#ifdef DIAGNOSTIC
	if (uio->uio_rw != UIO_WRITE)
		panic("%s: uio_rw = %x\n", WRITE_S, uio->uio_rw);
#endif

	switch (vp->v_type) {
	case VREG:
		if (ioflag & IO_APPEND)
			uio->uio_offset = ip->i_size;
		if ((ip->i_flags & APPEND) && uio->uio_offset != ip->i_size)
			ext2_trace_return(EPERM);
		/* FALLTHROUGH */
	case VLNK:
		break;
	case VDIR:
		dodir = 1;
        if ((ioflag & IO_SYNC) == 0)
			panic("%s: nonsync dir write", WRITE_S);
		break;
	default:
		panic("%s: type", WRITE_S);
	}

	fs = ip->I_FS;

	ext2_trace(" nm=%s,inode=%d,isize=%qu,woff=%qu,wsize=%d\n",
        VNAME(vp), ip->i_number, ip->i_size, uio->uio_offset, uio->uio_resid);
    
    if (uio->uio_offset < 0 ||
	    (u_quad_t)uio->uio_offset + uio->uio_resid > fs->s_maxfilesize)
		ext2_trace_return(EFBIG);
    if (uio->uio_resid == 0)
        return (0);
   
	/*
	 * Maybe this should be above the vnode op call, but so long as
	 * file servers have no limits, I don't think it matters.
	 */
	td = uio->uio_td;
	if (vp->v_type == VREG && td &&
	    uio->uio_offset + uio->uio_resid >
        td->p_rlimit[RLIMIT_FSIZE].rlim_cur) {
        psignal(td, SIGXFSZ);
		ext2_trace_return(EFBIG);
	}

	resid = uio->uio_resid;
	osize = ip->i_size;
	flags = ioflag & IO_SYNC ? B_SYNC : 0;
   
   if (UBCISVALID(vp)) {
      off_t filesize, endofwrite, local_offset, head_offset;
      int local_flags, first_block, fboff, fblk, loopcount;
   
      endofwrite = uio->uio_offset + uio->uio_resid;
   
      if (endofwrite > ip->i_size) {
         filesize = endofwrite;
         file_extended = 1;
      } else 
         filesize = ip->i_size;
   
      head_offset = ip->i_size;
   
      /* Go ahead and allocate the blocks that are going to be written */
      rsd = uio->uio_resid;
      local_offset = uio->uio_offset;
      local_flags = ioflag & IO_SYNC ? B_SYNC : 0;
      local_flags |= B_NOBUFF;

      first_block = 1;
      fboff = 0;
      fblk = 0;
      loopcount = 0;
      /* There's no need for this, since we can't get here with rsd == 0, but the compiler
        is not smart enough to figure this out and warns of possible badness. */
      xfersize = 0;
   
      for (error = 0; rsd > 0;) {
         blkalloc = 0;
         lbn = lblkno(fs, local_offset);
         blkoffset = blkoff(fs, local_offset);
         xfersize = EXT2_BLOCK_SIZE(fs) - blkoffset;
         if (first_block)
            fboff = blkoffset;
         if (rsd < xfersize)
            xfersize = rsd;
         if (EXT2_BLOCK_SIZE(fs) > xfersize)
            local_flags |= B_CLRBUF;
         else
            local_flags &= ~B_CLRBUF;
         
         /* Allocate block without reading into a buf (B_NOBUFF) */
         error = ext2_balloc2(ip,
            lbn, blkoffset + xfersize, ap->a_cred, 
            &bp, local_flags, &blkalloc);
         if (error)
            break;
         if (first_block) {
            fblk = blkalloc;
            first_block = 0;
         }
         loopcount++;
   
         rsd -= xfersize;
         local_offset += (off_t)xfersize;
         if (local_offset > ip->i_size)
            ip->i_size = local_offset;
      }
   
      if(error) {
         save_error = error;
         save_size = rsd;
         uio->uio_resid -= rsd;
         if (file_extended)
            filesize -= rsd;
      }
   
      flags = ioflag & IO_SYNC ? IO_SYNC : 0;
      /* UFS does not set this flag, but if we don't then 
         previously written data is zero filled and the file is corrupt.
         Perhaps a difference in balloc()?
      */
      flags |= IO_NOZEROVALID;
   
      if((error == 0) && fblk && fboff) {
         if(fblk > EXT2_BLOCK_SIZE(fs)) 
            panic("ext2_write : ext2_balloc allocated more than bsize(head)");
         /* We need to zero out the head */
         head_offset = uio->uio_offset - (off_t)fboff ;
         flags |= IO_HEADZEROFILL;
         flags &= ~IO_NOZEROVALID;
      }
   
      if((error == 0) && blkalloc && ((blkalloc - xfersize) > 0)) {
         /* We need to zero out the tail */
         if( blkalloc > EXT2_BLOCK_SIZE(fs)) 
            panic("ext2_write : ext2_balloc allocated more than bsize(tail)");
         local_offset += (blkalloc - xfersize);
         if (loopcount == 1) {
            /* blkalloc is same as fblk; so no need to check again*/
            local_offset -= fboff;
         }
         flags |= IO_TAILZEROFILL;
         /*  Freshly allocated block; bzero even if find a page */
         flags &= ~IO_NOZEROVALID;
      }
      /*
      * if the write starts beyond the current EOF then
      * we we'll zero fill from the current EOF to where the write begins
      */
      
      error = cluster_write(vp, uio, osize, filesize, head_offset, local_offset,
         ip->i_e2fs->s_d_blocksize, flags);
      
      if (uio->uio_offset > osize) {
         if (error && 0 == (ioflag & IO_UNIT))
            (void)VOP_TRUNCATE(vp, uio->uio_offset,
               ioflag & IO_SYNC, ap->a_cred, uio->uio_procp);
         ip->i_size = uio->uio_offset; 
         ubc_setsize(vp, (off_t)ip->i_size);
      }
      if(save_error) {
         uio->uio_resid += save_size;
         if(!error)
            error = save_error;	
      }
      ip->i_flag |= IN_CHANGE | IN_UPDATE;
   } else {
	for (error = 0; uio->uio_resid > 0;) {
		lbn = lblkno(fs, uio->uio_offset);
		blkoffset = blkoff(fs, uio->uio_offset);
		xfersize = EXT2_BLOCK_SIZE(fs) - blkoffset;
		if (uio->uio_resid < xfersize)
			xfersize = uio->uio_resid;

		if (EXT2_BLOCK_SIZE(fs) > xfersize)
			flags |= B_CLRBUF;
		else
			flags &= ~B_CLRBUF;

		error = ext2_balloc(ip,
		    lbn, blkoffset + xfersize, ap->a_cred, &bp, flags);
		if (error)
			break;

		if (uio->uio_offset + xfersize > ip->i_size) {
			ip->i_size = uio->uio_offset + xfersize;
            if (UBCISVALID(vp))
                ubc_setsize(vp, (u_long)ip->i_size); /* XXX check errors */
		}

		size = BLKSIZE(fs, ip, lbn) - bp->b_resid;
		if (size < xfersize)
			xfersize = size;

		error =
		    uiomove((char *)bp->b_data + blkoffset, (int)xfersize, uio);

		if (0 == dodir && (ioflag & IO_SYNC)) {
            (void)bwrite(bp);
		} else if (xfersize + blkoffset == EXT2_BLOCK_SIZE(fs)) {
            bp->b_flags |= B_AGE;
            bawrite(bp);
		} else {
			bdwrite(bp);
		}
		if (error || xfersize == 0)
			break;
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
	}
   } /* if (UBCISVALID(vp)) */

    dprint_clusters(vp);
    /*
	 * If we successfully wrote any data, and we are not the superuser
	 * we clear the setuid and setgid bits as a precaution against
	 * tampering.
	 */
    if (resid > uio->uio_resid && ap->a_cred && ap->a_cred->cr_uid != 0)
		ip->i_mode &= ~(ISUID | ISGID);
    if (resid > uio->uio_resid)
       VN_KNOTE(vp, NOTE_WRITE | (file_extended ? NOTE_EXTEND : 0));
	if (error) {
		if (ioflag & IO_UNIT) {
			(void)ext2_truncate(vp, osize,
			    ioflag & IO_SYNC, ap->a_cred, uio->uio_td);
			uio->uio_offset -= resid - uio->uio_resid;
			uio->uio_resid = resid;
		}
	} else if (resid > uio->uio_resid && (ioflag & IO_SYNC))
		error = ext2_update(vp, 1);
      
    ext2_trace_return(error);
}
