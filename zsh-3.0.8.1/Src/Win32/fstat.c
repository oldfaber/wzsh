/*
 *  Copyright (c) 2012, oldfaber _at_ gmail.com
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 *  SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 *  RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 *  NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE
 *  USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * stat()/fstat() clone
 *      the FILETIME_1970 value comes from the 'net (i.d. libntp)
 *      nt_stat() returns the wrong nlink count, always 1, st_ino == 0
 * TODO
 *      32 bit ONLY !
*/


#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#if defined(STDCRTLIB)
/* Standard C library, get _get_osfhandle declaration */
#include <stdio.h>
#include <io.h>
#define _nt_get_osfhandle(fd) _get_osfhandle(fd)
#else
/* get _nt_get_osfhandle declaration */
#include "forklib.h"
#endif

/* symlink, defined in sys/stat.h for POSIX */
#if !defined(S_IFLNK)
# define S_IFLNK 0xA000
#endif


#define UNSAFEMAX(a,b) ((a)>(b)?(a):(b))
#define MAKEDWORDLONG(a,b)    ((DWORDLONG)(((DWORD)(a))|(((DWORDLONG)((DWORD)(b)))<<32)))

/* 100-nanoseconds between 1/1/1601 and 1/1/1970 */
#define FILETIME_1970		(116444736000000000LL)
#define UM_FILETIME_TO_TIME(FT)	\
	((time_t) \
	 ((MAKEDWORDLONG(FT.dwLowDateTime, FT.dwHighDateTime)-((DWORDLONG)FILETIME_1970))/(10000000)) \
	)
#define FILETIME_TO_TIME(FT) UNSAFEMAX(0,UM_FILETIME_TO_TIME(FT))


static unsigned int attr2mode (const DWORD attr)
{
	unsigned int fmode = 0;

	if (attr & FILE_ATTRIBUTE_DIRECTORY)
		fmode |= (S_IFDIR | S_IREAD | S_IEXEC);
	else
		fmode |= (S_IFREG | S_IREAD | S_IEXEC);
	if (!(attr & FILE_ATTRIBUTE_READONLY))
		fmode |= S_IWRITE;
	if (attr & FILE_ATTRIBUTE_REPARSE_POINT) {
		if (fmode & S_IFDIR)
			fmode &= ~S_IFDIR;
		fmode |= S_IFLNK;
	}
	return fmode;
}


/*
 DESCRIPTION
	fill the stat structure from handle inforamtion
 NOTES
	from a handle st_dev, st_rdev are NOT known !
*/

int hstat(HANDLE fh, struct stat *fs)
{
	BY_HANDLE_FILE_INFORMATION fdata;

	if (!GetFileInformationByHandle (fh, &fdata))
		return 0;
	/* it is 64 bit, we only keep the lower 32 */
	fs->st_ino = (ino_t)fdata.nFileIndexLow;
	fs->st_mode = attr2mode(fdata.dwFileAttributes);
	fs->st_nlink = (short)fdata.nNumberOfLinks;
	/* it is 64 bit, we only keep the lower 32 */
	fs->st_size = fdata.nFileSizeLow;
	fs->st_atime = FILETIME_TO_TIME(fdata.ftLastAccessTime);
	fs->st_mtime = FILETIME_TO_TIME(fdata.ftLastWriteTime);
	fs->st_ctime = FILETIME_TO_TIME(fdata.ftCreationTime);
	return 1;
}


int nt_fstat(int fd, struct stat *fs)
{
	HANDLE fh;

	if (!fs) {
		errno = EINVAL;
		return (-1);
	}
	fh = (HANDLE)_nt_get_osfhandle(fd);
	if (fh == INVALID_HANDLE_VALUE) {
		errno = EINVAL;
		return (-1);
	}
	/* also zero the st_dev, st_rdev fields */
	memset(fs, 0, sizeof(*fs));
	if (!hstat(fh, fs)) {
		if (GetLastError() == ERROR_FILE_NOT_FOUND)
			errno = ENOENT;
		else
			errno = EINVAL;
		return (-1);
	}
	return 0;
}


static int is_server(const char *name)
{
	const char *p1, *p2;

	p1 = name;
	if (((p1[0] != '/') && (p1[0] != '\\')) || ((p1[1] != '/') && (p1[1] != '\\')))
		return 0;
	p1++;
	p2 = strrchr(name,'/');
	if (!p2)
		p2 = strchr(name,'\\');
	{
		p2--;
		while ((*p2 != '/') && (*p2 != '\\'))
			p2--;
	}
	if (p2 != p1)
		return 0;
	return 1;
}


/* from http://blogs.msdn.com/b/oldnewthing/archive/2007/10/23/5612082.aspx
   GetFileAttributes(filename) is the fastest way to test for existance and
   get the attributes */
int nt_stat(const char *filename, struct stat *fs)
{
	WIN32_FILE_ATTRIBUTE_DATA fdata;
	static const char rootdir[] = "C:\\";

	if (!fs || !filename) {
		errno = EINVAL;
		return (-1);
	}
	/* ZSH does not use the stat info for server names, it simply needs a
	   non-error return value with the directory attribute set, so for
	   server names we return the attributes for C:\ */
	if (is_server(filename))
		filename = rootdir;
	if (!GetFileAttributesEx(filename, GetFileExInfoStandard, &fdata)) {
		if (GetLastError() == ERROR_FILE_NOT_FOUND)
			errno = ENOENT;
		else
			errno = EINVAL;
		return (-1);
	}
	memset(fs, 0, sizeof(*fs));
	fs->st_ino = 0;
	fs->st_nlink = 1;
	fs->st_mode = attr2mode(fdata.dwFileAttributes);
	/* it is 64 bit, we only keep the lower 32 */
	fs->st_size = fdata.nFileSizeLow;
	fs->st_atime = FILETIME_TO_TIME(fdata.ftLastAccessTime);
	fs->st_mtime = FILETIME_TO_TIME(fdata.ftLastWriteTime);
	fs->st_ctime = FILETIME_TO_TIME(fdata.ftCreationTime);
	return (0);
}
