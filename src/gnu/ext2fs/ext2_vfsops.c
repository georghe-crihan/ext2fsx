/*
 *  modified for EXT2FS support in Lites 1.1
 *
 *  Aug 1995, Godmar Back (gback@cs.utah.edu)
 *  University of Utah, Department of Computer Science
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
 *	@(#)ffs_vfsops.c	8.8 (Berkeley) 4/18/94
 * $FreeBSD: src/sys/gnu/ext2fs/ext2_vfsops.c,v 1.101 2003/01/01 18:48:52 schweikh Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/namei.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#ifndef APPLE
#include <sys/bio.h>
#endif
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/malloc.h>
#include <sys/stat.h>
#ifndef APPLE
#include <sys/mutex.h>
#else
#include <machine/spl.h>
#include <sys/disk.h>

#define mntvnode_mtx mntvnode_slock
/* XXX Redefining mtx_* */
#define mtx_lock(l) simple_lock((l))
#define mtx_unlock(l) simple_unlock((l))

#include "ext2_apple.h"

static int vn_isdisk(struct vnode *, int *);

#include <ufs/ufs/ufsmount.h>

typedef struct ufs_args ext2_args;
#endif

#include <gnu/ext2fs/ext2_mount.h>
#include <gnu/ext2fs/inode.h>

#include <gnu/ext2fs/fs.h>
#include <gnu/ext2fs/ext2_extern.h>
#include <gnu/ext2fs/ext2_fs.h>
#include <gnu/ext2fs/ext2_fs_sb.h>
#include <ext2_byteorder.h>

#ifndef APPLE
static int ext2_fhtovp(struct mount *, struct fid *, struct vnode **);
#else
static int ext2_fhtovp(struct mount *, struct fid *, struct mbuf *, struct vnode **,
         int *, struct ucred **);
#endif
static int ext2_flushfiles(struct mount *mp, int flags, struct thread *td);
static int ext2_init(struct vfsconf *);
#ifndef APPLE
static int ext2_mount(struct mount *, struct nameidata *, struct thread *);
#else
static int ext2_mount(struct mount *, char *, caddr_t, struct nameidata *,
         struct proc *);
#endif /* APPLE */
static int ext2_mountfs(struct vnode *, struct mount *, struct thread *);
static int ext2_reload(struct mount *mountp, struct ucred *cred,
			struct thread *td);
static int ext2_root(struct mount *, struct vnode **vpp);
static int ext2_sbupdate(struct ext2mount *, int);
static int ext2_statfs(struct mount *, struct statfs *, struct thread *);
static int ext2_sync(struct mount *, int, struct ucred *, struct thread *);
static int ext2_uninit(struct vfsconf *);
static int ext2_unmount(struct mount *, int, struct thread *);
#ifndef APPLE
static int ext2_vget(struct mount *, ino_t, int, struct vnode **);
#else
static int ext2_vget(struct mount *, void *, struct vnode **);
#endif /* APPLE */
static int ext2_vptofh(struct vnode *, struct fid *);

#ifdef APPLE
static int ext2_sysctl(int *, u_int, void *, size_t *, void *, size_t, struct proc *);
static int vfs_stdstart(struct mount *, int, struct proc *);
static int vfs_stdquotactl(struct mount *, int, uid_t, caddr_t, struct proc *);

/* These assume SBSIZE == 1024 in fs.h */
#ifdef SBLOCK
#undef SBLOCK
#endif
#define SBLOCK ( 1024 / devBlockSize )
#ifdef SBSIZE
#undef SBSIZE
#endif
#define SBSIZE ( devBlockSize <= 1024 ? 1024 : devBlockSize )
#define SBOFF ( devBlockSize <= 1024 ? 0 : 1024 ) 
#endif /* APPLE */

#ifndef APPLE
MALLOC_DEFINE(M_EXT2NODE, "EXT2 node", "EXT2 vnode private part");
static MALLOC_DEFINE(M_EXT2MNT, "EXT2 mount", "EXT2 mount structure");

#define VT_EXT2 "ext2fs"
#define meta_bread bread
#define SBOFF 0
#endif /* APPLE */

static struct vfsops ext2fs_vfsops = {
	#ifndef APPLE
   NULL,
   #else
   ext2_mount,
   #endif
   vfs_stdstart,
	ext2_unmount,
	ext2_root,		/* root inode via vget */
	vfs_stdquotactl,
	ext2_statfs,
	ext2_sync,
	ext2_vget,
	ext2_fhtovp,
   #ifndef APPLE
	vfs_stdcheckexp,
   #endif
	ext2_vptofh,
	ext2_init,
   #ifndef APPLE
	ext2_uninit,
	vfs_stdextattrctl,
	ext2_mount,
   #else
   ext2_sysctl
   #endif
};

#ifndef APPLE
VFS_SET(ext2fs_vfsops, ext2fs, 0);
#define bsd_malloc malloc
#define bsd_free free
#else
#define bsd_malloc _MALLOC
#define bsd_free FREE
#endif /* APPLE */

static int ext2fs_inode_hash_lock;

static int	ext2_check_sb_compat(struct ext2_super_block *es, dev_t dev,
		    int ronly);
static int	compute_sb_data(struct vnode * devvp,
		    struct ext2_super_block * es, struct ext2_sb_info * fs);

#ifdef notyet
static int ext2_mountroot(void);

/*
 * Called by main() when ext2fs is going to be mounted as root.
 *
 * Name is updated by mount(8) after booting.
 */
#define ROOTNAME	"root_device"

static int
ext2_mountroot()
{
	struct ext2_sb_info *fs;
	struct mount *mp;
	struct thread *td = curthread;
	struct ext2mount *ump;
	u_int size;
	int error;
	
	if ((error = bdevvp(rootdev, &rootvp))) {
		printf("ext2_mountroot: can't find rootvp\n");
		return (error);
	}
	mp = bsd_malloc((u_long)sizeof(struct mount), M_MOUNT, M_WAITOK);
	bzero((char *)mp, (u_long)sizeof(struct mount));
	TAILQ_INIT(&mp->mnt_nvnodelist);
	TAILQ_INIT(&mp->mnt_reservedvnlist);
	mp->mnt_op = &ext2fs_vfsops;
	mp->mnt_flag = MNT_RDONLY;
	if (error = ext2_mountfs(rootvp, mp, td)) {
		bsd_free(mp, M_MOUNT);
		return (error);
	}
	if (error = vfs_lock(mp)) {
		(void)ext2_unmount(mp, 0, td);
		bsd_free(mp, M_MOUNT);
		return (error);
	}
	TAILQ_INSERT_HEAD(&mountlist, mp, mnt_list);
	mp->mnt_flag |= MNT_ROOTFS;
	mp->mnt_vnodecovered = NULLVP;
	ump = VFSTOEXT2(mp);
	fs = ump->um_e2fs;
	bzero(fs->fs_fsmnt, sizeof(fs->fs_fsmnt));
	fs->fs_fsmnt[0] = '/';
	bcopy((caddr_t)fs->fs_fsmnt, (caddr_t)mp->mnt_stat.f_mntonname,
	    MNAMELEN);
	(void) copystr(ROOTNAME, mp->mnt_stat.f_mntfromname, MNAMELEN - 1,
	    &size);
	bzero(mp->mnt_stat.f_mntfromname + size, MNAMELEN - size);
	(void)ext2_statfs(mp, &mp->mnt_stat, td);
	vfs_unlock(mp);
	inittodr(fs->s_es->s_wtime);		/* this helps to set the time */
	return (0);
}
#endif

/*
 * VFS Operations.
 *
 * mount system call
 */
