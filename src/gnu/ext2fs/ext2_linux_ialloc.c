/*
 *  modified for Lites 1.1
 *
 *  Aug 1995, Godmar Back (gback@cs.utah.edu)
 *  University of Utah, Department of Computer Science
 *
 * $FreeBSD: src/sys/gnu/ext2fs/ext2_linux_ialloc.c,v 1.18 2002/05/16 19:07:59 iedowse Exp $
 */
/*
 *  linux/fs/ext2/ialloc.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  BSD ufs-inspired inode and directory allocation by 
 *  Stephen Tweedie (sct@dcs.ed.ac.uk), 1993
 */

/*
 * The free inodes are managed by bitmaps.  A file system contains several
 * blocks groups.  Each group contains 1 bitmap block for blocks, 1 bitmap
 * block for inodes, N blocks for the inode table and data blocks.
 *
 * The file system contains group descriptors which are located after the
 * super block.  Each descriptor contains the number of the bitmap block and
 * the free blocks count in the block.  The descriptors are loaded in memory
 * when a file system is mounted (see ext2_read_super).
 */

static const char vwhatid[] __attribute__ ((unused)) =
"@(#) $Id$";

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/mount.h>
#include <sys/vnode.h>

#ifdef APPLE
#include <machine/spl.h>

#include "ext2_apple.h"
#endif /* APPLE */

#include <gnu/ext2fs/inode.h>
#include <gnu/ext2fs/ext2_mount.h>
#include <gnu/ext2fs/ext2_extern.h>
#include <gnu/ext2fs/ext2_fs.h>
#include <gnu/ext2fs/ext2_fs_sb.h>
#include <gnu/ext2fs/fs.h>
#include <ext2_byteorder.h>
#include <sys/stat.h>

#ifdef __i386__
#include <gnu/ext2fs/i386-bitops.h>
#elif  __alpha__
#include <gnu/ext2fs/alpha-bitops.h>
#elif __ppc__
#include <gnu/ext2fs/ppc-bitops.h>
#else
#error Provide a bitops.h file, please!
#endif

/* this is supposed to mark a buffer dirty on ready for delayed writing
 */
void mark_buffer_dirty(struct buf *bh)
{
	int s;

	s = splbio();
	bh->b_flags |= B_DIRTY;
	splx(s);
} 

struct ext2_group_desc * get_group_desc (struct mount * mp,
						unsigned int block_group,
						struct buffer_head ** bh)
{
	struct ext2_sb_info *sb = VFSTOEXT2(mp)->um_e2fs;
	unsigned long group_desc;
	unsigned long desc;
	struct ext2_group_desc * gdp;

	if (block_group >= sb->s_groups_count) {
		panic ("ext2_group_desc: "
			    "block_group >= groups_count - "
			    "block_group = %d, groups_count = %lu",
			    block_group, sb->s_groups_count);
      /* Linux logs the panic msg and returns NULL */
   }

	group_desc = block_group / EXT2_DESC_PER_BLOCK(sb);
	desc = block_group % EXT2_DESC_PER_BLOCK(sb);
	if (!sb->s_group_desc[group_desc]) {
		panic ( "ext2_group_desc:"
			    "Group descriptor not loaded - "
			    "block_group = %d, group_desc = %lu, desc = %lu",
			     block_group, group_desc, desc);
      /* Linux logs the panic msg and returns NULL */
   }
	gdp = (struct ext2_group_desc *) 
		sb->s_group_desc[group_desc]->b_data;
	if (bh)
		*bh = sb->s_group_desc[group_desc];
	return gdp + desc;
}

static void read_inode_bitmap (struct mount * mp,
			       unsigned long block_group,
			       unsigned int bitmap_nr
				#if !EXT2_SB_BITMAP_CACHE
                , struct buffer_head **bhpp
            #endif
					 )
{
	struct ext2_sb_info *sb = VFSTOEXT2(mp)->um_e2fs;
	struct ext2_group_desc * gdp;
	struct buffer_head * bh;
	int	error;

	gdp = get_group_desc (mp, block_group, NULL);
	if ((error = meta_bread (VFSTOEXT2(mp)->um_devvp,
			    fsbtodb(sb, le32_to_cpu(gdp->bg_inode_bitmap)), 
			    sb->s_blocksize,
			    NOCRED, &bh)) != 0)
		panic ( "ext2: read_inode_bitmap:"
			    "Cannot read inode bitmap - "
			    "block_group = %lu, inode_bitmap = %lu",
			    block_group, (unsigned long) le32_to_cpu(gdp->bg_inode_bitmap));
#if EXT2_SB_BITMAP_CACHE
	sb->s_inode_bitmap_number[bitmap_nr] = block_group;
	sb->s_inode_bitmap[bitmap_nr] = bh;
	LCK_BUF(bh)
#else
	*bhpp = bh;
#endif
}

