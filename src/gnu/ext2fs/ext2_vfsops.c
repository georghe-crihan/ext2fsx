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
/*
* Copyright 2003-2004 Brian Bergstrand.
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

static const char whatid[] __attribute__ ((unused)) =
"@(#) $Id$";

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/namei.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#include <sys/vnode.h>
#include <sys/vnode_if.h>
#include <sys/mount.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/malloc.h>
#include <sys/stat.h>
#include <sys/disk.h>
#include <sys/syslog.h>
#include <sys/sysctl.h>

#include <string.h>
#include <machine/spl.h>

#include "ext2_apple.h"

static int vn_isdisk(vnode_t, int *);

#include <gnu/ext2fs/ext2_mount.h>
#include <gnu/ext2fs/inode.h>

#include <gnu/ext2fs/fs.h>
#include <gnu/ext2fs/ext2_extern.h>
#include <gnu/ext2fs/ext2_fs.h>
#include <gnu/ext2fs/ext2_fs_sb.h>
#include <ext2_byteorder.h>

/* VOPS */
static int ext2_fhtovp(mount_t , struct fid *, vnode_t *, vfs_context_t);
static int ext2_flushfiles(mount_t mp, int flags, vfs_context_t);
static int ext2_init(struct vfsconf *);
static int ext2_mount(mount_t, vnode_t, caddr_t, vfs_context_t);
static int ext2_mountfs(vnode_t, mount_t, vfs_context_t);
static int ext2_reload(mount_t mountp, vfs_context_t);
static int ext2_root(mount_t, vnode_t *vpp, vfs_context_t);
static int ext2_sbupdate(vfs_context_t, struct ext2mount *, int);
static int ext2_statfs(mount_t, struct vfsstatfs *, vfs_context_t);
static int ext2_sync(mount_t, int, vfs_context_t);
static int ext2_uninit(struct vfsconf *);
static int ext2_unmount(mount_t, int, vfs_context_t);
static int ext2_vget(mount_t, void *, vnode_t *, vfs_context_t);
static int ext2_vptofh(vnode_t, struct fid *, vfs_context_t);

static int ext2_sysctl(int *, u_int, void *, size_t *, void *, size_t, vfs_context_t);
static int vfs_stdstart(mount_t , int, vfs_context_t);
static int vfs_stdquotactl(mount_t , int, uid_t, caddr_t, enum uio_seg, vfs_context_t);

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

struct ext2_iter_cargs {
	vfs_context_t ca_vctx;
	int ca_wait;
	int ca_err;
};

static struct vfsops ext2fs_vfsops = {
   ext2_mount,
   vfs_stdstart,
	ext2_unmount,
	ext2_root,		/* root inode via vget */
	vfs_stdquotactl,
	ext2_statfs,
	ext2_sync,
	ext2_vget,
	ext2_fhtovp,
	ext2_vptofh,
	ext2_init,
   ext2_sysctl
};

#define bsd_malloc _MALLOC
#define bsd_free FREE

static int ext2fs_inode_hash_lock;

static int	ext2_check_sb_compat(struct ext2_super_block *es, dev_t dev,
		    int ronly);