static int
#ifndef APPLE
ext2_mount(mp, ndp, td)
#else
ext2_mount(mp, path, data, ndp, td)
#endif /* APPLE */
	struct mount *mp;
   #ifdef APPLE
   char *path;
   caddr_t data;
   #endif
	struct nameidata *ndp;
	struct thread *td;
{
   struct export_args *export;
	#ifndef APPLE
   struct vfsoptlist *opts;
   #endif
	struct vnode *devvp;
	struct ext2mount *ump = 0;
	struct ext2_sb_info *fs;
	#ifndef APPLE
   char *path;
   #else
   ext2_args args;
   #endif
   char *fspec;
	size_t size;
	int error, flags;
   #ifndef APPLE
   int len;
   #endif
	mode_t accessmode;

   #ifndef APPLE
	opts = mp->mnt_optnew;

	vfs_getopt(opts, "fspath", (void **)&path, NULL);
	/* Double-check the length of path.. */
	if (strlen(path) >= MAXMNTLEN - 1)
		return (ENAMETOOLONG);

	fspec = NULL;
	error = vfs_getopt(opts, "from", (void **)&fspec, &len);
	if (!error && fspec[len - 1] != '\0')
		return (EINVAL);
   #else
   if ((error = copyin(data, (caddr_t)&args, sizeof (ext2_args))) != 0)
		return (error);
   
   export = &args.export;
   
   (void) copyinstr(args.fspec, mp->mnt_stat.f_mntfromname, MNAMELEN - 1, 
	    &size);
	bzero(mp->mnt_stat.f_mntfromname + size, MNAMELEN - size);
   
   fspec = mp->mnt_stat.f_mntfromname;
   #endif /* APPLE */

	/*
	 * If updating, check whether changing from read-only to
	 * read/write; if there is no device name, that's all we do.
	 */
	if (mp->mnt_flag & MNT_UPDATE) {
		ump = VFSTOEXT2(mp);
		fs = ump->um_e2fs;
		error = 0;
		if (fs->s_rd_only == 0 && (mp->mnt_flag & MNT_RDONLY)) {
			flags = WRITECLOSE;
			if (mp->mnt_flag & MNT_FORCE)
				flags |= FORCECLOSE;
			if (vfs_busy(mp, LK_NOWAIT, 0, td))
				return (EBUSY);
			error = ext2_flushfiles(mp, flags, td);
			vfs_unbusy(mp, td);
			if (!error && fs->s_wasvalid) {
				fs->s_es->s_state =
               cpu_to_le16(le16_to_cpu(fs->s_es->s_state) | EXT2_VALID_FS);
				ext2_sbupdate(ump, MNT_WAIT);
			}
			fs->s_rd_only = 1;
		}
		if (!error && (mp->mnt_flag & MNT_RELOAD))
			error = ext2_reload(mp, ndp->ni_cnd.cn_cred, td);
		if (error)
			return (error);
		devvp = ump->um_devvp;
		if (ext2_check_sb_compat(fs->s_es, (dev_t)devvp->v_rdev,
		    (mp->mnt_kern_flag & MNTK_WANTRDWR) == 0) != 0)
			return (EPERM);
		if (fs->s_rd_only && (mp->mnt_kern_flag & MNTK_WANTRDWR)) {
			/*
			 * If upgrade to read-write by non-root, then verify
			 * that user has necessary permissions on the device.
			 */
         #ifndef APPLE
			if (suser(td)) {
         #else
         if (suser(td->p_ucred, &td->p_acflag)) {
         #endif /* APPLE */
				vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY, td);
				if ((error = VOP_ACCESS(devvp, VREAD | VWRITE,
				    td->td_ucred, td)) != 0) {
					VOP_UNLOCK(devvp, 0, td);
					return (error);
				}
				VOP_UNLOCK(devvp, 0, td);
			}

			if ((le16_to_cpu(fs->s_es->s_state) & EXT2_VALID_FS) == 0 ||
			    (le16_to_cpu(fs->s_es->s_state) & EXT2_ERROR_FS)) {
				if (mp->mnt_flag & MNT_FORCE) {
					printf(
"WARNING: %s was not properly dismounted\n",
					    fs->fs_fsmnt);
				} else {
					printf(
"WARNING: R/W mount of %s denied.  Filesystem is not clean - run fsck\n",
					    fs->fs_fsmnt);
					return (EPERM);
				}
			}
			fs->s_es->s_state =
            cpu_to_le16(le16_to_cpu(fs->s_es->s_state) & ~EXT2_VALID_FS);
			ext2_sbupdate(ump, MNT_WAIT);
			fs->s_rd_only = 0;
		}
		if (fspec == NULL) {
         #ifndef APPLE
			error = vfs_getopt(opts, "export", (void **)&export,
			    &len);
			if (error || len != sizeof(struct export_args))
				return (EINVAL);
				/* Process export requests. */
			return (vfs_export(mp, export));
         #else
         #if 0
         struct export_args apple_exp;
         struct netexport apple_netexp;
         
         return (vfs_export(mp, &apple_netexp, export));
         #endif
         return (EINVAL);
         #endif
		}
	}
	/*
	 * Not an update, or updating the name: look up the name
	 * and verify that it refers to a sensible block device.
	 */
	if (fspec == NULL)
		return (EINVAL);
	NDINIT(ndp, LOOKUP, FOLLOW, UIO_SYSSPACE, fspec, td);
	if ((error = namei(ndp)) != 0)
		return (error);
	#ifndef APPLE
   NDFREE(ndp, NDF_ONLY_PNBUF);
   #else
   if (ndp->ni_cnd.cn_flags & HASBUF) {
      _FREE_ZONE(ndp->ni_cnd.cn_pnbuf, ndp->ni_cnd.cn_pnlen, M_NAMEI);		
		ndp->ni_cnd.cn_flags &= ~HASBUF;
   }
   #endif /* APPLE */
	devvp = ndp->ni_vp;

	if (!vn_isdisk(devvp, &error)) {
		vrele(devvp);
		return (error);
	}

	/*
	 * If mount by non-root, then verify that user has necessary
	 * permissions on the device.
	 */
   #ifndef APPLE
   if (suser(td)) {
   #else
   if (suser(td->p_ucred, &td->p_acflag)) {
   #endif /* APPLE */
		accessmode = VREAD;
		if ((mp->mnt_flag & MNT_RDONLY) == 0)
			accessmode |= VWRITE;
		vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY, td);
		if ((error = VOP_ACCESS(devvp, accessmode, td->td_ucred, td)) != 0) {
			vput(devvp);
			return (error);
		}
		VOP_UNLOCK(devvp, 0, td);
	}

	if ((mp->mnt_flag & MNT_UPDATE) == 0) {
		error = ext2_mountfs(devvp, mp, td);
	} else {
		if (devvp != ump->um_devvp)
			error = EINVAL;	/* needs translation */
		else
			vrele(devvp);
	}
	if (error) {
		vrele(devvp);
		return (error);
	}
	ump = VFSTOEXT2(mp);
	fs = ump->um_e2fs;
	/*
	 * Note that this strncpy() is ok because of a check at the start
	 * of ext2_mount().
	 */
   #ifndef APPLE
	strncpy(fs->fs_fsmnt, path, MAXMNTLEN);
   fs->fs_fsmnt[MAXMNTLEN - 1] = '\0';
   #else
   (void) copyinstr(path, fs->fs_fsmnt, sizeof(fs->fs_fsmnt) - 1, &size);
   bzero(fs->fs_fsmnt + size, sizeof(fs->fs_fsmnt) - size);
   bcopy((caddr_t)fs->fs_fsmnt, (caddr_t)mp->mnt_stat.f_mntonname,
	    MNAMELEN);
   #endif
   #ifndef APPLE
	(void)copystr(fspec, mp->mnt_stat.f_mntfromname, MNAMELEN - 1, &size);
	bzero(mp->mnt_stat.f_mntfromname + size, MNAMELEN - size);
   #endif
	(void)ext2_statfs(mp, &mp->mnt_stat, td);
	return (0);
}

