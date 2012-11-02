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

/*
 * from
 * $Header: /p/tcsh/cvsroot/tcsh/win32/dirent.h,v 1.6 2006/03/03 22:08:45 amold Exp $
 *
 * dirent.h
 * directory interface functions. Sort of like dirent functions on unix.
 * -amol
 *
 */

#if !defined(LOCAL_DIRENT) && defined(__GNUC__)
#include_next <dirent.h>
#define DIRENT_H
#endif

#ifndef DIRENT_H
#define DIRENT_H

#include <stdio.h>
#include <io.h>

/* avoid an external include dependency */
#if defined(_MSC_VER)
/* Visual Studio 2005+ has this defined */
# if !defined(_INTPTR_T_DEFINED)
#  define _INTPTR_T_DEFINED
#  define intptr_t long
# endif
#endif

struct dirent {
	long            d_ino;          /* always 0 */
	unsigned short  d_reclen;       /* always 0 */
	unsigned short  d_namlen;       /* Length of name in d_name. */
        /* Find*FileA returns MAX_PATH==FILENAME_MAX characters */
	char            d_name[FILENAME_MAX];
};

typedef struct {
	struct _finddata_t dd_dta; /* NOT used */
	struct dirent   dd_dir;
        /* holds an HANDLE */
	intptr_t        dd_handle;
        /* holds dd_name type */
	int             dd_stat;
	/* given path for dir with search pattern (struct is extended) */
	char            dd_name[1];
} DIR;

#ifdef	__cplusplus
extern "C" {
#endif

DIR *opendir(const char *);
struct dirent *readdir(DIR *);
int closedir(DIR *);
void rewinddir(DIR *);

#ifdef	__cplusplus
}
#endif

#endif /* DIRENT_H */
