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
/*
 * Copyright (c) 2000-2002 Apple Computer, Inc. All rights reserved.
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

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/attr.h>
#include <sys/stat.h>
#include <sys/vm.h>

#include <gnu/ext2fs/inode.h>
#include "ext2_apple.h"

static int ext2_attrcalcsize(struct attrlist *);
static int ext2_packattrblk(struct attrlist *, struct vnode *, void **, void **);
static unsigned long DerivePermissionSummary(uid_t, gid_t, mode_t,
   struct mount *, struct ucred *, struct proc *);

int
ext2_getattrlist(ap)
	struct vop_getattrlist_args /* {
	struct vnode *a_vp;
	struct attrlist *a_alist
	struct uio *a_uio; // INOUT
	struct ucred *a_cred;
	struct proc *a_p;
	} */ *ap;
{
   struct vnode *vp = ap->a_vp;
   struct attrlist *alist = ap->a_alist;
   int blockSize, fixedSize, attrBufSize, err;
   void *abp, *atp, *vdp;
   
   if ((ATTR_BIT_MAP_COUNT != alist->bitmapcount) ||
         (0 != (alist->commonattr & ~ATTR_CMN_VALIDMASK)) ||
         (0 != (alist->volattr & ~ATTR_VOL_VALIDMASK)) /* ||
         (0 != (alist->dirattr & ~ATTR_DIR_VALIDMASK)) ||
         (0 != (alist->fileattr & ~ATTR_FILE_VALIDMASK)) ||
         (0 != (alist->forkattr & ~ATTR_FORK_VALIDMASK))
         */) {
      return (ENOTSUP);
   }
   
   /* 
	 * Requesting volume information requires setting the ATTR_VOL_INFO bit and
	 * volume info requests are mutually exclusive with all other info requests.
    * We only support VOL attributes.
	 */
   if ((0 == alist->volattr) || (0 == (alist->volattr & ATTR_VOL_INFO)) ||
         (0 != alist->dirattr) || (0 != alist->fileattr) || (0 != alist->forkattr)) {
      return (ENOTSUP);
	}
   
   VI_LOCK(vp);
   if (0 == (vp->v_flag & VROOT)) {
      VI_UNLOCK(vp);
      return (EINVAL);
   }
   VI_UNLOCK(vp);
   
   fixedSize = ext2_attrcalcsize(alist);
   blockSize = fixedSize + sizeof(u_long) /* length longword */;
   
   if (alist->volattr & ATTR_VOL_MOUNTPOINT) blockSize += PATH_MAX;
   if (alist->volattr & ATTR_VOL_NAME) blockSize += NAME_MAX;
   
   attrBufSize = MIN(ap->a_uio->uio_resid, blockSize);
   abp = _MALLOC(attrBufSize, M_TEMP, 0);
   if (!abp)
      return (ENOMEM);
      
   atp = abp;
   *((u_long *)atp) = 0; /* Set buffer length in case of errors */
   ++((u_long *)atp); /* Reserve space for length field */
   vdp = ((char *)atp) + fixedSize; /* Point to variable-length storage */

   err = ext2_packattrblk(alist, vp, &atp, &vdp);
   if (0 != err)
      goto exit;
      
   /* Store length of fixed + var block */
   *((u_long *)abp) = ((char*)vdp - (char*)abp);
   /* Don't copy out more data than was generated */
   attrBufSize = MIN(attrBufSize, (char*)vdp - (char*)abp);
   
   err = uiomove((caddr_t)abp, attrBufSize, ap->a_uio);

exit:
   FREE(abp, M_TEMP);

   return (err);

}

static int ext2_attrcalcsize(struct attrlist *attrlist)
{
	int size;
	attrgroup_t a;
	
#if ((ATTR_CMN_NAME			| ATTR_CMN_DEVID			| ATTR_CMN_FSID 			| ATTR_CMN_OBJTYPE 		| \
      ATTR_CMN_OBJTAG		| ATTR_CMN_OBJID			| ATTR_CMN_OBJPERMANENTID	| ATTR_CMN_PAROBJID		| \
      ATTR_CMN_SCRIPT		| ATTR_CMN_CRTIME			| ATTR_CMN_MODTIME			| ATTR_CMN_CHGTIME		| \
      ATTR_CMN_ACCTIME		| ATTR_CMN_BKUPTIME			| ATTR_CMN_FNDRINFO			| ATTR_CMN_OWNERID		| \
      ATTR_CMN_GRPID		| ATTR_CMN_ACCESSMASK		| ATTR_CMN_NAMEDATTRCOUNT	| ATTR_CMN_NAMEDATTRLIST| \
      ATTR_CMN_FLAGS		| ATTR_CMN_USERACCESS) != ATTR_CMN_VALIDMASK)