/*
 * checks that the data in the descriptor blocks make sense
 * this is taken from ext2/super.c
 */
static int ext2_check_descriptors (struct ext2_sb_info * sb)
{
        int i;
        int desc_block = 0;
        unsigned long block = le32_to_cpu(sb->s_es->s_first_data_block);
        struct ext2_group_desc * gdp = NULL;

        /* ext2_debug ("Checking group descriptors"); */

        for (i = 0; i < sb->s_groups_count; i++)
        {
		/* examine next descriptor block */
                if ((i % EXT2_DESC_PER_BLOCK(sb)) == 0)
                        gdp = (struct ext2_group_desc *) 
				sb->s_group_desc[desc_block++]->b_data;
                if (le32_to_cpu(gdp->bg_block_bitmap) < block ||
                    le32_to_cpu(gdp->bg_block_bitmap) >= block + EXT2_BLOCKS_PER_GROUP(sb))
                {
                        printf ("ext2_check_descriptors: "
                                    "Block bitmap for group %d"
                                    " not in group (block %lu)!\n",
                                    i, (unsigned long) le32_to_cpu(gdp->bg_block_bitmap));
                        return 0;
                }
                if (le32_to_cpu(gdp->bg_inode_bitmap) < block ||
                    le32_to_cpu(gdp->bg_inode_bitmap) >= block + EXT2_BLOCKS_PER_GROUP(sb))
                {
                        printf ("ext2_check_descriptors: "
                                    "Inode bitmap for group %d"
                                    " not in group (block %lu)!\n",
                                    i, (unsigned long) le32_to_cpu(gdp->bg_inode_bitmap));
                        return 0;
                }
                if (le32_to_cpu(gdp->bg_inode_table) < block ||
                    le32_to_cpu(gdp->bg_inode_table) + sb->s_itb_per_group >=
                    block + EXT2_BLOCKS_PER_GROUP(sb))
                {
                        printf ("ext2_check_descriptors: "
                                    "Inode table for group %d"
                                    " not in group (block %lu)!\n",
                                    i, (unsigned long) le32_to_cpu(gdp->bg_inode_table));
                        return 0;
                }
                block += EXT2_BLOCKS_PER_GROUP(sb);
                gdp++;
        }
        return 1;
}

static int
ext2_check_sb_compat(es, dev, ronly)
	struct ext2_super_block *es;
	dev_t dev;
	int ronly;
{

	if (le16_to_cpu(es->s_magic) != EXT2_SUPER_MAGIC) {
		printf("ext2fs: %s: wrong magic number %#x (expected %#x)\n",
		    devtoname(dev), le16_to_cpu(es->s_magic), EXT2_SUPER_MAGIC);
		return (1);
	}
	if (le32_to_cpu(es->s_rev_level) > EXT2_GOOD_OLD_REV) {
		if (le32_to_cpu(es->s_feature_incompat) & ~EXT2_FEATURE_INCOMPAT_SUPP) {
			printf(
"WARNING: mount of %s denied due to unsupported optional features\n",
			    devtoname(dev));
			return (1);
		}
		if (!ronly &&
		    (le32_to_cpu(es->s_feature_ro_compat) & ~EXT2_FEATURE_RO_COMPAT_SUPP)) {
			printf(
"WARNING: R/W mount of %s denied due to unsupported optional features\n",
			    devtoname(dev));
			return (1);
		}
	}
	return (0);
}

/*
 * this computes the fields of the  ext2_sb_info structure from the
 * data in the ext2_super_block structure read in
 * ext2_sb_info is kept in cpu byte order except for group info
 * which is kept in LE (on disk) order.
 */
static int compute_sb_data(devvp, es, fs)
	struct vnode * devvp;
	struct ext2_super_block * es;
	struct ext2_sb_info * fs;
{
    int db_count, error;
    int i, j;
    int logic_sb_block = 1;	/* XXX for now */
    #if 0 /*#ifdef APPLE*/
    int devBlockSize=0;
    
    VOP_DEVBLOCKSIZE(devvp, &devBlockSize);
    #endif

#if 1
#define V(v)  
#else
#define V(v)  printf(#v"= %d\n", fs->v);
#endif

    fs->s_blocksize = EXT2_MIN_BLOCK_SIZE << le32_to_cpu(es->s_log_block_size); 
    V(s_blocksize)
    fs->s_bshift = EXT2_MIN_BLOCK_LOG_SIZE + le32_to_cpu(es->s_log_block_size);
    V(s_bshift)
    fs->s_fsbtodb = le32_to_cpu(es->s_log_block_size) + 1;
    V(s_fsbtodb)
    fs->s_qbmask = fs->s_blocksize - 1;
    V(s_bmask)
    fs->s_blocksize_bits = le32_to_cpu(EXT2_BLOCK_SIZE_BITS(es));
    V(s_blocksize_bits)
    fs->s_frag_size = EXT2_MIN_FRAG_SIZE << le32_to_cpu(es->s_log_frag_size);
    V(s_frag_size)
    if (fs->s_frag_size)
	fs->s_frags_per_block = fs->s_blocksize / fs->s_frag_size;
    V(s_frags_per_block)
    fs->s_blocks_per_group = le32_to_cpu(es->s_blocks_per_group);
    V(s_blocks_per_group)
    fs->s_frags_per_group = le32_to_cpu(es->s_frags_per_group);
    V(s_frags_per_group)
    fs->s_inodes_per_group = le32_to_cpu(es->s_inodes_per_group);
    V(s_inodes_per_group)
    fs->s_inodes_per_block = fs->s_blocksize / EXT2_INODE_SIZE;
    V(s_inodes_per_block)
    fs->s_itb_per_group = fs->s_inodes_per_group /fs->s_inodes_per_block;
    V(s_itb_per_group)
    fs->s_desc_per_block = fs->s_blocksize / sizeof (struct ext2_group_desc);
    V(s_desc_per_block)
    /* s_resuid / s_resgid ? */
    fs->s_groups_count = (le32_to_cpu(es->s_blocks_count) -
			  le32_to_cpu(es->s_first_data_block) +
			  EXT2_BLOCKS_PER_GROUP(fs) - 1) /
			 EXT2_BLOCKS_PER_GROUP(fs);
    V(s_groups_count)
    db_count = (fs->s_groups_count + EXT2_DESC_PER_BLOCK(fs) - 1) /
	EXT2_DESC_PER_BLOCK(fs);
    fs->s_db_per_group = db_count;
    V(s_db_per_group)

    fs->s_group_desc = bsd_malloc(db_count * sizeof (struct buf *),
		M_EXT2MNT, M_WAITOK);

    /* adjust logic_sb_block */
    if(fs->s_blocksize > 1024 /*SBSIZE*/) 
	/* Godmar thinks: if the blocksize is greater than 1024, then
	   the superblock is logically part of block zero. 
	 */
        logic_sb_block = 0;
    
    for (i = 0; i < db_count; i++) {
	error = meta_bread(devvp , fsbtodb(fs, logic_sb_block + i + 1), 
		fs->s_blocksize, NOCRED, &fs->s_group_desc[i]);
	if(error) {
	    for (j = 0; j < i; j++)
		brelse(fs->s_group_desc[j]);
	    bsd_free(fs->s_group_desc, M_EXT2MNT);
	    printf("EXT2-fs: unable to read group descriptors (%d)\n", error);
	    return EIO;
	}
	/* Set the B_LOCKED flag on the buffer, then brelse() it */
	LCK_BUF(fs->s_group_desc[i])
    }
    if(!ext2_check_descriptors(fs)) {
	    for (j = 0; j < db_count; j++)
		    ULCK_BUF(fs->s_group_desc[j])
	    bsd_free(fs->s_group_desc, M_EXT2MNT);
	    printf("EXT2-fs: (ext2_check_descriptors failure) "
		   "unable to read group descriptors\n");
	    return EIO;
    }

