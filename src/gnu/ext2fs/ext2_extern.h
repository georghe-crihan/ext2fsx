/*
 *  modified for EXT2FS support in Lites 1.1
 *
 *  Aug 1995, Godmar Back (gback@cs.utah.edu)
 *  University of Utah, Department of Computer Science
 */
/*-
 * Copyright (c) 1991, 1993, 1994
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
 *	@(#)ffs_extern.h	8.3 (Berkeley) 4/16/94
 * $FreeBSD: src/sys/gnu/ext2fs/ext2_extern.h,v 1.29 2002/05/18 22:18:17 iedowse Exp $
 */

#ifndef _SYS_GNU_EXT2FS_EXT2_EXTERN_H_
#define	_SYS_GNU_EXT2FS_EXT2_EXTERN_H_

#include <sys/types.h>

#ifndef __u32
#define __u32 u_int32_t
#endif

struct dx_hash_info;
struct dir_private_info;
struct ext2_inode;
struct ext2_sb_info;
struct ext2_dir_entry_2;
struct indir;
struct inode;
struct mount;
struct vfsconf;
struct vnode;

int	ext2_alloc(struct inode *,
	    ext2_daddr_t, ext2_daddr_t, int, struct ucred *, ext2_daddr_t *);
int	ext2_balloc2(struct inode *,
	    ext2_daddr_t, int, struct ucred *, struct buf **, int, int *);
int	ext2_blkatoff(struct vnode *, off_t, char **, struct buf **);
void	ext2_blkfree(struct inode *, ext2_daddr_t, long);
ext2_daddr_t	ext2_blkpref(struct inode *, ext2_daddr_t, int, ext2_daddr_t *, ext2_daddr_t);
int	ext2_bmap(struct vop_bmap_args *);
int	ext2_bmaparray(struct vnode *, ext2_daddr_t, ext2_daddr_t *, int *, int *);
void	ext2_dirbad(struct inode *ip, doff_t offset, char *how);
void	ext2_ei2i(struct ext2_inode *, struct inode *);
int	ext2_getlbns(struct vnode *, ext2_daddr_t, struct indir *, int *);
void	ext2_i2ei(struct inode *, struct ext2_inode *);
int	ext2_ihashget(dev_t, ino_t, int, struct vnode **);
void	ext2_ihashinit(void);
void	ext2_ihashins(struct inode *);
struct vnode *
	ext2_ihashlookup(dev_t, ino_t);
void	ext2_ihashrem(struct inode *);
void	ext2_ihashuninit(void);
void	ext2_itimes(struct vnode *vp);
int	ext2_reallocblks(struct vop_reallocblks_args *);
int	ext2_reclaim(struct vop_reclaim_args *);
/* void ext2_setblock(struct ext2_sb_info *, u_char *, int32_t); */
int	ext2_truncate(struct vnode *, off_t, int, struct ucred *, struct thread *);
int	ext2_update(struct vnode *, int);
int	ext2_valloc(struct vnode *, int, struct ucred *, struct vnode **);
int	ext2_vfree(struct vnode *, ino_t, int);
int	ext2_vinit(struct mount *, vop_t **, vop_t **, struct vnode **vpp);
int 	ext2_lookup(struct vop_cachedlookup_args *);
int 	ext2_readdir(struct vop_readdir_args *);
void	ext2_print_inode(struct inode *);
int	ext2_direnter(struct inode *, 
		struct vnode *, struct componentname *);
int	ext2_dirremove(struct vnode *, struct componentname *);
int	ext2_dirrewrite(struct inode *,
		struct inode *, struct componentname *);
int	ext2_dirempty(struct inode *, ino_t, struct ucred *);
int	ext2_checkpath(struct inode *, struct inode *, struct ucred *);
struct  ext2_group_desc * get_group_desc(struct mount * , 
		unsigned int , struct buf ** );
int	ext2_group_sparse(int group);
void	ext2_discard_prealloc(struct inode *);
int	ext2_inactive(struct vop_inactive_args *);
int	ext2_new_block(struct mount * mp, unsigned long goal,
	    u_int32_t *prealloc_count, u_int32_t *prealloc_block);
ino_t	ext2_new_inode(const struct inode * dir, int mode);
unsigned long ext2_count_free(struct buf *map, unsigned int numchars);
void	ext2_free_blocks(struct mount *mp, unsigned long block,
	    unsigned long count);
void	ext2_free_inode(struct inode * inode);
void	mark_buffer_dirty(struct buf *bh);

/* ext3_super.c */
extern void __ext3_std_error (struct ext2_sb_info *, const char *, int);
#define ext3_std_error(sb, errno) \
do { \
	if ((errno)) \
		__ext3_std_error((sb), __FUNCTION__, (errno)); \
} while (0)
extern void ext3_warning (struct ext2_sb_info *, const char *, const char *, ...);
extern void ext3_update_dynamic_rev(struct ext2_sb_info *);

/* ext3_dx.c */
extern int ext3_htree_store_dirent(struct vnode *dir_file, __u32 hash,
				    __u32 minor_hash,
				    struct ext2_dir_entry_2 *dirent);
extern void ext3_htree_free_dir_info(struct dir_private_info *p);

/* hash.c */
extern int ext3fs_dirhash(const char *name, int len, struct
			  dx_hash_info *hinfo);

/* Flags to low-level allocation routines. */
#define B_CLRBUF	0x01	/* Request allocated buffer be cleared. */
#define B_SYNC		0x02	/* Do all allocations synchronously. */
#define B_METAONLY	0x04	/* Return indirect block buffer. */
#define B_NOWAIT	0x08	/* do not sleep to await lock */

extern vop_t **ext2_vnodeop_p;
extern vop_t **ext2_specop_p;
extern vop_t **ext2_fifoop_p;

/* Compatibility wrapper for ext2_balloc2 */
static __inline__ int ext2_balloc(struct inode *ip,
	    ext2_daddr_t bn, int size, struct ucred *cred, struct buf **bpp, int flags)
{
   return (ext2_balloc2(ip, bn, size, cred, bpp, flags, NULL));
}

/* Sysctl OID numbers. */
#define EXT2_SYSCTL_INT_DIRCHECK 1

#endif /* !_SYS_GNU_EXT2FS_EXT2_EXTERN_H_ */