/*
 * load_inode_bitmap loads the inode bitmap for a blocks group
 *
 * It maintains a cache for the last bitmaps loaded.  This cache is managed
 * with a LRU algorithm.
 *
 * Notes:
 * 1/ There is one cache per mounted file system.
 * 2/ If the file system contains less than EXT2_MAX_GROUP_LOADED groups,
 *    this function reads the bitmap without maintaining a LRU cache.
 */
static int load_inode_bitmap (struct mount * mp,
			      unsigned int block_group
				#if !EXT2_SB_BITMAP_CACHE
					, struct buffer_head **bhpp
            #endif
					)
{
	struct ext2_sb_info *sb = VFSTOEXT2(mp)->um_e2fs;
	int i, j;
	unsigned long inode_bitmap_number;
	struct buffer_head * inode_bitmap;

	if (block_group >= sb->s_groups_count)
		panic ("ext2: load_inode_bitmap:"
			    "block_group >= groups_count - "
			    "block_group = %d, groups_count = %lu",
			     block_group, sb->s_groups_count);
	if (sb->s_loaded_inode_bitmaps > 0 &&
	    sb->s_inode_bitmap_number[0] == block_group)
		return 0;
	if (sb->s_groups_count <= EXT2_MAX_GROUP_LOADED) {
		if (sb->s_inode_bitmap[block_group]) {
			if (sb->s_inode_bitmap_number[block_group] != 
				block_group)
				panic ( "ext2: load_inode_bitmap:"
				    "block_group != inode_bitmap_number");
			else
				return block_group;
		} else {
		#if EXT2_SB_BITMAP_CACHE
			read_inode_bitmap (mp, block_group, block_group);
		#else
			read_inode_bitmap (mp, block_group, block_group, bhpp);
		#endif
			return block_group;
		}
	}

	for (i = 0; i < sb->s_loaded_inode_bitmaps &&
		    sb->s_inode_bitmap_number[i] != block_group;
	     i++)
		;
	if (i < sb->s_loaded_inode_bitmaps &&
  	    sb->s_inode_bitmap_number[i] == block_group) {
		inode_bitmap_number = sb->s_inode_bitmap_number[i];
		inode_bitmap = sb->s_inode_bitmap[i];
		for (j = i; j > 0; j--) {
			sb->s_inode_bitmap_number[j] =
				sb->s_inode_bitmap_number[j - 1];
			sb->s_inode_bitmap[j] =
				sb->s_inode_bitmap[j - 1];
		}
		sb->s_inode_bitmap_number[0] = inode_bitmap_number;
		sb->s_inode_bitmap[0] = inode_bitmap;
	} else {
#if EXT2_SB_BITMAP_CACHE
		if (sb->s_loaded_inode_bitmaps < EXT2_MAX_GROUP_LOADED)
			sb->s_loaded_inode_bitmaps++;
		else
			ULCK_BUF(sb->s_inode_bitmap[EXT2_MAX_GROUP_LOADED - 1])
		for (j = sb->s_loaded_inode_bitmaps - 1; j > 0; j--) {
			sb->s_inode_bitmap_number[j] =
				sb->s_inode_bitmap_number[j - 1];
			sb->s_inode_bitmap[j] =
				sb->s_inode_bitmap[j - 1];
		}
		read_inode_bitmap (mp, block_group, 0);
#else
		read_inode_bitmap (mp, block_group, 0, bhpp);
#endif /* EXT2_SB_BITMAP_CACHE */
	}
	return 0;
}