    for (i = 0; i < EXT2_MAX_GROUP_LOADED; i++) {
	    fs->s_inode_bitmap_number[i] = 0;
	    fs->s_inode_bitmap[i] = NULL;
	    fs->s_block_bitmap_number[i] = 0;
	    fs->s_block_bitmap[i] = NULL;
    }
    fs->s_loaded_inode_bitmaps = 0;
    fs->s_loaded_block_bitmaps = 0;
    return 0;
}

/*
 * Reload all incore data for a filesystem (used after running fsck on
 * the root filesystem and finding things to fix). The filesystem must
 * be mounted read-only.
 *
 * Things to do to update the mount:
 *	1) invalidate all cached meta-data.
 *	2) re-read superblock from disk.
 *	3) re-read summary information from disk.
 *	4) invalidate all inactive vnodes.
 *	5) invalidate all cached file data.
 *	6) re-read inode data for all active vnodes.
 */
static int
ext2_reload(mountp, cred, td)
	struct mount *mountp;
	struct ucred *cred;
	struct thread *td;
{
	struct vnode *vp, *nvp, *devvp;
	struct inode *ip;
	struct buf *bp;
	struct ext2_super_block * es;
	struct ext2_sb_info *fs;
	int error;
   #ifdef APPLE
   int devBlockSize=0;
   #endif

	if ((mountp->mnt_flag & MNT_RDONLY) == 0)
		return (EINVAL);
	/*
	 * Step 1: invalidate all cached meta-data.
	 */
	devvp = VFSTOEXT2(mountp)->um_devvp;
	if (vinvalbuf(devvp, 0, cred, td, 0, 0))
		panic("ext2_reload: dirty1");
	/*
	 * Step 2: re-read superblock from disk.
	 * constants have been adjusted for ext2
	 */
   #ifdef APPLE
   /* Get the current block size */
   VOP_DEVBLOCKSIZE(devvp, &devBlockSize);
   #endif
	if ((error = meta_bread(devvp, SBLOCK, SBSIZE, NOCRED, &bp)) != 0)
		return (error);
	es = (struct ext2_super_block *)(bp->b_data+SBOFF);
	if (ext2_check_sb_compat(es, (dev_t)devvp->v_rdev, 0) != 0) {
		brelse(bp);
		return (EIO);		/* XXX needs translation */
	}
	fs = VFSTOEXT2(mountp)->um_e2fs;
	bcopy(es, fs->s_es, sizeof(struct ext2_super_block));

	if((error = compute_sb_data(devvp, es, fs)) != 0) {
		brelse(bp);
		return error;
	}
#ifdef UNKLAR
	if (fs->fs_sbsize < SBSIZE)
		bp->b_flags |= B_INVAL;
#endif
	brelse(bp);

loop:
	mtx_lock(&mntvnode_mtx);
   #ifndef APPLE
	for (vp = TAILQ_FIRST(&mountp->mnt_nvnodelist); vp != NULL; vp = nvp) {
   #else
   for (vp = LIST_FIRST(&mountp->mnt_vnodelist); vp != NULL; vp = nvp) {
   #endif
		if (vp->v_mount != mountp) {
			mtx_unlock(&mntvnode_mtx);
			goto loop;
		}
      #ifndef APPLE
		nvp = TAILQ_NEXT(vp, v_nmntvnodes);
      #else
      nvp = LIST_NEXT(vp, v_mntvnodes);
      #endif
		mtx_unlock(&mntvnode_mtx);
		/*
		 * Step 4: invalidate all inactive vnodes.
		 */
  		if (vrecycle(vp, NULL, td))
  			goto loop;
		/*
		 * Step 5: invalidate all cached file data.
		 */
		#ifndef APPLE
      mtx_lock (&vp->v_interlock);
      #else
      simple_lock (&vp->v_interlock);
      #endif
		if (vget(vp, LK_EXCLUSIVE | LK_INTERLOCK, td)) {
			goto loop;
		}
		if (vinvalbuf(vp, 0, cred, td, 0, 0))
			panic("ext2_reload: dirty2");
		/*
		 * Step 6: re-read inode data for all active vnodes.
		 */
		ip = VTOI(vp);
		error =
		    bread(devvp, fsbtodb(fs, ino_to_fsba(fs, ip->i_number)),
		    (int)fs->s_blocksize, NOCRED, &bp);
		if (error) {
			vput(vp);
			return (error);
		}
		ext2_ei2i((struct ext2_inode *) ((char *)bp->b_data +
		    EXT2_INODE_SIZE * ino_to_fsbo(fs, ip->i_number)), ip);
		brelse(bp);
		vput(vp);
		mtx_lock(&mntvnode_mtx);
	}
	mtx_unlock(&mntvnode_mtx);
	return (0);
}

/*
 * Common code for mount and mountroot
 */
static int
ext2_mountfs(devvp, mp, td)
	struct vnode *devvp;
	struct mount *mp;
	struct thread *td;
{
	struct ext2mount *ump;
	struct buf *bp;
	struct ext2_sb_info *fs;
	struct ext2_super_block * es;
	dev_t dev = (dev_t)devvp->v_rdev;
	int error;
	int ronly;
   #ifdef APPLE
   int devBlockSize=0;
   #endif

	/*
	 * Disallow multiple mounts of the same device.
	 * Disallow mounting of a device that is currently in use
	 * (except for root, which might share swap device for miniroot).
	 * Flush out any old buffers remaining from a previous use.
	 */
	if ((error = vfs_mountedon(devvp)) != 0)
		return (error);
	if (vcount(devvp) > 1 && devvp != rootvp)
		return (EBUSY);
	if ((error = vinvalbuf(devvp, V_SAVE, td->td_ucred, td, 0, 0)) != 0)
		return (error);
#ifdef READONLY
/* turn on this to force it to be read-only */
	mp->mnt_flag |= MNT_RDONLY;
#endif

	ronly = (mp->mnt_flag & MNT_RDONLY) != 0;
	vn_lock(devvp, LK_EXCLUSIVE | LK_RETRY, td);
	error = VOP_OPEN(devvp, ronly ? FREAD : FREAD|FWRITE, FSCRED, td);
	VOP_UNLOCK(devvp, 0, td);
	if (error)
		return (error);
   #ifndef APPLE
	if (devvp->v_rdev->si_iosize_max != 0)
		mp->mnt_iosize_max = devvp->v_rdev->si_iosize_max;
	if (mp->mnt_iosize_max > MAXPHYS)
		mp->mnt_iosize_max = MAXPHYS;
   #endif
   
   #ifdef APPLE
   /* Set the block size to 512. Things just seem to royally screw 
      up otherwise.
    */
   devBlockSize = 512;
   if (VOP_IOCTL(devvp, DKIOCSETBLOCKSIZE, (caddr_t)&devBlockSize,
         FWRITE, td->td_ucred, td)) {
      return (ENXIO);
   }
   /* force specfs to re-read the new size */
   set_fsblocksize(devvp);
   
   /* cache the IO attributes */
	if ((error = vfs_init_io_attributes(devvp, mp))) {
		printf("ext2_mountfs: vfs_init_io_attributes returned %d\n",
			error);
		return (error);
	}
   
   /* VOP_DEVBLOCKSIZE(devvp, &devBlockSize); */
   #endif
   