#error AttributeBlockSize: Missing bits in common mask computation!
#endif
	assert((attrlist->commonattr & ~ATTR_CMN_VALIDMASK) == 0);

#if ((ATTR_VOL_FSTYPE		| ATTR_VOL_SIGNATURE		| ATTR_VOL_SIZE				| ATTR_VOL_SPACEFREE 	| \
      ATTR_VOL_SPACEAVAIL	| ATTR_VOL_MINALLOCATION	| ATTR_VOL_ALLOCATIONCLUMP	| ATTR_VOL_IOBLOCKSIZE	| \
      ATTR_VOL_OBJCOUNT		| ATTR_VOL_FILECOUNT		| ATTR_VOL_DIRCOUNT			| ATTR_VOL_MAXOBJCOUNT	| \
      ATTR_VOL_MOUNTPOINT	| ATTR_VOL_NAME				| ATTR_VOL_MOUNTFLAGS		| ATTR_VOL_INFO 		| \
      ATTR_VOL_MOUNTEDDEVICE| ATTR_VOL_ENCODINGSUSED	| ATTR_VOL_CAPABILITIES		| ATTR_VOL_ATTRIBUTES) != ATTR_VOL_VALIDMASK)
#error AttributeBlockSize: Missing bits in volume mask computation!
#endif
	assert((attrlist->volattr & ~ATTR_VOL_VALIDMASK) == 0);

#if ((ATTR_DIR_LINKCOUNT | ATTR_DIR_ENTRYCOUNT | ATTR_DIR_MOUNTSTATUS) != ATTR_DIR_VALIDMASK)
#error AttributeBlockSize: Missing bits in directory mask computation!
#endif
	assert((attrlist->dirattr & ~ATTR_DIR_VALIDMASK) == 0);
#if ((ATTR_FILE_LINKCOUNT	| ATTR_FILE_TOTALSIZE		| ATTR_FILE_ALLOCSIZE 		| ATTR_FILE_IOBLOCKSIZE 	| \
      ATTR_FILE_CLUMPSIZE	| ATTR_FILE_DEVTYPE			| ATTR_FILE_FILETYPE		| ATTR_FILE_FORKCOUNT		| \
      ATTR_FILE_FORKLIST	| ATTR_FILE_DATALENGTH		| ATTR_FILE_DATAALLOCSIZE	| ATTR_FILE_DATAEXTENTS		| \
      ATTR_FILE_RSRCLENGTH	| ATTR_FILE_RSRCALLOCSIZE	| ATTR_FILE_RSRCEXTENTS) != ATTR_FILE_VALIDMASK)
#error AttributeBlockSize: Missing bits in file mask computation!
#endif
	assert((attrlist->fileattr & ~ATTR_FILE_VALIDMASK) == 0);

