/*
 * Copyright (c) 1995 The University of Utah and
 * the Computer Systems Laboratory at the University of Utah (CSL).
 * All rights reserved.
 *
 * Permission to use, copy, modify and distribute this software is hereby
 * granted provided that (1) source code retains these copyright, permission,
 * and disclaimer notices, and (2) redistributions including binaries
 * reproduce the notices in supporting documentation, and (3) all advertising
 * materials mentioning features or use of this software display the following
 * acknowledgement: ``This product includes software developed by the
 * Computer Systems Laboratory at the University of Utah.''
 *
 * THE UNIVERSITY OF UTAH AND CSL ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS
 * IS" CONDITION.  THE UNIVERSITY OF UTAH AND CSL DISCLAIM ANY LIABILITY OF
 * ANY KIND FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * CSL requests users of this software to return to csl-dist@cs.utah.edu any
 * improvements that they make and grant CSL redistribution rights.
 *
 *      Utah $Hdr$
 * $FreeBSD: src/sys/gnu/ext2fs/ext2_inode_cnv.c,v 1.12 2002/05/16 19:07:59 iedowse Exp $
 */

/*
 * routines to convert on disk ext2 inodes into inodes and back
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/stat.h>
#include <sys/vnode.h>

#ifdef APPLE
#include "ext2_apple.h"
#endif

#include <gnu/ext2fs/inode.h>
#include <gnu/ext2fs/ext2_fs.h>
#include <gnu/ext2fs/ext2_extern.h>
#include <ext2_byteorder.h>

void
ext2_print_inode( in )
	struct inode *in;
{
	int i;

	printf( "Inode: %5d", in->i_number);
	printf( /* "Inode: %5d" */
		" Type: %10s Mode: 0x%o Flags: 0x%x  Version: %d\n",
		"n/a", in->i_mode, in->i_flags, in->i_gen);
	printf( "User: %5lu Group: %5lu  Size: %lu\n",
		(unsigned long)in->i_uid, (unsigned long)in->i_gid,
		(unsigned long)in->i_size);
	printf( "Links: %3d Blockcount: %d\n",
		in->i_nlink, in->i_blocks);
	printf( "ctime: 0x%x", in->i_ctime);
	printf( "atime: 0x%x", in->i_atime);
	printf( "mtime: 0x%x", in->i_mtime);
	printf( "BLOCKS: ");
	for(i=0; i < (in->i_blocks <= 24 ? ((in->i_blocks+1)/2): 12); i++)
		printf("%d ", in->i_db[i]);
	printf("\n");
}

/*
 *	raw ext2 inode to inode
 */
void
ext2_ei2i(ei, ip)
	struct ext2_inode *ei;
	struct inode *ip;
{
        int i;

	ip->i_nlink = le16_to_cpu(ei->i_links_count);
	/* Godmar thinks - if the link count is zero, then the inode is
	   unused - according to ext2 standards. Ufs marks this fact
	   by setting i_mode to zero - why ?
	   I can see that this might lead to problems in an undelete.
	*/
	ip->i_mode = le16_to_cpu(ei->i_links_count) ? le16_to_cpu(ei->i_mode) : 0;
	ip->i_size = le32_to_cpu(ei->i_size);
	ip->i_atime = le32_to_cpu(ei->i_atime);
	ip->i_mtime = le32_to_cpu(ei->i_mtime);
	ip->i_ctime = le32_to_cpu(ei->i_ctime);
	ip->i_flags = 0;
	ip->i_flags |= (le32_to_cpu(ei->i_flags) & EXT2_APPEND_FL) ? APPEND : 0;
	ip->i_flags |= (le32_to_cpu(ei->i_flags) & EXT2_IMMUTABLE_FL) ? IMMUTABLE : 0;
	ip->i_blocks = le32_to_cpu(ei->i_blocks);
	ip->i_gen = le32_to_cpu(ei->i_generation);
	ip->i_uid = (u_int32_t)le16_to_cpu(ei->i_uid);
	ip->i_gid = (u_int32_t)le16_to_cpu(ei->i_gid);
   /* if(!(test_opt (ip->i_sb, NO_UID32))) { Always use 32 bit uid's */
		ip->i_uid |= le16_to_cpu(ei->i_uid_high) << 16;
		ip->i_gid |= le16_to_cpu(ei->i_gid_high) << 16;
	/*}*/
	/* XXX use memcpy */
   