	bp = NULL;
	ump = NULL;
	if ((error = meta_bread(devvp, SBLOCK, SBSIZE, NOCRED, &bp)) != 0)
		goto out;
   #if defined(DIAGNOSTIC) && defined(APPLE)
   printf("ext2fs: first two sb words (%lX, %lX) -- sb=%u, sb size=%u\n",
      *(u_long*)(bp->b_data+SBOFF), *(u_long*)(bp->b_data+(SBOFF+4)),
      SBLOCK, SBSIZE);
   #endif
	es = (struct ext2_super_block *)(bp->b_data+SBOFF);
	if (ext2_check_sb_compat(es, dev, ronly) != 0) {
		error = EINVAL;		/* XXX needs translation */
		goto out;
	}
	if ((le16_to_cpu(es->s_state) & EXT2_VALID_FS) == 0 ||
	    (le16_to_cpu(es->s_state) & EXT2_ERROR_FS)) {
		if (ronly || (mp->mnt_flag & MNT_FORCE)) {
			printf(
"WARNING: Filesystem was not properly dismounted\n");
		} else {
			printf(
"WARNING: R/W mount denied.  Filesystem is not clean - run fsck\n");
			error = EPERM;
			goto out;
		}
	}
	ump = bsd_malloc(sizeof *ump, M_EXT2MNT, M_WAITOK);
	bzero((caddr_t)ump, sizeof *ump);
	/* I don't know whether this is the right strategy. Note that
	   we dynamically allocate both an ext2_sb_info and an ext2_super_block
	   while Linux keeps the super block in a locked buffer
	 */
	ump->um_e2fs = bsd_malloc(sizeof(struct ext2_sb_info), 
		M_EXT2MNT, M_WAITOK);
	ump->um_e2fs->s_es = bsd_malloc(sizeof(struct ext2_super_block), 
		M_EXT2MNT, M_WAITOK);
	bcopy(es, ump->um_e2fs->s_es, (u_int)sizeof(struct ext2_super_block));
	if ((error = compute_sb_data(devvp, ump->um_e2fs->s_es, ump->um_e2fs)))
		goto out;
	/*
	 * We don't free the group descriptors allocated by compute_sb_data()
	 * until ext2_unmount().  This is OK since the mount will succeed.
	 */
	brelse(bp);
	bp = NULL;
	fs = ump->um_e2fs;
	fs->s_rd_only = ronly;	/* ronly is set according to mnt_flags */
	/* if the fs is not mounted read-only, make sure the super block is 
	   always written back on a sync()
	 */
	fs->s_wasvalid = le16_to_cpu(fs->s_es->s_state) & EXT2_VALID_FS ? 1 : 0;
	if (ronly == 0) {
		fs->s_dirt = 1;		/* mark it modified */
		fs->s_es->s_state =
         cpu_to_le16(le16_to_cpu(fs->s_es->s_state) & ~EXT2_VALID_FS);	/* set fs invalid */
	}
	mp->mnt_data = (qaddr_t)ump;
   #ifndef APPLE
	mp->mnt_stat.f_fsid.val[0] = dev2udev(dev);
	mp->mnt_stat.f_fsid.val[1] = mp->mnt_vfc->vfc_typenum;
   #else
   vfs_getnewfsid(mp);
   #endif
	mp->mnt_maxsymlinklen = EXT2_MAXSYMLINKLEN;
	mp->mnt_flag |= MNT_LOCAL;
	ump->um_mountp = mp;
	ump->um_dev = dev;
	ump->um_devvp = devvp;
	/* setting those two parameters allowed us to use
	   ufs_bmap w/o changse !
	*/
	ump->um_nindir = EXT2_ADDR_PER_BLOCK(fs);
	ump->um_bptrtodb = le32_to_cpu(fs->s_es->s_log_block_size) + 1;
	ump->um_seqinc = EXT2_FRAGS_PER_BLOCK(fs);
   #ifndef APPLE
	devvp->v_rdev->si_mountpoint = mp;
   #endif
	if (ronly == 0) 
		ext2_sbupdate(ump, MNT_WAIT);
	return (0);
out:
	if (bp)
		brelse(bp);
	(void)VOP_CLOSE(devvp, ronly ? FREAD : FREAD|FWRITE, NOCRED, td);
	if (ump) {
		bsd_free(ump->um_e2fs->s_es, M_EXT2MNT);
		bsd_free(ump->um_e2fs, M_EXT2MNT);
		bsd_free(ump, M_EXT2MNT);
		mp->mnt_data = (qaddr_t)0;
	}
	return (error);
}

/*
 * unmount system call
 */
static int
ext2_unmount(mp, mntflags, td)
	struct mount *mp;
	int mntflags;
	struct thread *td;
{
	struct ext2mount *ump;
	struct ext2_sb_info *fs;
	int error, flags, ronly, i;

	flags = 0;
	if (mntflags & MNT_FORCE) {
		if (mp->mnt_flag & MNT_ROOTFS)
			return (EINVAL);
		flags |= FORCECLOSE;
	}
	if ((error = ext2_flushfiles(mp, flags, td)) != 0)
		return (error);
	ump = VFSTOEXT2(mp);
	fs = ump->um_e2fs;
	ronly = fs->s_rd_only;
	if (ronly == 0) {
		if (fs->s_wasvalid)
			fs->s_es->s_state =
            cpu_to_le16(le16_to_cpu(fs->s_es->s_state) | EXT2_VALID_FS);
		ext2_sbupdate(ump, MNT_WAIT);
	}

	/* release buffers containing group descriptors */
	for(i = 0; i < fs->s_db_per_group; i++) 
		ULCK_BUF(fs->s_group_desc[i])
	bsd_free(fs->s_group_desc, M_EXT2MNT);

	/* release cached inode/block bitmaps */
        for (i = 0; i < EXT2_MAX_GROUP_LOADED; i++)
                if (fs->s_inode_bitmap[i])
			ULCK_BUF(fs->s_inode_bitmap[i])

        for (i = 0; i < EXT2_MAX_GROUP_LOADED; i++)
                if (fs->s_block_bitmap[i])
			ULCK_BUF(fs->s_block_bitmap[i])
   
   #ifndef APPLE
	ump->um_devvp->v_rdev->si_mountpoint = NULL;
   #endif
	error = VOP_CLOSE(ump->um_devvp, ronly ? FREAD : FREAD|FWRITE,
		NOCRED, td);
	vrele(ump->um_devvp);
	bsd_free(fs->s_es, M_EXT2MNT);
	bsd_free(fs, M_EXT2MNT);
	bsd_free(ump, M_EXT2MNT);
	mp->mnt_data = (qaddr_t)0;
	mp->mnt_flag &= ~MNT_LOCAL;
	return (error);
}

/*
 * Flush out all the files in a filesystem.
 */
static int
ext2_flushfiles(mp, flags, td)
	struct mount *mp;
	int flags;
	struct thread *td;
{
	int error;

	error = vflush(mp, 0, flags);
	return (error);
}

/*
 * Get file system statistics.
 * taken from ext2/super.c ext2_statfs
 */
static int
ext2_statfs(mp, sbp, td)
	struct mount *mp;
	struct statfs *sbp;
	struct thread *td;
{
        unsigned long overhead;
	struct ext2mount *ump;
	struct ext2_sb_info *fs;
	struct ext2_super_block *es;
	int i, nsb;

	ump = VFSTOEXT2(mp);
	fs = ump->um_e2fs;
	es = fs->s_es;
	if (le16_to_cpu(es->s_magic) != EXT2_SUPER_MAGIC)
		panic("ext2_statfs - magic number spoiled");