#if ((ATTR_FORK_TOTALSIZE | ATTR_FORK_ALLOCSIZE) != ATTR_FORK_VALIDMASK)
#error AttributeBlockSize: Missing bits in fork mask computation!
#endif
	assert((attrlist->forkattr & ~ATTR_FORK_VALIDMASK) == 0);

	size = 0;
	
	if ((a = attrlist->commonattr) != 0) {
        if (a & ATTR_CMN_NAME) size += sizeof(struct attrreference);
		if (a & ATTR_CMN_DEVID) size += sizeof(dev_t);
		if (a & ATTR_CMN_FSID) size += sizeof(fsid_t);
		if (a & ATTR_CMN_OBJTYPE) size += sizeof(fsobj_type_t);
		if (a & ATTR_CMN_OBJTAG) size += sizeof(fsobj_tag_t);
		if (a & ATTR_CMN_OBJID) size += sizeof(fsobj_id_t);
        if (a & ATTR_CMN_OBJPERMANENTID) size += sizeof(fsobj_id_t);
		if (a & ATTR_CMN_PAROBJID) size += sizeof(fsobj_id_t);
		if (a & ATTR_CMN_SCRIPT) size += sizeof(text_encoding_t);
		if (a & ATTR_CMN_CRTIME) size += sizeof(struct timespec);
		if (a & ATTR_CMN_MODTIME) size += sizeof(struct timespec);
		if (a & ATTR_CMN_CHGTIME) size += sizeof(struct timespec);
		if (a & ATTR_CMN_ACCTIME) size += sizeof(struct timespec);
		if (a & ATTR_CMN_BKUPTIME) size += sizeof(struct timespec);
		if (a & ATTR_CMN_FNDRINFO) size += 32 * sizeof(u_int8_t);
		if (a & ATTR_CMN_OWNERID) size += sizeof(uid_t);
		if (a & ATTR_CMN_GRPID) size += sizeof(gid_t);
		if (a & ATTR_CMN_ACCESSMASK) size += sizeof(u_long);
		if (a & ATTR_CMN_NAMEDATTRCOUNT) size += sizeof(u_long);
		if (a & ATTR_CMN_NAMEDATTRLIST) size += sizeof(struct attrreference);
		if (a & ATTR_CMN_FLAGS) size += sizeof(u_long);
		if (a & ATTR_CMN_USERACCESS) size += sizeof(u_long);
	};
	if ((a = attrlist->volattr) != 0) {
		if (a & ATTR_VOL_FSTYPE) size += sizeof(u_long);
		if (a & ATTR_VOL_SIGNATURE) size += sizeof(u_long);
		if (a & ATTR_VOL_SIZE) size += sizeof(off_t);
		if (a & ATTR_VOL_SPACEFREE) size += sizeof(off_t);
		if (a & ATTR_VOL_SPACEAVAIL) size += sizeof(off_t);
		if (a & ATTR_VOL_MINALLOCATION) size += sizeof(off_t);
		if (a & ATTR_VOL_ALLOCATIONCLUMP) size += sizeof(off_t);
		if (a & ATTR_VOL_IOBLOCKSIZE) size += sizeof(size_t);
		if (a & ATTR_VOL_OBJCOUNT) size += sizeof(u_long);
		if (a & ATTR_VOL_FILECOUNT) size += sizeof(u_long);
		if (a & ATTR_VOL_DIRCOUNT) size += sizeof(u_long);
		if (a & ATTR_VOL_MAXOBJCOUNT) size += sizeof(u_long);
		if (a & ATTR_VOL_MOUNTPOINT) size += sizeof(struct attrreference);
        if (a & ATTR_VOL_NAME) size += sizeof(struct attrreference);
        if (a & ATTR_VOL_MOUNTFLAGS) size += sizeof(u_long);
        if (a & ATTR_VOL_MOUNTEDDEVICE) size += sizeof(struct attrreference);
        if (a & ATTR_VOL_ENCODINGSUSED) size += sizeof(unsigned long long);
        if (a & ATTR_VOL_CAPABILITIES) size += sizeof(vol_capabilities_attr_t);
        if (a & ATTR_VOL_ATTRIBUTES) size += sizeof(vol_attributes_attr_t);
	};
	if ((a = attrlist->dirattr) != 0) {
		if (a & ATTR_DIR_LINKCOUNT) size += sizeof(u_long);
		if (a & ATTR_DIR_ENTRYCOUNT) size += sizeof(u_long);
		if (a & ATTR_DIR_MOUNTSTATUS) size += sizeof(u_long);
	};
	if ((a = attrlist->fileattr) != 0) {
		if (a & ATTR_FILE_LINKCOUNT) size += sizeof(u_long);
		if (a & ATTR_FILE_TOTALSIZE) size += sizeof(off_t);
		if (a & ATTR_FILE_ALLOCSIZE) size += sizeof(off_t);
		if (a & ATTR_FILE_IOBLOCKSIZE) size += sizeof(size_t);
		if (a & ATTR_FILE_CLUMPSIZE) size += sizeof(off_t);
		if (a & ATTR_FILE_DEVTYPE) size += sizeof(u_long);
		if (a & ATTR_FILE_FILETYPE) size += sizeof(u_long);
		if (a & ATTR_FILE_FORKCOUNT) size += sizeof(u_long);
		if (a & ATTR_FILE_FORKLIST) size += sizeof(struct attrreference);
		if (a & ATTR_FILE_DATALENGTH) size += sizeof(off_t);
		if (a & ATTR_FILE_DATAALLOCSIZE) size += sizeof(off_t);
		if (a & ATTR_FILE_DATAEXTENTS) size += sizeof(extentrecord);
		if (a & ATTR_FILE_RSRCLENGTH) size += sizeof(off_t);
		if (a & ATTR_FILE_RSRCALLOCSIZE) size += sizeof(off_t);
		if (a & ATTR_FILE_RSRCEXTENTS) size += sizeof(extentrecord);
	};
	if ((a = attrlist->forkattr) != 0) {
		if (a & ATTR_FORK_TOTALSIZE) size += sizeof(off_t);
		if (a & ATTR_FORK_ALLOCSIZE) size += sizeof(off_t);
	};

    return size;
}