static int	compute_sb_data(vnode_t devvp, vfs_context_t context,
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
	mount_t mp;
	struct thread *td = curthread;
	struct ext2mount *ump;
	u_int size;
	int error;
	
	if ((error = bdevvp(rootdev, &rootvp))) {
		printf("ext2_mountroot: can't find rootvp\n");
		ext2_trace_return(error);
	}
	mp = bsd_malloc((u_long)sizeof(struct mount), M_MOUNT, M_WAITOK);
	bzero((char *)mp, (u_long)sizeof(struct mount));
	TAILQ_INIT(&mp->mnt_nvnodelist);
	TAILQ_INIT(&mp->mnt_reservedvnlist);
	mp->mnt_op = &ext2fs_vfsops;
	mp->mnt_flag = MNT_RDONLY;
	if (error = ext2_mountfs(rootvp, mp, td)) {
		bsd_free(mp, M_MOUNT);
		ext2_trace_return(error);
	}
	if (error = vfs_lock(mp)) {
		(void)ext2_unmount(mp, 0, td);
		bsd_free(mp, M_MOUNT);
		ext2_trace_return(error);
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
ext2_mount(mp, devvp, data, context)
	mount_t mp;
	vnode_t devvp;
    caddr_t data;
	vfs_context_t context;
{
#ifdef obsolete
    struct nameidata nd;
	struct vnode *devvp;
#endif
	struct export_args *export;
	struct ext2mount *ump = 0;
	struct ext2_sb_info *fs;
    struct ext2_args args;
    char *fspec, *path;
	size_t size;
	int error, flags;
	mode_t accessmode;
	proc_t p = vfs_context_proc(context);
   
   if ((error = copyin(CAST_USER_ADDR_T(data), (caddr_t)&args, sizeof (struct ext2_args))) != 0)
		ext2_trace_return(error);
   
   export = &args.export;
   
   size = 0;
#ifdef obsolete
   (void) copyinstr(CAST_USER_ADDR_T(args.fspec), (vfs_statfs(mp))->f_mntfromname,
		MNAMELEN - 1, &size);
	bzero(mp->mnt_stat.f_mntfromname + size, MNAMELEN - size);
#endif
   fspec = (vfs_statfs(mp))->f_mntfromname;

	/*
	 * If updating, check whether changing from read-only to
	 * read/write; if there is no device name, that's all we do.
	 */
	if (vfs_isupdate(mp)) {
		ump = VFSTOEXT2(mp);
		fs = ump->um_e2fs;
		error = 0;
		if (fs->s_rd_only == 0 && vfs_isrdonly(mp)) {
			flags = WRITECLOSE;
			if (vfs_isforce(mp))
				flags |= FORCECLOSE;
			if (vfs_busy(mp, 0))
				ext2_trace_return(EBUSY);
			error = ext2_flushfiles(mp, flags, context);
			vfs_unbusy(mp);
			if (!error && fs->s_wasvalid) {
				fs->s_es->s_state =
               cpu_to_le16(le16_to_cpu(fs->s_es->s_state) | EXT2_VALID_FS);
				ext2_sbupdate(context, ump, MNT_WAIT);
			}
			fs->s_rd_only = 1;
		}
		if (!error && vfs_isreload(mp))
			error = ext2_reload(mp, context);
		if (error)
			ext2_trace_return(error);
#ifdef obsolete
		devvp = ump->um_devvp;
#else
		if (devvp != ump->um_devvp)
			ext2_trace_return(EINVAL);
#endif
		if (ext2_check_sb_compat(fs->s_es, vnode_specrdev(devvp),
		    0 == vfs_iswriteupgrade(mp)) != 0)
			ext2_trace_return(EPERM);
		if (fs->s_rd_only && vfs_iswriteupgrade(mp)) {
			/*
			 * If upgrade to read-write by non-root, then verify
			 * that user has necessary permissions on the device.
			 */
            if (suser(vfs_context_ucred(context), &p->p_acflag)) {
				vnode_lock(devvp);
				if ((error = VNOP_ACCESS(devvp, VREAD | VWRITE,
				    context)) != 0) {
					vnode_unlock(devvp);
					ext2_trace_return(error);
				}
				vnode_unlock(devvp);
			}

			if ((le16_to_cpu(fs->s_es->s_state) & EXT2_VALID_FS) == 0 ||
			    (le16_to_cpu(fs->s_es->s_state) & EXT2_ERROR_FS)) {
				if (vfs_isforce(mp)) {
					printf(
"EXT2 WARNING: %s was not properly dismounted\n",
					    fs->fs_fsmnt);
				} else {
					printf(
"EXT2 WARNING: R/W mount of %s denied.  Filesystem is not clean - run fsck\n",
					    fs->fs_fsmnt);
					ext2_trace_return(EPERM);
				}
			}
			fs->s_es->s_state =
            cpu_to_le16(le16_to_cpu(fs->s_es->s_state) & ~EXT2_VALID_FS);
			ext2_sbupdate(context, ump, MNT_WAIT);
			fs->s_rd_only = 0;
		}
		if (fspec == NULL) {
         #if 0
         struct export_args apple_exp;
         struct netexport apple_netexp;
         
         ext2_trace_return(vfs_export(mp, &apple_netexp, export));
         #endif
         ext2_trace_return(EINVAL);
		}
	}
#ifdef obsolete
	/*
	 * Not an update, or updating the name: look up the name
	 * and verify that it refers to a sensible block device.
	 */
	if (fspec == NULL)
		ext2_trace_return(EINVAL);
	NDINIT(&nd, LOOKUP, FOLLOW, UIO_SYSSPACE, fspec, p);
	if ((error = namei(&nd)) != 0)
		ext2_trace_return(error);
#ifndef APPLE
    NDFREE(ndp, NDF_ONLY_PNBUF);
#endif /* APPLE */
	devvp = ndp->ni_vp;
#else
	vnode_ref(devvp);
#endif // obsolete

	if (!vn_isdisk(devvp, &error)) {
		vnode_rele(devvp);
		ext2_trace_return(error);
	}

	/*
	 * If mount by non-root, then verify that user has necessary
	 * permissions on the device.
	 */
   if (suser(p->p_ucred, &p->p_acflag)) {
		accessmode = VREAD;
		if (vfs_isrdonly(mp))
			accessmode |= VWRITE;
		vnode_lock(devvp);
		if ((error = VNOP_ACCESS(devvp, accessmode, context)) != 0) {
			vnode_put(devvp);
			ext2_trace_return(error);
		}
		vnode_unlock(devvp);
	}
   
   /* This is used by ext2_mountfs to set the last mount point in the superblock. */
#ifdef obsolete
   size = 0;
   (void) copyinstr(path, mp->mnt_stat.f_mntonname, MNAMELEN - 1, &size);
#endif
   path = (vfs_statfs(mp))->f_mntonname;
#ifdef obsolete
   #ifdef EXT2FS_DEBUG
   if (size < 2)
      log(LOG_WARNING, "ext2fs: mount path looks to be invalid\n");
   #endif
   bzero(mp->mnt_stat.f_mntonname + size, MNAMELEN - size);
#endif

    if (vfs_isupdate(mp)) {
		error = ext2_mountfs(devvp, mp, context);
	} else {
		if (devvp != ump->um_devvp)
			error = EINVAL;	/* needs translation */
		else
			vnode_rele(devvp);
	}
	if (error) {
		vnode_rele(devvp);
		ext2_trace_return(error);
	}
    /* ump is setup by ext2_mountfs */
    ump = VFSTOEXT2(mp);
	fs = ump->um_e2fs;
    
	strncpy(fs->fs_fsmnt, (vfs_statfs(mp))->f_mntonname, MAXMNTLEN-1);
	size = strlen(fs->fs_fsmnt);
	bzero(fs->fs_fsmnt + size, MAXMNTLEN - size);
	fs->s_mount_opt = args.e2_mnt_flags;
	
	if ((vfs_flags(mp) & MNT_UNKNOWNPERMISSIONS)
		 && 0 == (vfs_flags(mp) & MNT_ROOTFS) && args.e2_uid > 0) {
		fs->s_uid_noperm = args.e2_uid;
		fs->s_gid_noperm = args.e2_gid;
	} else {
		vfs_clearflags(mp, MNT_UNKNOWNPERMISSIONS);
	}

	(void)ext2_statfs(mp, vfs_statfs(mp), context);
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
	
	sb->s_dircount = 0;
	
	/* ext2_debug ("Checking group descriptors"); */
	for (i = 0; i < sb->s_groups_count; i++) {
		/* examine next descriptor block */
		if ((i % EXT2_DESC_PER_BLOCK(sb)) == 0)
		gdp = (struct ext2_group_desc *)buf_dataptr(sb->s_group_desc[desc_block++]);
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
		/* Compute initial directory count */
		sb->s_dircount += le16_to_cpu(gdp->bg_used_dirs_count);
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
		ext2_trace_return(1);
	}
	if (le32_to_cpu(es->s_rev_level) > EXT2_GOOD_OLD_REV) {
		if (le32_to_cpu(es->s_feature_incompat) & ~EXT2_FEATURE_INCOMPAT_SUPP) {
			printf(
"EXT2 WARNING: mount of %s denied due to unsupported optional features (%08X)\n",
			    devtoname(dev),
             (le32_to_cpu(es->s_feature_incompat) & ~EXT2_FEATURE_INCOMPAT_SUPP));
			ext2_trace_return(1);
		}
		if (!ronly &&
		    (le32_to_cpu(es->s_feature_ro_compat) & ~EXT2_FEATURE_RO_COMPAT_SUPP)) {
			printf(
"EXT2 WARNING: R/W mount of %s denied due to unsupported optional features (%08X)\n",
			    devtoname(dev),
             (le32_to_cpu(es->s_feature_ro_compat) & ~EXT2_FEATURE_RO_COMPAT_SUPP));
			ext2_trace_return(1);
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
static int compute_sb_data(devvp, context, es, fs)
	vnode_t devvp;
	vfs_context_t context;
	struct ext2_super_block * es;
	struct ext2_sb_info * fs;
{
    int db_count, error;
    int i, j;
    int logic_sb_block = 1;	/* XXX for now */
    /* fs->s_d_blocksize has not been set yet */
    u_int32_t devBlockSize=0;
    
    EVOP_DEVBLOCKSIZE(devvp, &devBlockSize, context);

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
    fs->s_blocksize_bits = EXT2_BLOCK_SIZE_BITS(es);
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
   if (EXT2_FEATURE_RO_COMPAT_SUPP & EXT2_FEATURE_RO_COMPAT_LARGE_FILE) {
      u_int64_t addrs_per_block = fs->s_blocksize / sizeof(u32);
	  fs->s_maxfilesize = (EXT2_NDIR_BLOCKS*(u_int64_t)fs->s_blocksize) +
		(addrs_per_block*(u_int64_t)fs->s_blocksize) +
		((addrs_per_block*addrs_per_block)*(u_int64_t)fs->s_blocksize) +
		((addrs_per_block*addrs_per_block*addrs_per_block)*(u_int64_t)fs->s_blocksize);
   } else
      fs->s_maxfilesize = 0x7FFFFFFFLL;
	for (i = 0; i < 4; ++i)
		fs->s_hash_seed[i] = le32_to_cpu(es->s_hash_seed[i]);
	fs->s_def_hash_version = es->s_def_hash_version;

    fs->s_group_desc = bsd_malloc(db_count * sizeof (buf_t ),
		M_EXT2MNT, M_WAITOK);

    /* adjust logic_sb_block */
    if(fs->s_blocksize > SBSIZE)
	/* Godmar thinks: if the blocksize is greater than 1024, then
	   the superblock is logically part of block zero. 
	 */
        logic_sb_block = 0;
    
    for (i = 0; i < db_count; i++) {
      error = buf_meta_bread(devvp , (daddr64_t)fsbtodb(fs, logic_sb_block + i + 1), 
         fs->s_blocksize, NOCRED, &fs->s_group_desc[i]);
      if(error) {
            for (j = 0; j < i; j++)
               ULCK_BUF(fs->s_group_desc[j]);
            bsd_free(fs->s_group_desc, M_EXT2MNT);
            printf("EXT2-fs: unable to read group descriptors (%d)\n", error);
            return EIO;
      }
      LCK_BUF(fs->s_group_desc[i]);
    }
    if(!ext2_check_descriptors(fs)) {
	    for (j = 0; j < db_count; j++)
		    ULCK_BUF(fs->s_group_desc[j]);
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
ext2_reload_callback(vnode_t vp, void *cargs)
{
	struct ext2_iter_cargs *args = (struct ext2_iter_cargs*)cargs;
	struct inode *ip;
	struct ext2_sb_info *fs;
	struct ext2mount *ump;
	buf_t bp;
	int err;
	
	/*
	 * Step 4: invalidate all inactive vnodes.
	 */
	if (vnode_recycle(vp))
		return (VNODE_RETURNED);
	/*
	 * Step 5: invalidate all cached file data.
	 */
	vnode_lock(vp);
	if (buf_invalidateblks(vp, BUF_WRITE_DATA, 0, 0))
		panic("ext2_reload: dirty2");
	/*
	 * Step 6: re-read inode data for all active vnodes.
	 */
	ip = VTOI(vp);
	ump = VFSTOEXT2(vnode_mount(vp));
	fs = ump->um_e2fs;
	err =
		buf_bread (ump->um_devvp, (daddr64_t)fsbtodb(fs, ino_to_fsba(fs, ip->i_number)),
			(int)fs->s_blocksize, NOCRED, &bp);
	if (err) {
		vnode_unlock(vp);
		args->ca_err = err;
		return (VNODE_RETURNED_DONE);
	}
	ext2_ei2i((struct ext2_inode *) ((char *)buf_dataptr +
		EXT2_INODE_SIZE * ino_to_fsbo(fs, ip->i_number)), ip);
	buf_brelse(bp);
	vnode_unlock(vp);
	return (VNODE_RETURNED);
}

static int
ext2_reload(mountp, context)
	mount_t mountp;
	vfs_context_t context;
{
	struct ext2_iter_cargs cargs;
	vnode_t devvp;
#ifdef obsolete
	vnode_t vp, nvp;
	struct inode *ip;
#endif
	buf_t bp;
	struct ext2_super_block * es;
	struct ext2_sb_info *fs;
#ifdef obsolete
	proc_t p = vfs_context_proc(context);
	ucred_t cred = vfs_context_ucred(context);
#endif
	int error;
    u_int32_t devBlockSize=0;

	if (vfs_isrdonly(mountp))
		ext2_trace_return(EINVAL);
	/*
	 * Step 1: invalidate all cached meta-data.
	 */
	devvp = VFSTOEXT2(mountp)->um_devvp;
	if (buf_invalidateblks(devvp, BUF_WRITE_DATA, 0, 0))
		panic("ext2_reload: dirty1");
	/*
	 * Step 2: re-read superblock from disk.
	 * constants have been adjusted for ext2
	 */
   /* Get the current block size */
   EVOP_DEVBLOCKSIZE(devvp, &devBlockSize, context);
   
	if ((error = buf_meta_bread(devvp, (daddr64_t)SBLOCK, SBSIZE, NOCRED, &bp)) != 0)
		ext2_trace_return(error);
	es = (struct ext2_super_block *)(buf_dataptr(bp)+SBOFF);
	if (ext2_check_sb_compat(es, vnode_specrdev(devvp), 0) != 0) {
		buf_brelse(bp);
		ext2_trace_return(EIO);		/* XXX needs translation */
	}
	fs = VFSTOEXT2(mountp)->um_e2fs;
	bcopy(es, fs->s_es, sizeof(struct ext2_super_block));

	if((error = compute_sb_data(devvp, context, es, fs)) != 0) {
		buf_brelse(bp);
		return error;
	}
#ifdef UNKLAR
	if (fs->fs_sbsize < SBSIZE)
		buf_markinvalid(bp);
#endif
	buf_brelse(bp);

#ifdef obsolete
loop:
   simple_lock(&mntvnode_slock);
   for (vp = LIST_FIRST(&mountp->mnt_vnodelist); vp != NULL; vp = nvp) {
		if (vp->v_mount != mountp) {
			simple_unlock(&mntvnode_slock);
			goto loop;
		}
        nvp = LIST_NEXT(vp, v_mntvnodes);
		/*
		 * Step 4: invalidate all inactive vnodes.
		 */
  		if (vrecycle(vp, &mntvnode_slock, td))
  			goto loop;
		/*
		 * Step 5: invalidate all cached file data.
		 */
        /* XXX Can cause spinlock deadlock because of a bug in vget() when
           using LK_INTERLOCK. Radar Bug #3193564 -- closed as "Behaves Correctly".
        simple_lock (&vp->v_interlock); */
		simple_unlock(&mntvnode_slock);
		if (vget(vp, LK_EXCLUSIVE /*| LK_INTERLOCK */, td)) {
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
			ext2_trace_return(error);
		}
		ext2_ei2i((struct ext2_inode *) ((char *)bp->b_data +
		    EXT2_INODE_SIZE * ino_to_fsbo(fs, ip->i_number)), ip);
		brelse(bp);
		vput(vp);
		simple_lock(&mntvnode_slock);
	}
	simple_unlock(&mntvnode_slock);
#endif //obsolete
	cargs.ca_vctx = context;
	cargs.ca_wait = cargs.ca_err = 0;
	if ((error = vnode_iterate(mountp, VNODE_RELOAD|VNODE_NOLOCK_INTERNAL,
		ext2_reload_callback, &cargs)))
		ext2_trace_return(error);
	if (cargs.ca_err)
		ext2_trace_return(cargs.ca_err);
	return (0);
}

/*
 * Common code for mount and mountroot
 */
static int
ext2_mountfs(devvp, mp, context)
	vnode_t devvp;
	mount_t mp;
	vfs_context_t context;
{
	struct timeval tv;
    struct ext2mount *ump;
	buf_t bp;
	struct ext2_sb_info *fs;
	struct ext2_super_block * es;
	dev_t dev = vnode_specrdev(devvp);
	int error;
	int ronly;
    u_int32_t devBlockSize=0;
   
    getmicrotime(&tv); /* Curent time */

	/*
	 * Disallow multiple mounts of the same device.
	 * Disallow mounting of a device that is currently in use
	 * (except for root, which might share swap device for miniroot).
	 * Flush out any old buffers remaining from a previous use.
	 */
#ifdef obsolete
	if ((error = vfs_mountedon(devvp)) != 0)
		ext2_trace_return(error);
	if (vcount(devvp) > 1 && devvp != rootvp)
		ext2_trace_return(EBUSY);
#endif
	if ((error = buf_invalidateblks(devvp, BUF_WRITE_DATA, 0, 0)) != 0)
		ext2_trace_return(error);
#ifdef READONLY
/* turn on this to force it to be read-only */
	vfs_setflags(mp, MNT_RDONLY);
#endif

	ronly = vfs_isrdonly(mp);
	vnode_lock(devvp);
	error = VNOP_OPEN(devvp, ronly ? FREAD : FREAD|FWRITE, context);
	vnode_unlock(devvp);
	if (error)
		ext2_trace_return(error);
   
   /* Set the block size to 512. Things just seem to royally screw 
      up otherwise.
    */
   devBlockSize = 512;
   if (VNOP_IOCTL(devvp, DKIOCSETBLOCKSIZE, (caddr_t)&devBlockSize,
         FWRITE, context)) {
      ext2_trace_return(ENXIO);
   }
   /* force specfs to re-read the new size */
   set_fsblocksize(devvp);
   
   /* cache the IO attributes */
	if ((error = vfs_init_io_attributes(devvp, mp))) {
		printf("ext2_mountfs: vfs_init_io_attributes returned %d\n",
			error);
		ext2_trace_return(error);
	}
   
   /* EVOP_DEVBLOCKSIZE(devvp, &devBlockSize, context); */
   
	bp = NULL;
	ump = NULL;
	#if defined(DIAGNOSTIC)
	printf("ext2fs: reading superblock from block %u, with size %u and offset %u\n",
		SBLOCK, SBSIZE, SBOFF);
	#endif
	if ((error = buf_meta_bread(devvp, (daddr64_t)SBLOCK, SBSIZE, NOCRED, &bp)) != 0)
		goto out;
	es = (struct ext2_super_block *)(buf_dataptr(bp)+SBOFF);
	if (ext2_check_sb_compat(es, dev, ronly) != 0) {
		error = EINVAL;		/* XXX needs translation */
		goto out;
	}
	if ((le16_to_cpu(es->s_state) & EXT2_VALID_FS) == 0 ||
	    (le16_to_cpu(es->s_state) & EXT2_ERROR_FS)) {
		if (ronly || vfs_isforce(mp)) {
			printf(
"EXT2-fs WARNING: Filesystem was not properly dismounted\n");
		} else {
			printf(
"EXT2-fs WARNING: R/W mount denied.  Filesystem is not clean - run fsck\n");
			error = EPERM;
			goto out;
		}
	}
   if ((int16_t)le16_to_cpu(es->s_max_mnt_count) >= 0 &&
		 le16_to_cpu(es->s_mnt_count) >= (u_int16_t)le16_to_cpu(es->s_max_mnt_count))
		printf ("EXT2-fs WARNING: maximal mount count reached, "
			"running fsck is recommended\n");
	else if (le32_to_cpu(es->s_checkinterval) &&
		(le32_to_cpu(es->s_lastcheck) + le32_to_cpu(es->s_checkinterval) <= tv.tv_sec))
		printf ("EXT2-fs WARNING: checktime reached, "
			"running fsck is recommended\n");
   
   /* UFS does this, so I assume we have the same shortcoming. */
   /*
	 * Buffer cache does not handle multiple pages in a buf when
	 * invalidating incore buffer in pageout. There are no locks 
	 * in the pageout path.  So there is a danger of loosing data when
	 * block allocation happens at the same time a pageout of buddy
	 * page occurs. incore() returns buf with both
	 * pages, this leads vnode-pageout to incorrectly flush of entire. 
	 * buf. Till the low level ffs code is modified to deal with these
	 * do not mount any FS more than 4K size.
	 */
   if ((EXT2_MIN_BLOCK_SIZE << le32_to_cpu(es->s_log_block_size)) > PAGE_SIZE) {
      error = ENOTSUP;
      goto out;
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
	if ((error = compute_sb_data(devvp, context, ump->um_e2fs->s_es, ump->um_e2fs)))
		goto out;
	/*
	 * We don't free the group descriptors allocated by compute_sb_data()
	 * until ext2_unmount().  This is OK since the mount will succeed.
	 */
	buf_brelse(bp);
	bp = NULL;
	fs = ump->um_e2fs;
    /* Init the lock */
	fs->s_lock = lck_mtx_alloc_init(EXT2_LCK_GRP, LCK_ATTR_NULL);
    assert(fs->s_lock != NULL);
   
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
	vfs_setfsprivate(mp, ump);
    vfs_getnewfsid(mp);
	vfs_setmaxsymlen(mp, EXT2_MAXSYMLINKLEN);
	vfs_setflags(mp, MNT_LOCAL); /* XXX Is this already set by vfs_fsadd? */
	ump->um_mountp = mp;
	ump->um_dev = dev;
	ump->um_devvp = devvp;
	/* setting those two parameters allowed us to use
	   ufs_bmap w/o changes !
	*/
	ump->um_nindir = EXT2_ADDR_PER_BLOCK(fs);
	ump->um_bptrtodb = le32_to_cpu(fs->s_es->s_log_block_size) + 1;
	ump->um_seqinc = EXT2_FRAGS_PER_BLOCK(fs);
    vnode_setmountedon(devvp);
   
   /* set device block size */
   fs->s_d_blocksize = devBlockSize;
   
   fs->s_es->s_mtime = cpu_to_le32(tv.tv_sec);
   if (!(int16_t)fs->s_es->s_max_mnt_count)
		fs->s_es->s_max_mnt_count = (int16_t)cpu_to_le16(EXT2_DFL_MAX_MNT_COUNT);
   fs->s_es->s_mnt_count = cpu_to_le16(le16_to_cpu(fs->s_es->s_mnt_count) + 1);
   /* last mount point */
   bzero(&fs->s_es->s_last_mounted[0], sizeof(fs->s_es->s_last_mounted));
   bcopy((caddr_t)(vfs_statfs(mp))->f_mntonname,
		(caddr_t)&fs->s_es->s_last_mounted[0],
		min(sizeof(fs->s_es->s_last_mounted), MNAMELEN));
	if (ronly == 0) 
		ext2_sbupdate(context, ump, MNT_WAIT);
	return (0);
out:
	if (bp)
		buf_brelse(bp);
	(void)VNOP_CLOSE(devvp, ronly ? FREAD : FREAD|FWRITE, context);
	if (ump) {
		vfs_setfsprivate(mp, NULL);
		bsd_free(ump->um_e2fs->s_es, M_EXT2MNT);
		bsd_free(ump->um_e2fs, M_EXT2MNT);
		bsd_free(ump, M_EXT2MNT);
	}
	ext2_trace_return(error);
}

/*
 * unmount system call
 */
static int
ext2_unmount(mp, mntflags, context)
	mount_t mp;
	int mntflags;
	vfs_context_t context;
{
	struct ext2mount *ump;
	struct ext2_sb_info *fs;
	int error, flags, ronly, i;

	flags = 0;
	if (mntflags & MNT_FORCE) {
		if (vfs_flags(mp) & MNT_ROOTFS)
			ext2_trace_return(EINVAL);
		flags |= FORCECLOSE;
	}
	if ((error = ext2_flushfiles(mp, flags, context)) != 0)
		ext2_trace_return(error);
	ump = VFSTOEXT2(mp);
	fs = ump->um_e2fs;
	ronly = fs->s_rd_only;
	if (ronly == 0) {
		if (fs->s_wasvalid)
			fs->s_es->s_state =
            cpu_to_le16(le16_to_cpu(fs->s_es->s_state) | EXT2_VALID_FS);
		ext2_sbupdate(context, ump, MNT_WAIT);
	}

	/* release buffers containing group descriptors */
	for(i = 0; i < fs->s_db_per_group; i++) 
		ULCK_BUF(fs->s_group_desc[i]);
	bsd_free(fs->s_group_desc, M_EXT2MNT);

	/* release cached inode/block bitmaps */
   for (i = 0; i < EXT2_MAX_GROUP_LOADED; i++)
      if (fs->s_inode_bitmap[i])
         ULCK_BUF(fs->s_inode_bitmap[i]);
   
   for (i = 0; i < EXT2_MAX_GROUP_LOADED; i++)
      if (fs->s_block_bitmap[i])
         ULCK_BUF(fs->s_block_bitmap[i]);

    vnode_clearmountedon(ump->um_devvp);
	error = VNOP_CLOSE(ump->um_devvp, ronly ? FREAD : FREAD|FWRITE, context);
	vnode_rele(ump->um_devvp);
   
    /* Free the lock alloc'd in mountfs */
    lck_mtx_free(fs->s_lock, EXT2_LCK_GRP);
   
	vfs_setfsprivate(mp, NULL);
	vfs_clearflags(mp, MNT_LOCAL);
	bsd_free(fs->s_es, M_EXT2MNT);
	bsd_free(fs, M_EXT2MNT);
	bsd_free(ump, M_EXT2MNT);
	ext2_trace_return(error);
}

/*
 * Flush out all the files in a filesystem.
 */
static int
ext2_flushfiles(mp, flags, context)
	mount_t mp;
	int flags;
	vfs_context_t context;
{
	int error;
	
    error = vflush(mp, NULLVP, SKIPSWAP|flags);
	error = vflush(mp, NULLVP, flags);
	ext2_trace_return(error);
}

/*
 * Get file system statistics.
 * taken from ext2/super.c ext2_statfs
 */
static int
ext2_statfs(mp, sbp, context)
	mount_t mp;
	struct vfsstatfs *sbp;
	vfs_context_t context;
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
#ifdef obsolete
	if (sbp != vfs_statfs(mp)) {
		sbp->f_type = vfs_typenum(mp);
		bcopy((caddr_t)mp->mnt_stat.f_mntonname,
			(caddr_t)&sbp->f_mntonname[0], MNAMELEN);
		bcopy((caddr_t)mp->mnt_stat.f_mntfromname,
			(caddr_t)&sbp->f_mntfromname[0], MNAMELEN);
	}
#endif
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
ext2_sync_callback(vnode_t vp, void *cargs)
{
	struct ext2_iter_cargs *args = (struct ext2_iter_cargs*)cargs;
	struct inode *ip;
	int error;
	
	VI_LOCK(vp);
	ip = VTOI(vp);
	/* The inode can be NULL when ext2_vget encounters an error from bread()
	and a sync() gets in before the vnode is invalidated.
	*/
	if (NULL == ip ||
		((ip->i_flag & (IN_ACCESS | IN_CHANGE | IN_MODIFIED | IN_UPDATE)) == 0 &&
		0 == vnode_hasdirtyblks(vp))) {
		VI_UNLOCK(vp);
		return (VNODE_RETURNED);
	}
	
	if ((error = VNOP_FSYNC(vp, (MNT_WAIT == args->ca_wait), args->ca_vctx)) != 0)
		args->ca_err = error;
	VI_UNLOCK(vp);
	return (VNODE_RETURNED);
}

static int
ext2_sync(mp, waitfor, context)
	mount_t mp;
	int waitfor;
	vfs_context_t context;
{
	struct ext2_iter_cargs args;
#ifdef obsolete
	vnode_t nvp, vp;
	struct inode *ip;
#endif
	struct ext2mount *ump = VFSTOEXT2(mp);
	struct ext2_sb_info *fs;
	int flags, error, allerror = 0;
	
	fs = ump->um_e2fs;
	if (fs->s_dirt != 0 && fs->s_rd_only != 0) {		/* XXX */
		printf("fs = %s\n", fs->fs_fsmnt);
		panic("ext2_sync: rofs mod");
	}
	// Called for each node attached to mount point.
	args.ca_vctx = context;
	args.ca_wait = waitfor;
	args.ca_err = 0;
	flags = VNODE_NOLOCK_INTERNAL|VNODE_NODEAD;
#ifdef notyet
	if (waitfor)
		flags |= VNODE_WAIT;
#endif
	if ((error = vnode_iterate(mp, flags, ext2_sync_callback, (void *)&args)))
		allerror = error;
	if (args.ca_err)
		allerror = args.ca_err;
#ifdef obsolete
	/*
	 * Write back each (modified) inode.
	 */
	simple_lock(&mntvnode_slock);
loop:
   for (vp = LIST_FIRST(&mp->mnt_vnodelist); vp != NULL; vp = nvp) {
		/*
		 * If the vnode that we are about to sync is no longer
		 * associated with this mount point, start over.
		 */
		if (vp->v_mount != mp)
			goto loop;
        VI_LOCK(vp);
		nvp = LIST_NEXT(vp, v_mntvnodes);
		ip = VTOI(vp);
		/* The inode can be NULL when ext2_vget encounters an error from bread()
			and a sync() gets in before the vnode is invalidated.
		 */
		if (NULL == ip || vp->v_flag & (VXLOCK|VORECLAIM)) {
			VI_UNLOCK(vp);
			continue;
		}
		if (vp->v_type == VNON ||
		    ((ip->i_flag & (IN_ACCESS | IN_CHANGE | IN_MODIFIED | IN_UPDATE)) == 0 &&
            LIST_EMPTY(&vp->v_dirtyblkhd) && !(vp->v_flag & VHASDIRTY))) {
			VI_UNLOCK(vp);
			continue;
		}
		
		/* XXX Can cause spinlock deadlock because of a bug in vget() when
           using LK_INTERLOCK. Radar Bug #3193564 -- closed as "Behaves Correctly". */
		/* XXX */ VI_UNLOCK(vp); /* XXX */
		simple_unlock(&mntvnode_slock);
		error = vget(vp, LK_EXCLUSIVE | LK_NOWAIT /*| LK_INTERLOCK */, td);
		if (error) {
			simple_lock(&mntvnode_slock);
			if (error == ENOENT)
				goto loop;
			continue;
		}
      
       didhold = ubc_hold(vp);
	   if ((error = VOP_FSYNC(vp, cred, waitfor, td)) != 0)
			allerror = error;
	   VOP_UNLOCK(vp, 0, td);
       if (didhold)
         ubc_rele(vp);
	   vrele(vp);
	   simple_lock(&mntvnode_slock);
	}
	simple_unlock(&mntvnode_slock);
#endif // obsolete

	/*
	 * Force stale file system control information to be flushed.
	 */
   #if 0
    if (waitfor != MNT_LAZY)
   #endif
    {
		vnode_lock(ump->um_devvp);
		if ((error = VNOP_FSYNC(ump->um_devvp, waitfor, context)) != 0)
			allerror = error;
		vnode_unlock(ump->um_devvp);
	}
	/*
	 * Write back modified superblock.
	 */
	if (fs->s_dirt != 0) {
		fs->s_dirt = 0;
		fs->s_es->s_wtime = cpu_to_le32(time_second);
		if ((error = ext2_sbupdate(context, ump, waitfor)) != 0)
			allerror = error;
	}
	ext2_trace_return(allerror);
}


static __inline__
void ext2_vget_irelse (struct inode *ip)
{
	ext2_ihashrem(ip);
	lck_mtx_destroy(&ip->i_lock, EXT2_LCK_GRP);
	FREE(ip, M_EXT2NODE);
}

/*
 * Look up an EXT2FS dinode number to find its incore vnode, otherwise read it
 * in from disk.  If it is in core, wait for the lock bit to clear, then
 * return the inode locked.  Detection and handling of mount points must be
 * done by the calling routine.
 */
static int
ext2_vget(mp, arg, vpp, context)
	mount_t mp;
    void *arg;
	vnode_t *vpp;
	vfs_context_t context;
{
	evinit_args_t viargs;
	struct ext2_sb_info *fs;
	struct inode *ip;
	struct ext2mount *ump;
	evalloc_args_t *vaargsp = (evalloc_args_t*)arg;
	buf_t bp;
	vnode_t vp;
	dev_t dev;
	int i, error;
	int used_blocks;
    int flags = LK_EXCLUSIVE;
    ino_t ino = vaargsp->va_ino;

#ifdef obsolete // XXX ???
   /* Check for unmount in progress */
	if (mp->mnt_kern_flag & MNTK_UNMOUNT) {
		*vpp = NULL;
		ext2_trace_return(EPERM);
	}
#endif

	ump = VFSTOEXT2(mp);
	dev = ump->um_dev;
restart:
	if ((error = ext2_ihashget(dev, ino, flags, vpp)) != 0)
		ext2_trace_return(error);
	if (*vpp != NULL)
		return (0);

	/*
	 * Lock out the creation of new entries in the hash table in
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
    assert(NULL != ip);
	bzero((caddr_t)ip, sizeof(struct inode));

#ifdef obsolete
	/* Allocate a new vnode/inode. */
   if ((error = getnewvnode(VT_EXT2, mp, ext2_vnodeop_p, &vp)) != 0) {
		if (ext2fs_inode_hash_lock < 0)
			wakeup(&ext2fs_inode_hash_lock);
		ext2fs_inode_hash_lock = 0;
		*vpp = NULL;
		FREE(ip, M_EXT2NODE);
		ext2_trace_return(error);
	}
	vp->v_data = ip;
	ip->i_vnode = vp;
#endif
	ip->i_e2fs = fs = ump->um_e2fs;
	ip->i_dev = dev;
	ip->i_number = ino;
    /* Init our private lock */
	lck_mtx_init(&ip->i_lock, EXT2_LCK_GRP, LCK_ATTR_NULL);
    assert(fs->s_lock != NULL);
   
	/*
	 * Put it onto its hash chain and lock it so that other requests for
	 * this inode will block if they arrive while we are sleeping waiting
	 * for old data structures to be purged or for the contents of the
	 * disk portion of this inode to be read.
	 */
	ILOCK(ip);
	ext2_ihashins(ip);

	if (ext2fs_inode_hash_lock < 0)
		wakeup(&ext2fs_inode_hash_lock);
	ext2fs_inode_hash_lock = 0;

	/* Read in the disk contents for the inode, copy into the inode. */
#if 0
printf("ext2_vget(%d) dbn= %d ", ino, fsbtodb(fs, ino_to_fsba(fs, ino)));
#endif
	if ((error = buf_bread(ump->um_devvp, (daddr64_t)fsbtodb(fs, ino_to_fsba(fs, ino)),
	    (int)fs->s_blocksize, NOCRED, &bp)) != 0) {
		/*
		 * The inode does not contain anything useful, so it would
		 * be misleading to leave it on its hash chain. With mode
		 * still zero, it will be unlinked and returned to the free
		 * list by vput().
		 */
#ifdef obsolete // XXX
		vnode_put(vp);
#endif
		IULOCK(ip);
		ext2_vget_irelse(ip);
		buf_brelse(bp);
		*vpp = NULL;
		ext2_trace_return(error);
	}
	/* convert ext2 inode to dinode */
	ext2_ei2i((struct ext2_inode *) ((char *)buf_dataptr(bp) + EXT2_INODE_SIZE *
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
	buf_brelse(bp);

	/*
	 * Create the vnode from the inode.
	 */
	vaargsp->va_vctx = context;
	viargs.vi_vallocargs = vaargsp;
	viargs.vi_ip = ip;
	viargs.vi_vnops = ext2_vnodeop_p;
	viargs.vi_specops = ext2_specop_p;
	viargs.vi_fifoops = ext2_fifoop_p;
	viargs.vi_flags = EXT2_VINIT_INO_LCKD; // vinit will unlock the inode
	if ((error = ext2_vinit(mp, &viargs, &vp)) != 0) {
#ifdef obsolete
		vnode_vput(vp);
#endif
		IULOCK(ip);
		ext2_vget_irelse(ip);
		*vpp = NULL;
		ext2_trace_return(error);
	}
	/*
	 * Finish inode initialization now that aliasing has been resolved.
	 */
	ip->i_vnode = vp;
	ip->i_devvp = ump->um_devvp;
	vnode_ref(ip->i_devvp);
	/*
	 * Set up a generation number for this inode if it does not
	 * already have one. This should only happen on old filesystems.
	 */
	if (ip->i_gen == 0) {
		ip->i_gen = random() / 2 + 1;
		if (0 == vfs_isrdonly(mp))
			ip->i_flag |= IN_MODIFIED;
	}
   
#ifdef obsolete
	/* Setup UBC info. */
    if (UBCINFOMISSING(vp) || UBCINFORECLAIMED(vp))
      ubc_info_init(vp);
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
ext2_fhtovp(mp, fhp, vpp, context)
	mount_t mp;
	struct fid *fhp;
	vnode_t *vpp;
	vfs_context_t context;
{
	struct inode *ip;
	struct ufid *ufhp;
	vnode_t nvp;
	struct ext2_sb_info *fs;
	int error;

	ufhp = (struct ufid *)fhp;
	fs = VFSTOEXT2(mp)->um_e2fs;
	if (ufhp->ufid_ino < ROOTINO ||
	    ufhp->ufid_ino > fs->s_groups_count * le32_to_cpu(fs->s_es->s_inodes_per_group))
		ext2_trace_return(ESTALE);
   
   error = VFS_VGET(mp, (void*)ufhp->ufid_ino, &nvp, context);
	if (error) {
		*vpp = NULLVP;
		ext2_trace_return(error);
	}
	ip = VTOI(nvp);
	if (ip->i_mode == 0 ||
	    ip->i_gen != ufhp->ufid_gen || ip->i_nlink <= 0) {
		vnode_put(nvp);
		*vpp = NULLVP;
		ext2_trace_return(ESTALE);
	}
	*vpp = nvp;
	return (0);
}

/*
 * Vnode pointer to File handle
 */
/* ARGSUSED */
static int
ext2_vptofh(vp, fhp, context)
	vnode_t vp;
	struct fid *fhp;
	vfs_context_t context;
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
ext2_sbupdate(context, mp, waitfor)
	vfs_context_t context;
	struct ext2mount *mp;
	int waitfor;
{
	struct ext2_sb_info *fs = mp->um_e2fs;
	struct ext2_super_block *es = fs->s_es;
	buf_t bp;
	int error = 0;
    u_int32_t devBlockSize=0, i;
   
    EVOP_DEVBLOCKSIZE(mp->um_devvp, &devBlockSize, context);
/*
printf("\nupdating superblock, waitfor=%s\n", waitfor == MNT_WAIT ? "yes":"no");
*/
	/* BDB - We don't want to unlock/release the buffers here, just sync them. Also,
		we can't use b[ad]write on group/cache buffers because they are locked (and 
		B_NORELSE is only respected on a sync write).
		*/
	
	/* group descriptors */
	lock_super(fs);
	for(i = 0; i < fs->s_db_per_group; i++) {
		bp = fs->s_group_desc[i];
		if (!BMETA_ISDIRTY(bp)) {
			continue;
		}
		//bp->b_flags |= (B_NORELSE|B_BUSY);
		//bp->b_flags &= ~B_DIRTY;
		BMETA_CLEAN(bp);
		buf_bwrite(bp);
		//bp->b_flags &= ~B_BUSY;
	}
	unlock_super(fs);
	
#if EXT2_SB_BITMAP_CACHE
	/* cached inode/block bitmaps */
	lock_super(fs);
	for (i = 0; i < EXT2_MAX_GROUP_LOADED; i++) {
		bp = fs->s_inode_bitmap[i];
		if (bp) {
			if (!(bp->b_flags & B_DIRTY)) {
				continue;
			} 
			bp->b_flags |= (B_NORELSE|B_BUSY);
			bp->b_flags &= ~B_DIRTY;
			buf_bwrite(bp);
			bp->b_flags &= ~B_BUSY;
		}
	}
	unlock_super(fs);
	
	lock_super(fs);
	for (i = 0; i < EXT2_MAX_GROUP_LOADED; i++) {
		bp = fs->s_block_bitmap[i];
		if (bp) {
			if (!(bp->b_flags & B_DIRTY)) {
				continue;
			} 
			bp->b_flags |= (B_NORELSE|B_BUSY);
			bp->b_flags &= ~B_DIRTY;
			buf_bwrite(bp);
			bp->b_flags &= ~B_BUSY;
		}
	}
	unlock_super(fs);
#endif
	
	/* superblock */
	bp = buf_getblk(mp->um_devvp, (daddr64_t)SBLOCK, SBSIZE, 0, 0, BLK_META);
	lock_super(fs);
	bcopy((caddr_t)es, ((caddr_t)buf_dataptr(bp)+SBOFF), (u_int)sizeof(struct ext2_super_block));
	unlock_super(fs);
	if (waitfor == MNT_WAIT)
		error = buf_bwrite(bp);
	else
		buf_bawrite(bp);

	ext2_trace_return(error);
}

/*
 * Return the root of a filesystem.
 */
static int
ext2_root(mp, vpp, context)
	mount_t mp;
	vnode_t *vpp;
	vfs_context_t context;
{
	evalloc_args_t args = {0};
	vnode_t nvp;
	struct inode *ip;
	int error;
   
	*vpp = NULL;
	args.va_ino = ROOTINO;
	args.va_vctx = context;
    error = VFS_VGET(mp, &args, &nvp, context);
	if (error)
		ext2_trace_return(error);
	ip = VTOI(nvp);
	if (!S_ISDIR(ip->i_mode) || !ip->i_blocks || !ip->i_size) {
		log(LOG_WARNING, "EXT2-fs: root inode is corrupt, please run fsck.\n");
		vnode_put(nvp);
		return (EINVAL);
	}
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

/*
 * Check if vnode represents a disk device
 */
int
vn_isdisk(vp, errp)
	vnode_t vp;
	int *errp;
{
	dev_t rdev;
	
	if (0 == vnode_isblk(vp)) {
		if (errp != NULL)
			*errp = ENOTBLK;
		return (0);
	}
	rdev = vnode_specrdev(vp);
	if (0 == rdev || (major(rdev) >= nblkdev)) {
		if (errp != NULL)
			*errp = ENXIO;
		return (0);
	}

	if (errp != NULL)
		*errp = 0;
	return(1);
}

/*
 * Vfs start routine, a no-op.
 */
/* ARGSUSED */
static int
vfs_stdstart(mp, flags, context)
	mount_t mp;
	int flags;
	vfs_context_t context;
{
	return (0);
}

/*
 * Do operations associated with quotas, not supported
 */
/* ARGSUSED */
static int
vfs_stdquotactl(mp, cmd, uid, arg, segflg, context)
	mount_t mp;
	int cmd;
	uid_t uid;
	caddr_t arg;
	enum uio_seg segflg;
	vfs_context_t context;
{
	return (EOPNOTSUPP);
}

__private_extern__ int dirchk;

static int
ext2_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
	   size_t newlen, vfs_context_t context)
{
	int error = 0, intval;
	
#ifdef DARWIN7
	struct sysctl_req *req;
	struct vfsidctl vc;
	mount_t mp;
	struct nfsmount *nmp;
	struct vfsquery vq;
	
	/*
		* All names at this level are terminal.
		*/
	if(namelen > 1)
		return ENOTDIR;	/* overloaded */
	
	/* common code for "new style" VFS_CTL sysctl, get the mount. */
	switch (name[0]) {
	case VFS_CTL_TIMEO:
	case VFS_CTL_QUERY:
		req = oldp;
		error = SYSCTL_IN(req, &vc, sizeof(vc));
		if (error)
			return (error);
		mp = vfs_getvfs(&vc.vc_fsid);
		if (mp == NULL)
			return (ENOENT);
		nmp = VFSTONFS(mp);
		if (nmp == NULL)
			return (ENOENT);
		bzero(&vq, sizeof(vq));
		VCTLTOREQ(&vc, req);
	}
#endif /* DARWIN7 */

	switch (name[0]) {
		case EXT2_SYSCTL_INT_DIRCHECK:
			if (!oldp && !newp) {
				*oldlenp = sizeof(dirchk);
				return (0);
			}
			if (oldp && *oldlenp < sizeof(dirchk)) {
				*oldlenp = sizeof(dirchk);
				return (ENOMEM);
			}
			if (oldp) {
				*oldlenp = sizeof(dirchk);
				error = copyout(&dirchk, CAST_USER_ADDR_T(oldp), sizeof(dirchk));
				if (error)
					return (error);
			}
			
			if (newp && newlen != sizeof(int))
				return (EINVAL);
			if (newp) {
				error = copyin(CAST_USER_ADDR_T(newp), &intval, sizeof(dirchk));
				if (!error) {
					if (1 == intval || 0 == intval)
						dirchk = intval;
					else
						error = EINVAL;
				}
			}
			return (error);
			
		default:
			return (ENOTSUP);
	}

	return (error);
}

#ifdef obsolete
extern int vfs_opv_numops; /* kernel */
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
#endif // obsolete

/* Kernel entry/exit points */

__private_extern__ struct vnodeopv_desc ext2fs_vnodeop_opv_desc;
__private_extern__ struct vnodeopv_desc ext2fs_specop_opv_desc;
__private_extern__ struct vnodeopv_desc ext2fs_fifoop_opv_desc;

extern struct sysctl_oid sysctl__vfs_e2fs;
extern struct sysctl_oid sysctl__vfs_e2fs_dircheck;
static struct sysctl_oid* e2sysctl_list[] = {
	&sysctl__vfs_e2fs,
	&sysctl__vfs_e2fs_dircheck,
	(struct sysctl_oid *)0
};

// XXX Shoudl this be a ptr
static struct vfsconf ext2_vfsconf;

kern_return_t ext2fs_start (kmod_info_t * ki, void * d) {
#ifdef obsolete
   struct vfsconf	*vfsConf = NULL;
   int funnelState, i;
   kern_return_t kret;
   
   /* Register our module */
   funnelState = thread_funnel_set(kernel_flock, TRUE);
   
   MALLOC(vfsConf, void *, sizeof(struct vfsconf), M_TEMP, M_WAITOK);
	if (NULL == vfsConf) {
      kret = KERN_RESOURCE_SHORTAGE;
      goto funnel_release;
   }
#endif
	struct vfs_fsentry fsc;
	struct vnodeopv_desc* vnops[] =
		{&ext2fs_vnodeop_opv_desc, &ext2fs_specop_opv_desc, &ext2fs_fifoop_opv_desc};
	int kret, i;
	
	bzero(&fsc, sizeof(struct vfs_fsentry));
   
	fsc.vfe_vfsops = &ext2fs_vfsops;
	fsc.vfe_vopcnt = 3;
	fsc.vfe_opvdescs = vnops;
	strncpy(&fsc.vfe_fsname[0], EXT2FS_NAME, MFSNAMELEN);
	fsc.vfe_flags = VFS_TBLTHREADSAFE|VFS_TBLFSNODELOCK|VFS_TBLLOCALVOL;
	kret = vfs_fsadd(&fsc, NULL);
	
#ifdef obsolete
   init_vnodeopv_desc(&ext2fs_vnodeop_opv_desc);
   init_vnodeopv_desc(&ext2fs_specop_opv_desc);
   init_vnodeopv_desc(&ext2fs_fifoop_opv_desc);
   
   kret = vfsconf_add(vfsConf);
#endif
	if (kret) {
		printf ("ext2fs_start: Failed to register with kernel, error = %d\n", kret);
		return (KERN_FAILURE);
	}
	
	/* This is required for vfs_sysctl() to call our handler. */
	sysctl__vfs_e2fs.oid_number = ext2_vfsconf.vfc_typenum;
	/* Register our sysctl's */
	for (i=0; e2sysctl_list[i]; ++i) {
		sysctl_register_oid(e2sysctl_list[i]);
	};

#ifdef obsolete
funnel_release:
	if (vfsConf)
		FREE(vfsConf, M_TEMP);

	thread_funnel_set(kernel_flock, funnelState);
#endif

   if (kret)
      return (KERN_FAILURE);
   
   return (KERN_SUCCESS);
}

kern_return_t ext2fs_stop (kmod_info_t * ki, void * d) {
#ifdef obsolete
   int funnelState, i;
   struct vfsconf *vc;
   
   funnelState = thread_funnel_set(kernel_flock, TRUE);

   /* Don't unload if there are active mounts. Thanks to W. Crooze for pointing this
      problem out. */
   
   /* XXX - Doesn't seem to be a lock for this global - guess the funnel is enough. */
   vc = vfsconf;
   while (vc) {
      if ((NULL != vc->vfc_vfsops) && (0 == strcmp(vc->vfc_name, EXT2FS_NAME)))
         break;
      vc = vc->vfc_next;
   }
   
   if (vc->vfc_refcount > 0) {
      /* There are still mounts active. */
      log(LOG_INFO, "ext2fs_stop: failed to unload kext, mounts still active\n");
      thread_funnel_set(kernel_flock, funnelState);
      ext2_trace_return(KERN_FAILURE);
   }
#endif
	int error, i;
	
	/* Deregister with the kernel */

	/* sysctl's first */
	for (i=0; e2sysctl_list[i]; ++i) ; /* Get Count */
	for (--i; i >= 0; --i) { /* Work backwords */
		assert(NULL != e2sysctl_list[i]);
		sysctl_unregister_oid(e2sysctl_list[i]);
	};

#ifdef obsolete
   vfsconf_del(EXT2FS_NAME);
#endif
	if ((error = vfs_fsremove(&ext2_vfsconf)))
		return (KERN_FAILURE);
	FREE(*ext2fs_vnodeop_opv_desc.opv_desc_vector_p, M_TEMP);
	FREE(*ext2fs_specop_opv_desc.opv_desc_vector_p, M_TEMP);
	FREE(*ext2fs_fifoop_opv_desc.opv_desc_vector_p, M_TEMP);

#ifdef obsolete
	thread_funnel_set(kernel_flock, funnelState);
#endif

   ext2_uninit(NULL);
   ext2_trace_return(KERN_SUCCESS);
}