	/*
	 * Compute the overhead (FS structures)
	 */
	if (le32_to_cpu(es->s_feature_ro_compat) & EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER) {
		nsb = 0;
		for (i = 0 ; i < fs->s_groups_count; i++)
			if (ext2_group_sparse(i))
				nsb++;
	} else
		nsb = fs->s_groups_count;
	overhead = le32_to_cpu(es->s_first_data_block) + 
	    /* Superblocks and block group descriptors: */
	    nsb * (1 + fs->s_db_per_group) +
	    /* Inode bitmap, block bitmap, and inode table: */
	    fs->s_groups_count * (1 + 1 + fs->s_itb_per_group);

	sbp->f_bsize = EXT2_FRAG_SIZE(fs);	
	sbp->f_iosize = EXT2_BLOCK_SIZE(fs);
	sbp->f_blocks = le32_to_cpu(es->s_blocks_count) - overhead;
	sbp->f_bfree = le32_to_cpu(es->s_free_blocks_count); 
	sbp->f_bavail = sbp->f_bfree - le32_to_cpu(es->s_r_blocks_count); 
	sbp->f_files = le32_to_cpu(es->s_inodes_count); 
	sbp->f_ffree = le32_to_cpu(es->s_free_inodes_count); 
	if (sbp != &mp->mnt_stat) {
		sbp->f_type = mp->mnt_vfc->vfc_typenum;
		bcopy((caddr_t)mp->mnt_stat.f_mntonname,
			(caddr_t)&sbp->f_mntonname[0], MNAMELEN);
		bcopy((caddr_t)mp->mnt_stat.f_mntfromname,
			(caddr_t)&sbp->f_mntfromname[0], MNAMELEN);
	}
	return (0);
}

/*
 * Go through the disk queues to initiate sandbagged IO;
 * go through the inodes to write those that have been modified;
 * initiate the writing of the super block if it has been modified.
 *
 * Note: we are always called with the filesystem marked `MPBUSY'.
 */
static int
ext2_sync(mp, waitfor, cred, td)
	struct mount *mp;
	int waitfor;
	struct ucred *cred;
	struct thread *td;
{
	struct vnode *nvp, *vp;
	struct inode *ip;
	struct ext2mount *ump = VFSTOEXT2(mp);
	struct ext2_sb_info *fs;
	int error, allerror = 0;
   #ifdef APPLE
   int didhold;
   #endif

	fs = ump->um_e2fs;
	if (fs->s_dirt != 0 && fs->s_rd_only != 0) {		/* XXX */
		printf("fs = %s\n", fs->fs_fsmnt);
		panic("ext2_sync: rofs mod");
	}
	/*
	 * Write back each (modified) inode.
	 */
	mtx_lock(&mntvnode_mtx);
loop:
   #ifndef APPLE
	for (vp = TAILQ_FIRST(&mp->mnt_nvnodelist); vp != NULL; vp = nvp) {
   #else
   for (vp = LIST_FIRST(&mp->mnt_vnodelist); vp != NULL; vp = nvp) {
   #endif
		/*
		 * If the vnode that we are about to sync is no longer
		 * associated with this mount point, start over.
		 */
		if (vp->v_mount != mp)
			goto loop;
      #ifndef APPLE
		nvp = TAILQ_NEXT(vp, v_nmntvnodes);
      #else
      nvp = LIST_NEXT(vp, v_mntvnodes);
      #endif
		mtx_unlock(&mntvnode_mtx);
		VI_LOCK(vp);
		ip = VTOI(vp);
		if (vp->v_type == VNON ||
		    ((ip->i_flag &
		    (IN_ACCESS | IN_CHANGE | IN_MODIFIED | IN_UPDATE)) == 0 &&
          #ifndef APPLE
		    (TAILQ_EMPTY(&vp->v_dirtyblkhd) || waitfor == MNT_LAZY))) {
          #else
          LIST_EMPTY(&vp->v_dirtyblkhd))) {
          #endif
			VI_UNLOCK(vp);
			mtx_lock(&mntvnode_mtx);
			continue;
		}
		error = vget(vp, LK_EXCLUSIVE | LK_NOWAIT | LK_INTERLOCK, td);
		if (error) {
			mtx_lock(&mntvnode_mtx);
			if (error == ENOENT)
				goto loop;
			continue;
		}
      #ifdef APPLE
      didhold = ubc_hold(vp);
      #endif
		if ((error = VOP_FSYNC(vp, cred, waitfor, td)) != 0)
			allerror = error;
		VOP_UNLOCK(vp, 0, td);
      #ifdef APPLE
      if (didhold)
         ubc_rele(vp);
      #endif
		vrele(vp);
		mtx_lock(&mntvnode_mtx);
	}
	mtx_unlock(&mntvnode_mtx);
	/*
	 * Force stale file system control information to be flushed.
	 */
   #ifndef APPLE
	if (waitfor != MNT_LAZY)
   #endif
   {
		vn_lock(ump->um_devvp, LK_EXCLUSIVE | LK_RETRY, td);
		if ((error = VOP_FSYNC(ump->um_devvp, cred, waitfor, td)) != 0)
			allerror = error;
		VOP_UNLOCK(ump->um_devvp, 0, td);
	}
	/*
	 * Write back modified superblock.
	 */
	if (fs->s_dirt != 0) {
		fs->s_dirt = 0;
		fs->s_es->s_wtime = cpu_to_le32(time_second);
		if ((error = ext2_sbupdate(ump, waitfor)) != 0)
			allerror = error;
	}
	return (allerror);
}

/*
 * Look up an EXT2FS dinode number to find its incore vnode, otherwise read it
 * in from disk.  If it is in core, wait for the lock bit to clear, then
 * return the inode locked.  Detection and handling of mount points must be
 * done by the calling routine.
 */
static int
#ifndef APPLE
ext2_vget(mp, ino, flags, vpp)
#else
ext2_vget(mp, inop, vpp)
#endif /* APPLE */
	struct mount *mp;
   #ifndef APPLE
	ino_t ino;
	int flags;
   #else
   void *inop;
   #endif
	struct vnode **vpp;
{
	struct ext2_sb_info *fs;
	struct inode *ip;
	struct ext2mount *ump;
	struct buf *bp;
	struct vnode *vp;
	dev_t dev;
	int i, error;
	int used_blocks;
   #ifdef APPLE
   int flags = LK_EXCLUSIVE;
   ino_t ino = (ino_t)inop;
   #endif

	ump = VFSTOEXT2(mp);
	dev = ump->um_dev;
restart:
	if ((error = ext2_ihashget(dev, ino, flags, vpp)) != 0)
		return (error);
	if (*vpp != NULL)
		return (0);

	/*
	 * Lock out the creation of new entries in the FFS hash table in
	 * case getnewvnode() or MALLOC() blocks, otherwise a duplicate
	 * may occur!
	 */
	if (ext2fs_inode_hash_lock) {
		while (ext2fs_inode_hash_lock) {
			ext2fs_inode_hash_lock = -1;
			tsleep(&ext2fs_inode_hash_lock, PVM, "e2vget", 0);
		}
		goto restart;
	}
	ext2fs_inode_hash_lock = 1;

	/*
	 * If this MALLOC() is performed after the getnewvnode()
	 * it might block, leaving a vnode with a NULL v_data to be
	 * found by ext2_sync() if a sync happens to fire right then,
	 * which will cause a panic because ext2_sync() blindly
	 * dereferences vp->v_data (as well it should).
	 */
	MALLOC(ip, struct inode *, sizeof(struct inode), M_EXT2NODE, M_WAITOK);