static int
ext2_packvolattr (struct attrlist *alist,
			 struct inode *ip,	/* root directory */
			 void **attrbufptrptr,
			 void **varbufptrptr)
{
   struct proc *p;
   void *attrbufptr;
   void *varbufptr;
	struct ext2_sb_info *fs;
	struct mount *mp;
	attrgroup_t a;
	u_long attrlength;
   int err = 0;
   struct timespec ts;
	
	attrbufptr = *attrbufptrptr;
	varbufptr = *varbufptrptr;
	mp = ip->i_vnode->v_mount;
   fs = ip->i_e2fs;
   
   p = current_proc();

   if ((a = alist->commonattr) != 0) {
      
      if (a & ATTR_CMN_NAME) {
         attrlength = 0;
         
         ((struct attrreference *)attrbufptr)->attr_dataoffset = (u_int8_t *)varbufptr - (u_int8_t *)attrbufptr;
         ((struct attrreference *)attrbufptr)->attr_length = attrlength;
         /* copy name */
         
         /* Advance beyond the space just allocated and round up to the next 4-byte boundary: */
         (u_int8_t *)varbufptr += attrlength + ((4 - (attrlength & 3)) & 3);
         ++((struct attrreference *)attrbufptr);
      };
		if (a & ATTR_CMN_DEVID) *((dev_t *)attrbufptr)++ = 0;
		if (a & ATTR_CMN_FSID) *((fsid_t *)attrbufptr)++ =  mp->mnt_stat.f_fsid;
		if (a & ATTR_CMN_OBJTYPE) *((fsobj_type_t *)attrbufptr)++ = 0;
		if (a & ATTR_CMN_OBJTAG) *((fsobj_tag_t *)attrbufptr)++ = VT_OTHER;
		if (a & ATTR_CMN_OBJID)	{
			((fsobj_id_t *)attrbufptr)->fid_objno = 0;
			((fsobj_id_t *)attrbufptr)->fid_generation = 0;
			++((fsobj_id_t *)attrbufptr);
		};
      if (a & ATTR_CMN_OBJPERMANENTID) {
         ((fsobj_id_t *)attrbufptr)->fid_objno = 0;
         ((fsobj_id_t *)attrbufptr)->fid_generation = 0;
         ++((fsobj_id_t *)attrbufptr);
      };
		if (a & ATTR_CMN_PAROBJID) {
            ((fsobj_id_t *)attrbufptr)->fid_objno = 0;
			((fsobj_id_t *)attrbufptr)->fid_generation = 0;
			++((fsobj_id_t *)attrbufptr);
		};
      
      if (a & ATTR_CMN_SCRIPT) *((text_encoding_t *)attrbufptr)++ = 0;
      
      ts.tv_sec = ip->i_ctime; ts.tv_nsec = ip->i_ctimensec;
		if (a & ATTR_CMN_CRTIME) *((struct timespec *)attrbufptr)++ = ts;
      ts.tv_sec = ip->i_mtime; ts.tv_nsec = ip->i_mtimensec;
		if (a & ATTR_CMN_MODTIME) *((struct timespec *)attrbufptr)++ = ts;
		if (a & ATTR_CMN_CHGTIME) *((struct timespec *)attrbufptr)++ = ts;
		ts.tv_sec = ip->i_atime; ts.tv_nsec = ip->i_atimensec;
      if (a & ATTR_CMN_ACCTIME) *((struct timespec *)attrbufptr)++ = ts;
		if (a & ATTR_CMN_BKUPTIME) {
			((struct timespec *)attrbufptr)->tv_sec = 0;
			((struct timespec *)attrbufptr)->tv_nsec = 0;
			++((struct timespec *)attrbufptr);
		};
		if (a & ATTR_CMN_FNDRINFO) {
            bzero (attrbufptr, 32 * sizeof(u_int8_t));
            (u_int8_t *)attrbufptr += 32 * sizeof(u_int8_t);
		};
		if (a & ATTR_CMN_OWNERID) *((uid_t *)attrbufptr)++ = ip->i_uid;
		if (a & ATTR_CMN_GRPID) *((gid_t *)attrbufptr)++ = ip->i_gid;
		if (a & ATTR_CMN_ACCESSMASK) *((u_long *)attrbufptr)++ = (u_long)ip->i_mode;
		if (a & ATTR_CMN_FLAGS) *((u_long *)attrbufptr)++ = 0;
		if (a & ATTR_CMN_USERACCESS) {
         *((u_long *)attrbufptr)++ = DerivePermissionSummary(ip->i_uid,
            ip->i_gid, ip->i_mode, mp, p->p_ucred, p);
      }
	}
	
	if ((a = alist->volattr) != 0) {
      struct statfs sb;
      
      if (0 != (err = mp->mnt_op->vfs_statfs(mp, &sb, p)))
         goto exit;

		if (a & ATTR_VOL_FSTYPE) *((u_long *)attrbufptr)++ = (u_long)mp->mnt_vfc->vfc_typenum;
		if (a & ATTR_VOL_SIGNATURE) *((u_long *)attrbufptr)++ = (u_long)sb.f_type;
      if (a & ATTR_VOL_SIZE) *((off_t *)attrbufptr)++ = (off_t)sb.f_blocks * sb.f_bsize;
      if (a & ATTR_VOL_SPACEFREE) *((off_t *)attrbufptr)++ = (off_t)sb.f_bfree * sb.f_bsize;
      if (a & ATTR_VOL_SPACEAVAIL) *((off_t *)attrbufptr)++ = (off_t)sb.f_bavail * sb.f_bsize;
      if (a & ATTR_VOL_MINALLOCATION) *((off_t *)attrbufptr)++ = sb.f_bsize;
		if (a & ATTR_VOL_ALLOCATIONCLUMP) *((off_t *)attrbufptr)++ = sb.f_bsize;
      if (a & ATTR_VOL_IOBLOCKSIZE) *((size_t *)attrbufptr)++ = sb.f_iosize;
		if (a & ATTR_VOL_OBJCOUNT) *((u_long *)attrbufptr)++ = sb.f_files - sb.f_ffree;
		if (a & ATTR_VOL_FILECOUNT) *((u_long *)attrbufptr)++ = 0;
		if (a & ATTR_VOL_DIRCOUNT) *((u_long *)attrbufptr)++ = 0;
		if (a & ATTR_VOL_MAXOBJCOUNT) *((u_long *)attrbufptr)++ = sb.f_files;
      if (a & ATTR_VOL_NAME) {
         attrlength = 0;
         ((struct attrreference *)attrbufptr)->attr_dataoffset = (u_int8_t *)varbufptr - (u_int8_t *)attrbufptr;
         ((struct attrreference *)attrbufptr)->attr_length = attrlength;
         /* Copy vol name */

         /* Advance beyond the space just allocated and round up to the next 4-byte boundary: */
         (u_int8_t *)varbufptr += attrlength + ((4 - (attrlength & 3)) & 3);
         ++((struct attrreference *)attrbufptr);
      };
		if (a & ATTR_VOL_MOUNTFLAGS) *((u_long *)attrbufptr)++ = (u_long)mp->mnt_flag;
        if (a & ATTR_VOL_MOUNTEDDEVICE) {
            ((struct attrreference *)attrbufptr)->attr_dataoffset = (u_int8_t *)varbufptr - (u_int8_t *)attrbufptr;
            ((struct attrreference *)attrbufptr)->attr_length = strlen(mp->mnt_stat.f_mntfromname) + 1;
			attrlength = ((struct attrreference *)attrbufptr)->attr_length;
			attrlength = attrlength + ((4 - (attrlength & 3)) & 3);		/* round up to the next 4-byte boundary: */
			(void) bcopy(mp->mnt_stat.f_mntfromname, varbufptr, attrlength);
			
			/* Advance beyond the space just allocated: */
            (u_int8_t *)varbufptr += attrlength;
            ++((struct attrreference *)attrbufptr);
        };
        if (a & ATTR_VOL_ENCODINGSUSED) *((unsigned long long *)attrbufptr)++ = (unsigned long long)0;
        if (a & ATTR_VOL_CAPABILITIES) {
        	((vol_capabilities_attr_t *)attrbufptr)->capabilities[VOL_CAPABILITIES_FORMAT] =
            VOL_CAP_FMT_SYMBOLICLINKS|VOL_CAP_FMT_HARDLINKS;
        	((vol_capabilities_attr_t *)attrbufptr)->capabilities[VOL_CAPABILITIES_INTERFACES] = 0 /*VOL_CAP_INT_NFSEXPORT*/;
        	((vol_capabilities_attr_t *)attrbufptr)->capabilities[VOL_CAPABILITIES_RESERVED1] = 0;
        	((vol_capabilities_attr_t *)attrbufptr)->capabilities[VOL_CAPABILITIES_RESERVED2] = 0;

        	((vol_capabilities_attr_t *)attrbufptr)->valid[VOL_CAPABILITIES_FORMAT]
            = VOL_CAP_FMT_SYMBOLICLINKS|VOL_CAP_FMT_HARDLINKS;
        	((vol_capabilities_attr_t *)attrbufptr)->valid[VOL_CAPABILITIES_INTERFACES] = 0 /*VOL_CAP_INT_NFSEXPORT*/;
        	((vol_capabilities_attr_t *)attrbufptr)->valid[VOL_CAPABILITIES_RESERVED1] = 0;
        	((vol_capabilities_attr_t *)attrbufptr)->valid[VOL_CAPABILITIES_RESERVED2] = 0;

            ++((vol_capabilities_attr_t *)attrbufptr);
        };
        if (a & ATTR_VOL_ATTRIBUTES) {
        	((vol_attributes_attr_t *)attrbufptr)->validattr.commonattr = ATTR_CMN_VALIDMASK;
        	((vol_attributes_attr_t *)attrbufptr)->validattr.volattr = ATTR_VOL_VALIDMASK;
        	((vol_attributes_attr_t *)attrbufptr)->validattr.dirattr = ATTR_DIR_VALIDMASK;
        	((vol_attributes_attr_t *)attrbufptr)->validattr.fileattr = ATTR_FILE_VALIDMASK;
        	((vol_attributes_attr_t *)attrbufptr)->validattr.forkattr = ATTR_FORK_VALIDMASK;

        	((vol_attributes_attr_t *)attrbufptr)->nativeattr.commonattr = ATTR_CMN_VALIDMASK;
        	((vol_attributes_attr_t *)attrbufptr)->nativeattr.volattr = ATTR_VOL_VALIDMASK;
        	((vol_attributes_attr_t *)attrbufptr)->nativeattr.dirattr = ATTR_DIR_VALIDMASK;
        	((vol_attributes_attr_t *)attrbufptr)->nativeattr.fileattr = ATTR_FILE_VALIDMASK;
        	((vol_attributes_attr_t *)attrbufptr)->nativeattr.forkattr = ATTR_FORK_VALIDMASK;

            ++((vol_attributes_attr_t *)attrbufptr);
        }
	}

exit:
	*attrbufptrptr = attrbufptr;
	*varbufptrptr = varbufptr;
   
   return (err);
}

