/*
 * getsize.c --- get the size of a partition.
 * 
 * Copyright (C) 1995, 1995 Theodore Ts'o.
 * Copyright (C) 2003 VMware, Inc.
 *
 * Windows version of ext2fs_get_device_size by Chris Li, VMware.
 * 
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE

#include <stdio.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_ERRNO_H
#include <errno.h>
#endif
#include <fcntl.h>
#ifdef HAVE_LINUX_FD_H
#include <sys/ioctl.h>
#include <linux/fd.h>
#endif
#ifdef HAVE_SYS_DISKLABEL_H
#include <sys/param.h> /* for __FreeBSD_version */
#include <sys/ioctl.h>
#include <sys/disklabel.h>
#endif /* HAVE_SYS_DISKLABEL_H */
#ifdef HAVE_SYS_DISK_H
#include <sys/queue.h> /* for LIST_HEAD */
#include <sys/disk.h>
#endif /* HAVE_SYS_DISK_H */

#if defined(__linux__) && defined(_IO) && !defined(BLKGETSIZE)
#define BLKGETSIZE _IO(0x12,96)	/* return device size */
#endif

#ifdef APPLE_DARWIN
#include <sys/ioctl.h>
#include <sys/disk.h>

#define BLKGETSIZE64 DKIOCGETBLOCKCOUNT
#endif /* APPLE_DARWIN */

#include "ext2_fs.h"
#include "ext2fs.h"

#if defined(__CYGWIN__) || defined (WIN32)
#include "windows.h"
#include "winioctl.h"

errcode_t ext2fs_get_device_size(const char *file, int blocksize,
				 blk_t *retblocks)
{
	HANDLE dev;
	PARTITION_INFORMATION pi;
	DISK_GEOMETRY gi;
	DWORD retbytes;
	LARGE_INTEGER filesize;

	dev = CreateFile(file, GENERIC_READ, 
			 FILE_SHARE_READ | FILE_SHARE_WRITE ,
                	 NULL,  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,  NULL); 
 
	if (dev == INVALID_HANDLE_VALUE)
		return EBADF;
	if (DeviceIoControl(dev, IOCTL_DISK_GET_PARTITION_INFO,
			    &pi, sizeof(PARTITION_INFORMATION),
			    &pi, sizeof(PARTITION_INFORMATION),
			    &retbytes, NULL)) {

		*retblocks = pi.PartitionLength.QuadPart / blocksize;
	
	} else if (DeviceIoControl(dev, IOCTL_DISK_GET_DRIVE_GEOMETRY,
				&gi, sizeof(DISK_GEOMETRY),
				&gi, sizeof(DISK_GEOMETRY),
				&retbytes, NULL)) {

		*retblocks = gi.BytesPerSector *
			     gi.SectorsPerTrack *
			     gi.TracksPerCylinder *
			     gi.Cylinders.QuadPart / blocksize;

	} else if (GetFileSizeEx(dev, &filesize)) {
		*retblocks = filesize.QuadPart / blocksize;
	}

	CloseHandle(dev);
	return 0;
}

#else

static int valid_offset (int fd, ext2_loff_t offset)
{
	char ch;

	if (ext2fs_llseek (fd, offset, 0) < 0)
		return 0;
	if (read (fd, &ch, 1) < 1)
		return 0;
	return 1;
}

/*
 * Returns the number of blocks in a partition
 */
errcode_t ext2fs_get_device_size(const char *file, int blocksize,
				 blk_t *retblocks)
{
	int	fd;
#ifdef BLKGETSIZE64
   unsigned long long size;
#elif defined(BLKGETSIZE)
	unsigned long	size;
#endif
	ext2_loff_t high, low;
#ifdef FDGETPRM
	struct floppy_struct this_floppy;
#endif
#ifdef HAVE_SYS_DISKLABEL_H
	int part;
	struct disklabel lab;
	struct partition *pp;
	char ch;
#endif /* HAVE_SYS_DISKLABEL_H */

#ifdef HAVE_OPEN64
	fd = open64(file, O_RDONLY);
#else
	fd = open(file, O_RDONLY);
#endif
	if (fd < 0)
		return errno;

#if defined(BLKGETSIZE) || defined(BLKGETSIZE64)
#ifdef BLKGETSIZE
	if (ioctl(fd, BLKGETSIZE, &size) >= 0) {
#elif defined(BLKGETSIZE64)
	if (ioctl(fd, BLKGETSIZE64, &size) >= 0) {
      if ((sizeof(*retblocks) < sizeof(unsigned long long))
          && ((size / (blocksize / 512)) > 0xFFFFFFFF))
         return EFBIG;
#endif
		close(fd);
		*retblocks = size / (blocksize / 512);
		return 0;
	}
#endif
#ifdef FDGETPRM
	if (ioctl(fd, FDGETPRM, &this_floppy) >= 0) {
		close(fd);
		*retblocks = this_floppy.size / (blocksize / 512);
		return 0;
	}
#endif
#ifdef HAVE_SYS_DISKLABEL_H
#if (defined(__FreeBSD__) && __FreeBSD_version < 500040) || defined(APPLE_DARWIN) 
	/* old disklabel interface */
	part = strlen(file) - 1;
	if (part >= 0) {
		ch = file[part];
		if (isdigit(ch))
			part = 0;
		else if (ch >= 'a' && ch <= 'h')
			part = ch - 'a';
		else
			part = -1;
	}
	if (part >= 0 && (ioctl(fd, DIOCGDINFO, (char *)&lab) >= 0)) {
		pp = &lab.d_partitions[part];
		if (pp->p_size) {
			close(fd);
			*retblocks = pp->p_size / (blocksize / 512);
			return 0;
		}
	}
#else /* __FreeBSD_version < 500040 */
	{
	    off_t ms;
	    u_int bs;
	    if (ioctl(fd, DIOCGMEDIASIZE, &ms) >= 0) {
		*retblocks = ms / blocksize;
		return 0;
	    }
	}
#endif /* __FreeBSD_version < 500040 */
#endif /* HAVE_SYS_DISKLABEL_H */

	/*
	 * OK, we couldn't figure it out by using a specialized ioctl,
	 * which is generally the best way.  So do binary search to
	 * find the size of the partition.
	 */
	low = 0;
	for (high = 1024; valid_offset (fd, high); high *= 2)
		low = high;
	while (low < high - 1)
	{
		const ext2_loff_t mid = (low + high) / 2;

		if (valid_offset (fd, mid))
			low = mid;
		else
			high = mid;
	}
	valid_offset (fd, 0);
	close(fd);
	*retblocks = (low + 1) / blocksize;
	return 0;
}

#endif /* WIN32 */

#ifdef DEBUG
int main(int argc, char **argv)
{
	blk_t	blocks;
	int	retval;
	
	if (argc < 2) {
		fprintf(stderr, "Usage: %s device\n", argv[0]);
		exit(1);
	}

	retval = ext2fs_get_device_size(argv[1], 1024, &blocks);
	if (retval) {
		com_err(argv[0], retval,
			"while calling ext2fs_get_device_size");
		exit(1);
	}
	printf("Device %s has %d 1k blocks.\n", argv[1], blocks);
	exit(0);
}
#endif