	/* Allocate a new vnode/inode. */
   if ((error = getnewvnode(VT_EXT2, mp, ext2_vnodeop_p, &vp)) != 0) {
		if (ext2fs_inode_hash_lock < 0)
			wakeup(&ext2fs_inode_hash_lock);
		ext2fs_inode_hash_lock = 0;
		*vpp = NULL;
		FREE(ip, M_EXT2NODE);
		return (error);
	}
	bzero((caddr_t)ip, sizeof(struct inode));
	vp->v_data = ip;
	ip->i_vnode = vp;
	ip->i_e2fs = fs = ump->um_e2fs;
	ip->i_dev = dev;
	ip->i_number = ino;
   #ifdef APPLE
   /* Init our private lock */
   lockinit(&ip->i_lock, PINOD, "ext2 inode", 0, 0);
   #endif
	/*
	 * Put it onto its hash chain and lock it so that other requests for
	 * this inode will block if they arrive while we are sleeping waiting
	 * for old data structures to be purged or for the contents of the
	 * disk portion of this inode to be read.
	 */
	ext2_ihashins(ip);

	if (ext2fs_inode_hash_lock < 0)
		wakeup(&ext2fs_inode_hash_lock);
	ext2fs_inode_hash_lock = 0;

	/* Read in the disk contents for the inode, copy into the inode. */
#if 0
printf("ext2_vget(%d) dbn= %d ", ino, fsbtodb(fs, ino_to_fsba(fs, ino)));
#endif
	if ((error = bread(ump->um_devvp, fsbtodb(fs, ino_to_fsba(fs, ino)),
	    (int)fs->s_blocksize, NOCRED, &bp)) != 0) {
		/*
		 * The inode does not contain anything useful, so it would
		 * be misleading to leave it on its hash chain. With mode
		 * still zero, it will be unlinked and returned to the free
		 * list by vput().
		 */
		vput(vp);
		brelse(bp);
		*vpp = NULL;
		return (error);
	}
	/* convert ext2 inode to dinode */
	ext2_ei2i((struct ext2_inode *) ((char *)bp->b_data + EXT2_INODE_SIZE *
			ino_to_fsbo(fs, ino)), ip);
	ip->i_block_group = ino_to_cg(fs, ino);
	ip->i_next_alloc_block = 0;
	ip->i_next_alloc_goal = 0;
	ip->i_prealloc_count = 0;
	ip->i_prealloc_block = 0;
        /* now we want to make sure that block pointers for unused
           blocks are zeroed out - ext2_balloc depends on this 
	   although for regular files and directories only
	*/
	if(S_ISDIR(ip->i_mode) || S_ISREG(ip->i_mode)) {
		used_blocks = (ip->i_size+fs->s_blocksize-1) / fs->s_blocksize;
		for(i = used_blocks; i < EXT2_NDIR_BLOCKS; i++)
			ip->i_db[i] = 0;
	}
/*
	ext2_print_inode(ip);
*/
	brelse(bp);

	/*
	 * Initialize the vnode from the inode, check for aliases.
	 * Note that the underlying vnode may have changed.
	 */
	if ((error = ext2_vinit(mp, ext2_specop_p, ext2_fifoop_p, &vp)) != 0) {
		vput(vp);
		*vpp = NULL;
		return (error);
	}
	/*
	 * Finish inode initialization now that aliasing has been resolved.
	 */
	ip->i_devvp = ump->um_devvp;
	VREF(ip->i_devvp);
	/*
	 * Set up a generation number for this inode if it does not
	 * already have one. This should only happen on old filesystems.
	 */
	if (ip->i_gen == 0) {
		ip->i_gen = random() / 2 + 1;
		if ((vp->v_mount->mnt_flag & MNT_RDONLY) == 0)
			ip->i_flag |= IN_MODIFIED;
	}
   
   /* Setup UBC info for VREG*/
   #ifdef APPLE
   if (S_ISREG(ip->i_mode)) {
      if (UBCINFOMISSING(vp) || UBCINFORECLAIMED(vp))
         ubc_info_init(vp);
   }
   #endif
   
	*vpp = vp;
	return (0);
}

/*
 * File handle to vnode
 *
 * Have to be really careful about stale file handles:
 * - check that the inode number is valid
 * - call ext2_vget() to get the locked inode
 * - check for an unallocated inode (i_mode == 0)
 * - check that the given client host has export rights and return
 *   those rights via. exflagsp and credanonp
 */
static int
#ifndef APPLE
ext2_fhtovp(mp, fhp, vpp)
#else
ext2_fhtovp(mp, fhp, nam, vpp, exflagsp, credanonp)
#endif /* APPLE */
	struct mount *mp;
	struct fid *fhp;
   #ifdef APPLE
   struct mbuf *nam;
   #endif
	struct vnode **vpp;
   #ifdef APPLE
   int *exflagsp;
   struct ucred **credanonp;
   #endif
{
	struct inode *ip;
	struct ufid *ufhp;
	struct vnode *nvp;
	struct ext2_sb_info *fs;
	int error;

	ufhp = (struct ufid *)fhp;
	fs = VFSTOEXT2(mp)->um_e2fs;
	if (ufhp->ufid_ino < ROOTINO ||
	    ufhp->ufid_ino > fs->s_groups_count * le32_to_cpu(fs->s_es->s_inodes_per_group))
		return (ESTALE);
   
   #ifndef APPLE
	error = VFS_VGET(mp, ufhp->ufid_ino, LK_EXCLUSIVE, &nvp);
   #else
   error = VFS_VGET(mp, (void*)ufhp->ufid_ino, &nvp);
   #endif / * APPLE */
	if (error) {
		*vpp = NULLVP;
		return (error);
	}
	ip = VTOI(nvp);
	if (ip->i_mode == 0 ||
	    ip->i_gen != ufhp->ufid_gen || ip->i_nlink <= 0) {
		vput(nvp);
		*vpp = NULLVP;
		return (ESTALE);
	}
	*vpp = nvp;
	return (0);
}

/*
 * Vnode pointer to File handle
 */
/* ARGSUSED */
static int
ext2_vptofh(vp, fhp)
	struct vnode *vp;
	struct fid *fhp;
{
	struct inode *ip;
	struct ufid *ufhp;

	ip = VTOI(vp);
	ufhp = (struct ufid *)fhp;
	ufhp->ufid_len = sizeof(struct ufid);
	ufhp->ufid_ino = ip->i_number;
	ufhp->ufid_gen = ip->i_gen;
	return (0);
}

/*
 * Write a superblock and associated information back to disk.
 */
static int
ext2_sbupdate(mp, waitfor)
	struct ext2mount *mp;
	int waitfor;
{
	struct ext2_sb_info *fs = mp->um_e2fs;
	struct ext2_super_block *es = fs->s_es;
	struct buf *bp;
	int error = 0;
   #ifdef APPLE
   int devBlockSize=0;
   
   VOP_DEVBLOCKSIZE(mp->um_devvp, &devBlockSize);
   #endif
/*
printf("\nupdating superblock, waitfor=%s\n", waitfor == MNT_WAIT ? "yes":"no");
*/
	bp = getblk(mp->um_devvp, SBLOCK, fs->s_blocksize, 0, 0
   #ifdef APPLE
   ,BLK_META
   #endif
   );
	bcopy((caddr_t)es, (bp->b_data+SBOFF), (u_int)sizeof(struct ext2_super_block));
	if (waitfor == MNT_WAIT)
		error = bwrite(bp);
	else
		bawrite(bp);

	/*
	 * The buffers for group descriptors, inode bitmaps and block bitmaps
	 * are not busy at this point and are (hopefully) written by the
	 * usual sync mechanism. No need to write them here
		 */

	return (error);
}

/*
 * Return the root of a filesystem.
 */
static int
ext2_root(mp, vpp)
	struct mount *mp;
	struct vnode **vpp;
{
	struct vnode *nvp;
	int error;