int
ext2_packattrblk(struct attrlist *alist,
			struct vnode *vp,
			void **attrbufptrptr,
			void **varbufptrptr)
{
	struct inode *ip = VTOI(vp);

	if (alist->volattr != 0) {
		return (ext2_packvolattr (alist, ip, attrbufptrptr, varbufptrptr));
   }
   
   return (EINVAL);
}

/* Cribbed from hfs/hfs_attrlist.c */
unsigned long
DerivePermissionSummary(uid_t obj_uid, gid_t obj_gid, mode_t obj_mode,
			struct mount *mp, struct ucred *cred, struct proc *p)
{
	register gid_t *gp;
	unsigned long permissions;
	int i;

	if (obj_uid == UNKNOWNUID)
		obj_uid = console_user;

	/* User id 0 (root) always gets access. */
	if (cred->cr_uid == 0) {
		permissions = R_OK | W_OK | X_OK;
		goto Exit;
	};

	/* Otherwise, check the owner. */
	if (obj_uid == cred->cr_uid) {
		permissions = ((unsigned long)obj_mode & S_IRWXU) >> 6;
		goto Exit;
	}

	/* Otherwise, check the groups. */
	if (! (mp->mnt_flag & MNT_UNKNOWNPERMISSIONS)) {
		for (i = 0, gp = cred->cr_groups; i < cred->cr_ngroups; i++, gp++) {
			if (obj_gid == *gp) {
				permissions = ((unsigned long)obj_mode & S_IRWXG) >> 3;
				goto Exit;
			}
		}
	}

	/* Otherwise, settle for 'others' access. */
	permissions = (unsigned long)obj_mode & S_IRWXO;

Exit:
	return (permissions);    
}