/*-
 * Copyright (c) 1980, 1991 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
 */

/* dirent.c
 * directory interface functions. Sort of like dirent functions on unix.
 * Also allow browsing network shares as if they were directories
 *
 * -amol
 *
 * from
 * $Header: /p/tcsh/cvsroot/tcsh/win32/dirent.c,v 1.9 2006/04/07 00:57:59 amold Exp $
 *
 * oldfaber:
 *      This version
 *      - removes the strsafe dependency
 *      - uses _alloca() for the opendir() buffer name
 *      - renames dd_buf to dd_dir and allocate it in DIR
 *      - removes unseless d_off in struct dirent and dd_loc,dd_size in DIR
 *      - sets d_ino to 0
 *        zsh 3.0.x for _WIN32 does not use d_ino (it is used in compat.c::zgetcwd())
 *      - fixes the memory leak using only one allocation
 *      - adds a NULL inbuf check
 *      - calloc()'s the DIR structure
 *      - sets the d_namelen field
 *      - reduces the conversions in case of network path
 *      - accepts //?/c: as a local path
 *      - removes the dynamic linking to MPR.DLL (less code)
 *      adds a main() for testing
 * BEWARE
 *      d_name is an array of NAME_MAX, but this code uses MAX_PATH for memcopies ...
 * TODO
 *      use a char *start, initialized to buf or buf+2 instead of memmove ?
 * NOTES:
 *      dirent.c in tcsh is different from original zsh/winnt/dirent.c:
 *      - crash if inbuf == NULL
 *      - silently uses "." if strlen(inbuf) == 0
 *      - copies inbuf (it is const) into an allocated buffer
 *        (but leaks the first allocated buffer!)
 *      - uses strsafe functions (but also uses memcpy)
 *      - bug: on alloc error uses dptr even if NULL
 */


#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <errno.h>
#include <malloc.h>
#include <direct.h>

#include "dirent.h"

#if defined(_MSC_VER)
#pragma intrinsic("memset", "memcpy", "strlen", "strcpy")
#pragma comment(lib, "mpr.lib")
#endif

#if defined(W32DEBUG) && defined(__TESTING__) && defined(_MSC_VER)
# define _CRTDBG_MAP_ALLOC
/* Microsoft header conflict ! */
# undef _malloca
# include <stdlib.h>
# include <crtdbg.h>
# define xmalloc(s) _malloc_dbg(s, _NORMAL_BLOCK, __FILE__, __LINE__)
# define xcalloc(s) _calloc_dbg(1, s, _NORMAL_BLOCK, __FILE__, __LINE__)
# define xfree(p)   _free_dbg(p, _NORMAL_BLOCK)
#elif defined(__TESTING__)
# define xmalloc(s)  malloc(s)
# define xcalloc(s)  calloc(1, s)
# define xfree(p)    free(p)
#else
# define xmalloc(s) malloc(s)
# define xcalloc(s) calloc(1, s)
# define xfree(p)   free(p)
#endif

#define IS_ROOT 0x01
#define IS_NET  0x02
#define INVALID_HND ((intptr_t)INVALID_HANDLE_VALUE)


static HANDLE open_enum(char*, WIN32_FIND_DATA*);
static void close_enum(DIR*);
static int enum_next_share(DIR*);

typedef struct _enum_h {
	unsigned char *netres;
	HANDLE henum;
} nethandle_t;