void ext2_free_inode (struct inode * inode)
{
	struct ext2_sb_info * sb;
	struct buffer_head * bh;
	struct buffer_head * bh2;
	unsigned long block_group;
	unsigned long bit;
	int bitmap_nr;
	struct ext2_group_desc * gdp;
	struct ext2_super_block * es;

	if (!inode)
		return;

	if (inode->i_nlink) {
		printf ("ext2_free_inode: inode has nlink=%d\n",
			inode->i_nlink);
		return;
	}

	ext2_debug ("ext2: freeing inode %lu\n", (unsigned long)inode->i_number);

	sb = inode->i_e2fs;
	lock_super (sb);
	if (inode->i_number < EXT2_FIRST_INO ||
	    inode->i_number > le32_to_cpu(sb->s_es->s_inodes_count)) {
		printf ("free_inode reserved inode or nonexistent inode");
		unlock_super (sb);
		return;
	}
	es = sb->s_es;
	block_group = (inode->i_number - 1) / EXT2_INODES_PER_GROUP(sb);
	bit = (inode->i_number - 1) % EXT2_INODES_PER_GROUP(sb);
#if EXT2_SB_BITMAP_CACHE
	bitmap_nr = load_inode_bitmap (ITOV(inode)->v_mount, block_group);
	bh = sb->s_inode_bitmap[bitmap_nr];
#else
	bitmap_nr = load_inode_bitmap (ITOV(inode)->v_mount, block_group, &bh);
#endif
   if (!ext2_clear_bit (bit, bh->b_data))
		printf ( "ext2_free_inode:"
		      "bit already cleared for inode %lu",
		      (unsigned long)inode->i_number);
	else {
		gdp = get_group_desc (ITOV(inode)->v_mount, block_group, &bh2);
		gdp->bg_free_inodes_count = cpu_to_le16(le16_to_cpu(gdp->bg_free_inodes_count) + 1);
		if (S_ISDIR(inode->i_mode)) {
			gdp->bg_used_dirs_count = cpu_to_le16(le16_to_cpu(gdp->bg_used_dirs_count) - 1);
			sb->s_dircount -= 1;
		}
		mark_buffer_dirty(bh2);
		es->s_free_inodes_count = cpu_to_le32(le32_to_cpu(es->s_free_inodes_count) + 1);
	}
#if EXT2_SB_BITMAP_CACHE
	mark_buffer_dirty(bh);
#endif
/*** XXX
	if (sb->s_flags & MS_SYNCHRONOUS) {
		ll_rw_block (WRITE, 1, &bh);
		wait_on_buffer (bh);
	}
***/
	sb->s_dirt = 1;
	unlock_super (sb);
#if !EXT2_SB_BITMAP_CACHE
	if (0 == (ITOV(inode)->v_mount->mnt_flag & MNT_SYNCHRONOUS))
		bdwrite(bh);
	else
		bwrite(bh);
#endif
}

#if linux
/*
 * This function increments the inode version number
 *
 * This may be used one day by the NFS server
 */
static void inc_inode_version (struct inode * inode,
			       struct ext2_group_desc *gdp,
			       int mode)
{
	unsigned long inode_block;
	struct buffer_head * bh;
	struct ext2_inode * raw_inode;

	inode_block = cpu_to_le32(gdp->bg_inode_table) + (((inode->i_number - 1) %
			EXT2_INODES_PER_GROUP(inode->i_sb)) /
			EXT2_INODES_PER_BLOCK(inode->i_sb));
	bh = bread (inode->i_sb->s_dev, inode_block, inode->i_sb->s_blocksize);
	if (!bh) {
		printf ("inc_inode_version Cannot load inode table block - "
			    "inode=%lu, inode_block=%lu\n",
			    inode->i_number, inode_block);
		inode->u.ext2_i.i_version = 1;
		return;
	}
	raw_inode = ((struct ext2_inode *) bh->b_data) +
			(((inode->i_number - 1) %
			EXT2_INODES_PER_GROUP(inode->i_sb)) %
			EXT2_INODES_PER_BLOCK(inode->i_sb));
	raw_inode->i_version++;
	inode->u.ext2_i.i_version = raw_inode->i_version;
	if (0 == (ITOV(inode)->v_mount->mnt_flag & MNT_SYNCHRONOUS))
		bdwrite(bh);
	else
		bwrite(bh);
}

#endif /* linux */

/*
 * There are two policies for allocating an inode.  If the new inode is
 * a directory, then a forward search is made for a block group with both
 * free space and a low directory-to-inode ratio; if that fails, then of
 * the groups with above-average free space, that group with the fewest
 * directories already is chosen.
 *
 * For other inodes, search forward from the parent directory\'s block
 * group to find a free inode.
 */
/*
 * this functino has been reduced to the actual 'find the inode number' part
 */