	#ifndef APPLE
	error = VFS_VGET(mp, (ino_t)ROOTINO, LK_EXCLUSIVE, &nvp);
   #else
   error = VFS_VGET(mp, (void*)ROOTINO, &nvp);
   #endif / * APPLE */
	if (error)
		return (error);
	*vpp = nvp;
	return (0);
}

static int
ext2_init(struct vfsconf *vfsp)
{

	ext2_ihashinit();
	return (0);
}

static int
ext2_uninit(struct vfsconf *vfsp)
{

	ext2_ihashuninit();
	return (0);
}


#ifdef APPLE

/*
 * Check if vnode represents a disk device
 */
int
vn_isdisk(vp, errp)
	struct vnode *vp;
	int *errp;
{
	#if 0
   struct cdevsw *cdevsw;
   #endif

	if (vp->v_type != VBLK) {
		if (errp != NULL)
			*errp = ENOTBLK;
		return (0);
	}
	if (vp->v_rdev == NULL) {
		if (errp != NULL)
			*errp = ENXIO;
		return (0);
	}
   #if 0
	cdevsw = devsw(vp->v_rdev);
	if (cdevsw == NULL) {
		if (errp != NULL)
			*errp = ENXIO;
		return (0);
	}
	if (cdevsw->d_flags != D_DISK) {
		if (errp != NULL)
			*errp = ENOTBLK;
		return (0);
	}
   #endif
	if (errp != NULL)
		*errp = 0;
	return (1);
}

/*
 * Vfs start routine, a no-op.
 */
/* ARGSUSED */
static int
vfs_stdstart(mp, flags, p)
	struct mount *mp;
	int flags;
	struct proc *p;
{
	return 0;
}

/*
 * Do operations associated with quotas, not supported
 */
/* ARGSUSED */
static int
vfs_stdquotactl(mp, cmd, uid, arg, p)
	struct mount *mp;
	int cmd;
	uid_t uid;
	caddr_t arg;
	struct proc *p;
{
	return EOPNOTSUPP;
}

static int
ext2_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
	   size_t newlen, struct proc *p)
{
   return (ENOTSUP);
}

extern int vfs_opv_numops; // kernel
typedef int (*PFI)();

/* Must be called while holding kernel funnel */
static void init_vnodeopv_desc (struct vnodeopv_desc *opv)
{
   int	(***opv_desc_vector_p)(void *);
	int	(**opv_desc_vector)(void *);
	struct vnodeopv_entry_desc	*opve_descp;
   int j;
   
   opv_desc_vector_p = opv->opv_desc_vector_p;
   
   /* Something better than M_TEMP */
   MALLOC(*opv_desc_vector_p, PFI *, vfs_opv_numops * sizeof(PFI),
	       M_TEMP, M_WAITOK);
	bzero(*opv_desc_vector_p, vfs_opv_numops*sizeof(PFI));

	opv_desc_vector = *opv_desc_vector_p;
   
   for (j = 0; opv->opv_desc_ops[j].opve_op; j++) {
		opve_descp = &(opv->opv_desc_ops[j]);

		/*
		 * Sanity check:  is this operation listed
		 * in the list of operations?  We check this
		 * by seeing if its offest is zero.  Since
		 * the default routine should always be listed
		 * first, it should be the only one with a zero
		 * offset.  Any other operation with a zero
		 * offset is probably not listed in
		 * vfs_op_descs, and so is probably an error.
		 *
		 * A panic here means the layer programmer
		 * has committed the all-too common bug
		 * of adding a new operation to the layer's
		 * list of vnode operations but
		 * not adding the operation to the system-wide
		 * list of supported operations.
		 */
		if (opve_descp->opve_op->vdesc_offset == 0 &&
		    opve_descp->opve_op->vdesc_offset != VOFFSET(vop_default)) {
			printf("init_vnodeopv_desc: operation %s not listed in %s.\n",
			       opve_descp->opve_op->vdesc_name,
			       "vfs_op_descs");
			panic("init_vnodeopv_desc: bad operation");
		}
		/*
		 * Fill in this entry.
		 */
		opv_desc_vector[opve_descp->opve_op->vdesc_offset] =
		    opve_descp->opve_impl;
	}

	/*  
	 * Finally, go back and replace unfilled routines
	 * with their default.  (Sigh, an O(n^3) algorithm.  I
	 * could make it better, but that'd be work, and n is small.)
	 */  
	opv_desc_vector_p = opv->opv_desc_vector_p;

	/*   
	 * Force every operations vector to have a default routine.
	 */  
	opv_desc_vector = *opv_desc_vector_p;
	if (opv_desc_vector[VOFFSET(vop_default)] == NULL)
	    panic("init_vnodeopv_desc: operation vector without default routine.");
	for (j = 0; j < vfs_opv_numops; j++) {
		if (opv_desc_vector[j] == NULL)
			opv_desc_vector[j] = opv_desc_vector[VOFFSET(vop_default)];
   }
}

// Kernel entry/exit points

__private_extern__ struct vnodeopv_desc ext2fs_vnodeop_opv_desc;
__private_extern__ struct vnodeopv_desc ext2fs_specop_opv_desc;
__private_extern__ struct vnodeopv_desc ext2fs_fifoop_opv_desc;

kern_return_t ext2fs_start (kmod_info_t * ki, void * d) {
   struct vfsconf	*vfsConf = NULL;
   int funnelState;
   kern_return_t kret;
   
   // Register our module
   funnelState = thread_funnel_set(kernel_flock, TRUE);
   
   MALLOC(vfsConf, void *, sizeof(struct vfsconf), M_TEMP, M_WAITOK);
	if (NULL == vfsConf) {
      kret = ENOMEM;
      goto funnel_release;
   }
      
   bzero(vfsConf, sizeof(struct vfsconf));
   
   vfsConf->vfc_vfsops = &ext2fs_vfsops;
   strncpy(&vfsConf->vfc_name[0], EXT2FS_NAME, MFSNAMELEN);
   vfsConf->vfc_typenum = maxvfsconf++; /* kernel global */
	vfsConf->vfc_refcount = 0;
	vfsConf->vfc_flags = 0; /* |MNT_QUOTA */
	vfsConf->vfc_mountroot = NULL; /* boot support */
	vfsConf->vfc_next = NULL;
   
   init_vnodeopv_desc(&ext2fs_vnodeop_opv_desc);
   init_vnodeopv_desc(&ext2fs_specop_opv_desc);
   init_vnodeopv_desc(&ext2fs_fifoop_opv_desc);
   
   kret = vfsconf_add(vfsConf);
   #ifdef DIAGNOSTIC
   if (kret) {
      printf ("ext2fs_start: Failed to register with kernel, error = %d\n", kret);
   }
   #endif

funnel_release:
	if (vfsConf)
		FREE(vfsConf, M_TEMP);

	thread_funnel_set(kernel_flock, funnelState);
   
   if (kret)
      return (KERN_FAILURE);
   
   return (KERN_SUCCESS);
}

kern_return_t ext2fs_stop (kmod_info_t * ki, void * d) {
   int funnelState;
   
   // Deregister with the kernel
   funnelState = thread_funnel_set(kernel_flock, TRUE);

	vfsconf_del(EXT2FS_NAME);
	FREE(*ext2fs_vnodeop_opv_desc.opv_desc_vector_p, M_TEMP);
   FREE(*ext2fs_specop_opv_desc.opv_desc_vector_p, M_TEMP);
   FREE(*ext2fs_fifoop_opv_desc.opv_desc_vector_p, M_TEMP);

	thread_funnel_set(kernel_flock, funnelState);
   
   ext2_uninit(NULL);
   return (KERN_SUCCESS);
}

#endif /* APPLE */