   /* Linux leaves the block #'s in LE order*/
	for(i = 0; i < NDADDR; i++)
		ip->i_db[i] = le32_to_cpu(ei->i_block[i]);
	for(i = 0; i < NIADDR; i++)
		ip->i_ib[i] = le32_to_cpu(ei->i_block[EXT2_NDIR_BLOCKS + i]);
}

#define low_16_bits(x)	((x) & 0xFFFF)
#define high_16_bits(x)	(((x) & 0xFFFF0000) >> 16)

/*
 *	inode to raw ext2 inode
 */
void
ext2_i2ei(ip, ei)
	struct inode *ip;
	struct ext2_inode *ei;
{
	int i;

	ei->i_mode = cpu_to_le16(ip->i_mode);
        ei->i_links_count = cpu_to_le16(ip->i_nlink);
	/* 
	   Godmar thinks: if dtime is nonzero, ext2 says this inode
	   has been deleted, this would correspond to a zero link count
	 */
	ei->i_dtime = ip->i_nlink ? 0 : cpu_to_le32(ip->i_mtime);
	ei->i_size = cpu_to_le32(ip->i_size);
	ei->i_atime = cpu_to_le32(ip->i_atime);
	ei->i_mtime = cpu_to_le32(ip->i_mtime);
	ei->i_ctime = cpu_to_le32(ip->i_ctime);
	/*ei->i_flags = ip->i_flags;*/
	ei->i_flags = 0;
	ei->i_flags |= cpu_to_le32((ip->i_flags & APPEND) ? EXT2_APPEND_FL: 0);
	ei->i_flags |= cpu_to_le32((ip->i_flags & IMMUTABLE) ? EXT2_IMMUTABLE_FL: 0);
	ei->i_blocks = cpu_to_le32(ip->i_blocks);
	ei->i_generation = cpu_to_le32(ip->i_gen);
	ei->i_uid = cpu_to_le32(ip->i_uid);
	ei->i_gid = cpu_to_le32(ip->i_gid);
   /*if(!(test_opt(inode->i_sb, NO_UID32))) { Always use 32 bit uid's */
		ei->i_uid_low = cpu_to_le16(low_16_bits(ip->i_uid));
		ei->i_gid_low = cpu_to_le16(low_16_bits(ip->i_gid));
/*
 * Fix up interoperability with old kernels. Otherwise, old inodes get
 * re-used with the upper 16 bits of the uid/gid intact
 */
		if(!ei->i_dtime) {
			ei->i_uid_high = cpu_to_le16(high_16_bits(ip->i_uid));
			ei->i_gid_high = cpu_to_le16(high_16_bits(ip->i_gid));
		} else {
			ei->i_uid_high = 0;
			ei->i_gid_high = 0;
		}
	/*} else {
		raw_inode->i_uid_low = cpu_to_le16(fs_high2lowuid(inode->i_uid));
		raw_inode->i_gid_low = cpu_to_le16(fs_high2lowgid(inode->i_gid));
		raw_inode->i_uid_high = 0;
		raw_inode->i_gid_high = 0;
	}*/
   
	/* XXX use memcpy */
	for(i = 0; i < NDADDR; i++)
		ei->i_block[i] = cpu_to_le32(ip->i_db[i]);
	for(i = 0; i < NIADDR; i++)
		ei->i_block[EXT2_NDIR_BLOCKS + i] = cpu_to_le32(ip->i_ib[i]);
}