ino_t ext2_new_inode (const struct inode * dir, int mode)
{
	struct ext2_sb_info * sb;
	struct buffer_head * bh;
	struct buffer_head * bh2;
	int i, j, avefreei;
	int bitmap_nr;
	struct ext2_group_desc * gdp;
	struct ext2_group_desc * tmp;
	struct ext2_super_block * es;

	if (!dir)
		return 0;
	sb = dir->i_e2fs;

	lock_super (sb);
	es = sb->s_es;
repeat:
	gdp = NULL; i=0;

	if (S_ISDIR(mode)) {
		avefreei = le32_to_cpu(es->s_free_inodes_count) /
			sb->s_groups_count;
/* I am not yet convinced that this next bit is necessary.
		i = dir->u.ext2_i.i_block_group;
		for (j = 0; j < sb->u.ext2_sb.s_groups_count; j++) {
			tmp = get_group_desc (sb, i, &bh2);
			if ((le16_to_cpu(tmp->bg_used_dirs_count) << 8) < 
			    le16_to_cpu(tmp->bg_free_inodes_count)) {
				gdp = tmp;
				break;
			}
			else
			i = ++i % sb->u.ext2_sb.s_groups_count;
		}
*/
		if (!gdp) {
			for (j = 0; j < sb->s_groups_count; j++) {
				tmp = get_group_desc(ITOV(dir)->v_mount,j,&bh2);
				if (tmp->bg_free_inodes_count &&
					le16_to_cpu(tmp->bg_free_inodes_count) >= avefreei) {
					if (!gdp || 
					    (le16_to_cpu(tmp->bg_free_blocks_count) >
					     le16_to_cpu(gdp->bg_free_blocks_count))) {
						i = j;
						gdp = tmp;
					}
				}
			}
		}
	}
	else 
	{
		/*
		 * Try to place the inode in its parent directory
		 */
		i = dir->i_block_group;
		tmp = get_group_desc (ITOV(dir)->v_mount, i, &bh2);
		if (tmp->bg_free_inodes_count)
			gdp = tmp;
		else
		{
			/*
			 * Use a quadratic hash to find a group with a
			 * free inode
			 */
			for (j = 1; j < sb->s_groups_count; j <<= 1) {
				i += j;
				if (i >= sb->s_groups_count)
					i -= sb->s_groups_count;
				tmp = get_group_desc(ITOV(dir)->v_mount,i,&bh2);
				if (tmp->bg_free_inodes_count) {
					gdp = tmp;
					break;
				}
			}
		}
		if (!gdp) {
			/*
			 * That failed: try linear search for a free inode
			 */
			i = dir->i_block_group;
			for (j = 0; j < sb->s_groups_count; j++) {
				if (++i >= sb->s_groups_count)
					i = 0;
				tmp = get_group_desc(ITOV(dir)->v_mount,i,&bh2);
				if (tmp->bg_free_inodes_count) {
					gdp = tmp;
					break;
				}
			}
		}
	}

	if (!gdp) {
		unlock_super (sb);
		return 0;
	}
#if EXT2_SB_BITMAP_CACHE
	bitmap_nr = load_inode_bitmap (ITOV(dir)->v_mount, i);
	bh = sb->s_inode_bitmap[bitmap_nr];
#else
	bitmap_nr = load_inode_bitmap (ITOV(dir)->v_mount, i, &bh);
#endif
	if ((j = ext2_find_first_zero_bit ((unsigned long *) bh->b_data,
				      EXT2_INODES_PER_GROUP(sb))) <
	    EXT2_INODES_PER_GROUP(sb)) {
      if (ext2_set_bit (j, bh->b_data)) {
			printf ( "ext2_new_inode:"
				      "bit already set for inode %d", j);
		#if !EXT2_SB_BITMAP_CACHE
			brelse(bh);
		#endif
			goto repeat;
		}
/* Linux now does the following:
		mark_buffer_dirty(bh);
		if (sb->s_flags & MS_SYNCHRONOUS) {
			ll_rw_block (WRITE, 1, &bh);
			wait_on_buffer (bh);
		}
*/
	#if EXT2_SB_BITMAP_CACHE
		mark_buffer_dirty(bh);
	#endif
	} else {
	#if !EXT2_SB_BITMAP_CACHE
		brelse(bh);
	#endif
		if (gdp->bg_free_inodes_count != 0) {
			printf ( "ext2_new_inode:"
				    "Free inodes count corrupted in group %d",
				    i);
			unlock_super (sb);
			return 0;
		}
		goto repeat;
	}
	j += i * EXT2_INODES_PER_GROUP(sb) + 1;
	if (j < EXT2_FIRST_INO || j > le32_to_cpu(es->s_inodes_count)) {
		printf ( "ext2_new_inode:"
			    "reserved inode or inode > inodes count - "
			    "block_group = %d,inode=%d", i, j);
		unlock_super (sb);
	#if !EXT2_SB_BITMAP_CACHE
		brelse(bh);
	#endif
		return 0;
	}
	gdp->bg_free_inodes_count = cpu_to_le16(le16_to_cpu(gdp->bg_free_inodes_count) - 1);
	if (S_ISDIR(mode)) {
		gdp->bg_used_dirs_count = cpu_to_le16(le16_to_cpu(gdp->bg_used_dirs_count) + 1);
		sb->s_dircount += 1;
	}
	mark_buffer_dirty(bh2);
	es->s_free_inodes_count = cpu_to_le32(le32_to_cpu(es->s_free_inodes_count) - 1);
	/* mark_buffer_dirty(sb->u.ext2_sb.s_sbh, 1); */
	sb->s_dirt = 1;
	unlock_super (sb);
#if !EXT2_SB_BITMAP_CACHE
	if (0 == (ITOV(dir)->v_mount->mnt_flag & MNT_SYNCHRONOUS))
		bdwrite(bh);
	else
		bwrite(bh);
#endif
	return j;
}