DIR *opendir(const char *inbuf)
{
	DIR *dptr;
	char *tmp;
	char *buf;
	size_t buflen;
	WIN32_FIND_DATA fdata;
	int is_net = 0;
	int had_error = 0;

	/* zsh does not seem to call opendir(NULL), but testing is safer */
	if (!inbuf) {
		errno = EFAULT;
		return NULL;
	}

	buflen = strlen(inbuf);
	if (!buflen) {
		errno = ENOTDIR;
		return NULL;
	}

	/* allocate buf and copy const inbuf */
	/* may add "X:" at the beginning and slash+* at the end */
	buf = (char *)_alloca(buflen + 5);
	if (!buf) {
		errno = ENOMEM;
		return NULL;
	}
#if defined(W32DEBUG) && defined(__TESTING__)
	/* poison buf */
	memset(buf, 'X', buflen + 5);
#endif
	strcpy(buf, inbuf);

	if ((buflen > 2) &&
	    ((((buf[0] == '/') && (buf[1] == '/')) || ((buf[0] == '\\') && (buf[1] == '\\'))) &&
	     /* avoid //?/, it's a long UNC path, NOT a net path */
	     (buf[2] != '?'))) {
		/* open_enum will convert '/' to '\', so we skip the
		   conversion from '/' to '\' below */
		is_net = 1;
		tmp = buf + buflen;
	} else {
		/* convert '\\' to '/' only for paths */
		tmp = buf;
		while (*tmp) {
#ifdef DSPMBYTE
			if (Ismbyte1(*tmp) && *(tmp + 1))
				tmp ++;
			else
#endif
				if (*tmp == '\\')
					*tmp = '/';
			tmp++;
		}
		/* if buf lacks a terminating '/' add it now */
		if (*(tmp - 1) != '/') {
			*tmp++ = '/';
			buflen++;
		}
		/* if buf starts with '/' prepend the current drive */
		/* (avoid //?... !) */
		if ((buf[0] == '/') && (buf[1] != '/')) {
			memmove(buf + 2, buf, buflen);
			buf[0] = (char)('@' + _getdrive());
			buf[1] = ':';
			tmp += 2;
			buflen += 2;
		}
		/* terminate buf */
		*tmp = '\0';
	}

	dptr = (DIR *)xcalloc(sizeof(DIR) + buflen + 5);
	if (!dptr) {
		errno = ENOMEM;
		had_error = 1;
		goto done;
	}
	dptr->dd_handle = INVALID_HND;

	/* buf has the full string */
	if (is_net) {
		dptr->dd_handle = (intptr_t)open_enum(buf, &fdata);
		dptr->dd_stat = IS_NET;
	}
	if (dptr->dd_handle == INVALID_HND) {
		if (*(tmp - 1) != '/') {
			/* may be true only if is_net is nonzero */
			*tmp++ = '/';
			buflen++;
		}
		/* add a terminating '*' */
		*tmp++ = '*';
		*tmp = '\0';
		/* buf[] wasn't a "\\server" path */
		dptr->dd_stat = 0;
		dptr->dd_handle = (intptr_t)FindFirstFile(buf, &fdata);
	}
	if (dptr->dd_handle == INVALID_HND) {
		if (GetLastError() == ERROR_DIRECTORY)
			errno = ENOTDIR;
		else
			errno = ENOENT;
		had_error = 1;
		goto done;
	}
	memcpy(dptr->dd_name, buf, buflen);

	if (lstrcmpi(fdata.cFileName, ".")) {
		memcpy((dptr->dd_dir).d_name, ".", 2);
		(dptr->dd_dir).d_namlen = (unsigned short)1U;
		dptr->dd_stat |= IS_ROOT;
	} else {
		memcpy((dptr->dd_dir).d_name, fdata.cFileName, MAX_PATH);
		(dptr->dd_dir).d_namlen = (unsigned short)strlen((dptr->dd_dir).d_name);
	}

done:
	if (had_error) {
		xfree(dptr);
		dptr = NULL;
	}

	return dptr;
}


int closedir(DIR *dptr)
{
	if (!dptr)
		return 0;
	if (dptr->dd_stat & IS_NET) {
		close_enum(dptr);
	} else
		FindClose((HANDLE)dptr->dd_handle);
	/* dd_dir is part of DIR */
	xfree(dptr);
	return 0;
}


#if 0
void rewinddir(DIR *dptr)
{
	HANDLE hfind;
	WIN32_FIND_DATA fdata;
	char *tmp = dptr->dd_name;

	if (!dptr)
		return;

	if (dptr->dd_stat & IS_NET) {
		hfind = open_enum(tmp, &fdata);
		close_enum(dptr);
		dptr->dd_handle = (intptr_t)hfind;
	} else {
		hfind = FindFirstFile(tmp, &fdata);
		/* rewinddir cannot fail, if it fails ... ? */
		if (hfind != INVALID_HANDLE_VALUE) {
                        /* close the OLD handle */
			FindClose((HANDLE)(dptr->dd_handle));
			dptr->dd_handle = (intptr_t)hfind;
		}
	}
	memcpy((dptr->dd_dir).d_name, fdata.cFileName, MAX_PATH);
}
#endif


struct dirent *readdir(DIR *dir)
{
	WIN32_FIND_DATA fdata;
	HANDLE hfind;
	const char *tmp;

	if (!dir)
		return NULL;

	memset(&fdata, 0, sizeof(fdata));
	if (dir->dd_stat & IS_NET) {
		if (enum_next_share(dir) < 0)
			return NULL;
	} else if (dir->dd_stat & IS_ROOT) {
		// special hack for root (which does not have . or ..)
		tmp = (const char *)(dir->dd_name);
		hfind = FindFirstFile(tmp, &fdata);
		FindClose((HANDLE)(dir->dd_handle));
		dir->dd_handle = (intptr_t)hfind;
		memcpy((dir->dd_dir).d_name, fdata.cFileName, MAX_PATH);
		(dir->dd_dir).d_namlen = (unsigned short)strlen((dir->dd_dir).d_name);
		dir->dd_stat &= ~IS_ROOT;
		return &dir->dd_dir;
	}
	if (!(dir->dd_stat & IS_NET))
		do {
			if (!FindNextFile((HANDLE)(dir->dd_handle), &fdata))
				return NULL;
			if (!(fdata.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN))
				break;
		} while (1);
	if (!(dir->dd_stat & IS_NET)) {
                // @@@@ use strlcpy ... will return the len
		memcpy((dir->dd_dir).d_name, fdata.cFileName, MAX_PATH);
		(dir->dd_dir).d_namlen = (unsigned short)strlen((dir->dd_dir).d_name);
	}
	return &dir->dd_dir;
}