#ifdef unused
static unsigned long ext2_count_free_inodes (struct mount * mp)
{
#if defined(EXT2FS_DEBUG) && EXT2FS_DEBUG > 1
        struct ext2_sb_info *sb = VFSTOEXT2(mp)->um_e2fs;
	struct ext2_super_block * es;
	unsigned long desc_count, bitmap_count, x;
	int bitmap_nr;
	struct ext2_group_desc * gdp;
	int i;

	lock_super (sb);
	es = sb->s_es;
	desc_count = 0;
	bitmap_count = 0;
	gdp = NULL;
	for (i = 0; i < sb->s_groups_count; i++) {
		gdp = get_group_desc (mp, i, NULL);
		desc_count += le16_to_cpu(gdp->bg_free_inodes_count);
		bitmap_nr = load_inode_bitmap (mp, i);
		x = ext2_count_free (sb->s_inode_bitmap[bitmap_nr],
				     EXT2_INODES_PER_GROUP(sb) / 8);
		ext2_debug ("group %d: stored = %d, counted = %lu\n",
			i, le16_to_cpu(gdp->bg_free_inodes_count), x);
		bitmap_count += x;
	}
	ext2_debug("stored = %lu, computed = %lu, %lu\n",
		le32_to_cpu(es->s_free_inodes_count), desc_count, bitmap_count);
	unlock_super (sb);
	return desc_count;
#else
	return VFSTOEXT2(mp)->um_e2fsb->s_free_inodes_count;
#endif
}
#endif /* unused */

#ifdef LATER
void ext2_check_inodes_bitmap (struct mount * mp)
{
	struct ext2_super_block * es;
	unsigned long desc_count, bitmap_count, x;
	int bitmap_nr;
	struct ext2_group_desc * gdp;
	int i;

	lock_super (sb);
	es = sb->u.ext2_sb.s_es;
	desc_count = 0;
	bitmap_count = 0;
	gdp = NULL;
	for (i = 0; i < sb->u.ext2_sb.s_groups_count; i++) {
		gdp = get_group_desc (sb, i, NULL);
		desc_count += le16_to_cpu(gdp->bg_free_inodes_count);
		bitmap_nr = load_inode_bitmap (sb, i);
		x = ext2_count_free (sb->u.ext2_sb.s_inode_bitmap[bitmap_nr],
				     EXT2_INODES_PER_GROUP(sb) / 8);
		if (le16_to_cpu(gdp->bg_free_inodes_count) != x)
			printf ( "ext2_check_inodes_bitmap:"
				    "Wrong free inodes count in group %d, "
				    "stored = %d, counted = %lu", i,
				    le16_to_cpu(gdp->bg_free_inodes_count), x);
		bitmap_count += x;
	}
	if (le32_to_cpu(es->s_free_inodes_count) != bitmap_count)
		printf ( "ext2_check_inodes_bitmap:"
			    "Wrong free inodes count in super block, "
			    "stored = %lu, counted = %lu",
			    (unsigned long) le32_to_cpu(es->s_free_inodes_count), bitmap_count);
	unlock_super (sb);
}
#endif