/*
 NOTES
	Support for treating share names as directories
	-amol 5/28/97
	This function is called for any string that starts with a double
	slash. If the string has more slashes a "normal" FindFirstFile
	should be used,	open_enum for a share \\server\share works, but it is
	MUCH MUCH slower than using Find[First|Next]File on \\server\share\*
	Modifies server !
*/

/* is this big enough ? Microsoft documentation uses 16K */
#define BLEN 4096

HANDLE open_enum(char *server, WIN32_FIND_DATA *fdata)
{
	NETRESOURCE netres;
	HANDLE henum;
	unsigned long ret;
	char *ptr;
	int slashes;

	nethandle_t *hnet;

	ptr = server;
	slashes = 0;

	while (*ptr) {
		if (*ptr == '/')
			*ptr = '\\';
		if (*ptr == '\\')
			slashes++;
		ptr++;
	}

	if ((slashes == 3) && (*(ptr - 1) == '\\'))
		/* special case a server name like "//server/" */
		*(ptr - 1) = '\0';
	else if (slashes > 2)
		return INVALID_HANDLE_VALUE;

	memset(fdata, 0, sizeof(WIN32_FIND_DATA));
	fdata->cFileName[0] = '.';

	netres.dwScope = RESOURCE_GLOBALNET;
	netres.dwType = RESOURCETYPE_ANY;
	netres.lpRemoteName = server;
	netres.lpProvider = NULL;
	netres.dwUsage = 0;

	ret = WNetOpenEnum(RESOURCE_GLOBALNET, RESOURCETYPE_ANY, 0, &netres, &henum);
	if (ret != NO_ERROR)
		return INVALID_HANDLE_VALUE;

	hnet = (nethandle_t *)xmalloc(sizeof(nethandle_t));
	hnet->netres = (unsigned char *)xcalloc(BLEN);
	hnet->henum = henum;

	return (HANDLE)hnet;
}


void close_enum(DIR *dptr)
{
	nethandle_t *hnet;

	hnet = (nethandle_t*)(dptr->dd_handle);

	xfree(hnet->netres);
	WNetCloseEnum(hnet->henum);
	xfree(hnet);
}


int enum_next_share(DIR *dir)
{
	nethandle_t *hnet;
	char *tmp, *p1;
	HANDLE henum;
	DWORD count, breq,ret;
	size_t dl;

	hnet = (nethandle_t*)(dir->dd_handle);
	henum = hnet->henum;
	count = 1;
	breq = BLEN;

	ret = WNetEnumResource(henum, &count, hnet->netres, &breq);
	if (ret != NO_ERROR)
		return -1;

	tmp = ((NETRESOURCE*)hnet->netres)->lpRemoteName;
	p1 = &tmp[2];
#ifdef DSPMBYTE
	for (; *p1 != '\\'; p1 ++)
		if (Ismbyte1(*p1) && *(p1 + 1))
			p1 ++;
#else /* DSPMBYTE */
	while(*p1++ != '\\')
		;
#endif /* DSPMBYTE */

	dl = strlen(p1);
	memcpy((dir->dd_dir).d_name, p1, dl + 1);
	(dir->dd_dir).d_namlen = (unsigned short)dl;

	return 0;
}



#if defined(__TESTING__)

static const char *const xargs[] = {
	"C:\\tmp",
	"\\tmp",
	"\\\\?\\c:\\tmp\\",
	"\\\\?\\d:\\temp",
	"//?/c:/tmp/",
	"//?/d:/temp",
	"//aserver1/",
	"//aserver1",
	"//aserver1/ut/",
	"//aserver1/ut",
	NULL
};


int main(int argc, char *argv[])
{
	DIR *dp;
	struct dirent *ep;
	const char *dname;
	char *const *darg;
	char *const *dl;
	int dent;
	size_t llen;

	if (argc >= 2)
		darg = ++argv;
	else
		darg = (char *const *)xargs;
	/* test NULL case */
	dp = opendir(NULL);
	if (dp)
		fprintf(stderr, "opendir(NULL) NOT FAILED!\n");
	dp = opendir("");
	if (dp)
		fprintf(stderr, "opendir("") NOT FAILED!\n");
	for (dl = darg; *dl; dl++) {
		dname = *dl;
		dp = opendir(dname);
		if (dp) {
			dent = 0;
			while ((ep = readdir(dp))) {
				dent++;
				llen = strlen(ep->d_name);
				if (ep->d_namlen != (unsigned short)llen)
					printf("DEBUG: %u differs from %u entries\n", ep->d_namlen, llen);
				/* print the name */
				// printf("file %s\n", ep->d_name);
			}
			printf("DEBUG: %s has %d entries\n", dname, dent);
			(void) closedir (dp);
		} else
			perror ("Couldn't open the directory");
	}
#if defined(_MSC_VER) && defined(W32DEBUG)
	/* uses OutputDebugString() !! */
	if (_CrtDumpMemoryLeaks())
		puts("leaks!");
#endif
	return 0;
}

#endif
